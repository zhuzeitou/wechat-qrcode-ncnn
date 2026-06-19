using System;
using System.Buffers;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using UnityEngine;
using ZZT.QRCode.Native;

namespace ZZT.QRCode
{
    /// <summary>
    /// QR Code Detector.
    /// <para>
    /// This class provides methods to detect and decode QR codes from image files, textures or raw pixel data.
    /// It wraps the native C++ implementation.
    /// </para>
    /// </summary>
    public class QrcodeDetector : IDisposable
    {
        /// <summary>
        /// Pixel format definition.
        /// </summary>
        public enum PixelFormat
        {
            /// <summary>Single channel Gray</summary>
            Gray = 0,
            /// <summary>3 channels RGB</summary>
            RGB = 1,
            /// <summary>3 channels BGR</summary>
            BGR = 2,
            /// <summary>4 channels RGBA</summary>
            RGBA = 3,
            /// <summary>4 channels BGRA</summary>
            BGRA = 4,
            /// <summary>4 channels ARGB</summary>
            ARGB = 5,
            /// <summary>4 channels ABGR</summary>
            ABGR = 6
        }

        private static readonly Dictionary<TextureFormat, PixelFormat> FormatMap = new()
        {
            { TextureFormat.Alpha8, PixelFormat.Gray },
            { TextureFormat.R8, PixelFormat.Gray },
            { TextureFormat.RGB24, PixelFormat.RGB },
            { TextureFormat.RGBA32, PixelFormat.RGBA },
            { TextureFormat.BGRA32, PixelFormat.BGRA },
            { TextureFormat.ARGB32, PixelFormat.ARGB }
        };

        private static int GetBpp(PixelFormat fmt)
        {
            switch (fmt)
            {
                case PixelFormat.Gray: return 1;
                case PixelFormat.RGB: return 3;
                case PixelFormat.BGR: return 3;
                case PixelFormat.RGBA: return 4;
                case PixelFormat.BGRA: return 4;
                case PixelFormat.ARGB: return 4;
                case PixelFormat.ABGR: return 4;
                default: return 0;
            }
        }

        private Bridge.NativeDetector _detector;

        private static readonly SemaphoreSlim GlobalSemaphore = new(Math.Max(1, Environment.ProcessorCount - 1));

        // ─── Log callback public API ──────────────────────────────────────────
        //
        // The native library emits log messages through a single, process-wide C
        // callback.  We root the P/Invoke delegate inside Bridge and expose
        // standard C# events here.  By default no handler is registered and the
        // library is silent — exactly zero per-frame overhead.
        //
        // Two delivery paths are available:
        //
        //   OnLogMessage              — fires on whatever thread the native
        //                               callback invoked us from (any thread).
        //                               Suitable for file loggers, telemetry, etc.
        //
        //   OnLogMessageMainThread    — guarantees delivery on the Unity main
        //                               thread via SynchronizationContext.Post,
        //                               or inline when the log already originates
        //                               from the main thread.  Suitable for
        //                               handlers that touch Unity APIs.
        //
        // IMPORTANT: callbacks may arrive from any native thread.  Do not call
        // Unity main-thread APIs directly inside a handler without dispatching.

        /// <summary>
        /// Log severity levels matching <c>zzt_qrcode_log_level_t</c> in the C API.
        /// </summary>
        public enum LogLevel
        {
            /// <summary>Fine-grained diagnostic events.</summary>
            Verbose = 0,
            /// <summary>Debug-level messages.</summary>
            Debug = 1,
            /// <summary>Normal informational messages.</summary>
            Info = 2,
            /// <summary>Warning conditions.</summary>
            Warn = 3,
            /// <summary>Error conditions.</summary>
            Error = 4,
        }

        /// <summary>
        /// Sets the process-wide minimum log level for the native library.
        /// Native log messages below this level will be filtered out before dispatch.
        /// <para>
        /// The process-wide default is <see cref="LogLevel.Warn"/>.
        /// Setting the level to <see cref="LogLevel.Verbose"/> enables detailed performance diagnostics for decoding steps,
        /// which can be useful for debugging but may add overhead.
        /// </para>
        /// </summary>
        /// <param name="minLevel">The minimum log level to set.</param>
        /// <returns>An <see cref="QrcodeResults.ErrorCode"/> indicating success (OK) or a failure code.</returns>
        public static QrcodeResults.ErrorCode SetMinimumLogLevel(LogLevel minLevel)
        {
            int err = Bridge.SetLogLevel((int)minLevel);
            return (QrcodeResults.ErrorCode)err;
        }

        /// <summary>
        /// Represents the method that will handle the <see cref="OnLogMessage"/> and
        /// <see cref="OnLogMessageMainThread"/> events.
        /// </summary>
        /// <param name="level">Log severity level.</param>
        /// <param name="message">The log message text.</param>
        public delegate void LogMessageHandler(LogLevel level, string message);

        // ─── Shared lock and handler storage ────────────────────────────────

        private static readonly object LogMessageLock = new();
        private static LogMessageHandler _directHandlers;
        private static LogMessageHandler _mainThreadHandlers;
        private static bool _bridgeLogSubscribed;

        // ─── Main-thread identity (captured at startup) ─────────────────────

        private static int _mainThreadId;
        private static SynchronizationContext _mainThreadContext;

        // ─── Epoch guard ────────────────────────────────────────────────────
        // Incremented on every ClearLogCallbackState().  Main-thread messages
        // posted via SynchronizationContext capture the current epoch value.
        // When the callback eventually runs it compares against the live epoch;
        // a mismatch means the state was reset (domain reload / quit) and the
        // message is silently discarded.
        private static int _epoch;

        // ─── Bridge subscription helpers ────────────────────────────────────

        private static void SubscribeBridgeIfNeeded()
        {
            if (_bridgeLogSubscribed) return;
            Bridge.LogMessageReceived += OnBridgeLogMessage;
            Bridge.EnsureLogCallbackInstalled();
            _bridgeLogSubscribed = true;
        }

        private static void UnsubscribeBridgeIfNoHandlers()
        {
            if (_directHandlers != null || _mainThreadHandlers != null) return;
            if (!_bridgeLogSubscribed) return;
            Bridge.LogMessageReceived -= OnBridgeLogMessage;
            Bridge.ClearLogCallback();
            _bridgeLogSubscribed = false;
        }

        // ─── OnLogMessage (direct, any thread) ──────────────────────────────

        /// <summary>
        /// Occurs when the native QR code library emits a log message.
        /// <para>
        /// Subscribe with <c>+=</c> and unsubscribe with <c>-=</c>.  Native logging
        /// is completely silent until at least one handler is attached.
        /// </para>
        /// <para>
        /// <b>Thread safety:</b> This event is raised on whatever thread the native
        /// callback invoked us from (any native thread).  Do not rely on Unity
        /// main-thread APIs inside a handler without proper dispatching.
        /// Exceptions thrown by handlers are silently caught to prevent affecting
        /// QR detection.
        /// </para>
        /// </summary>
        public static event LogMessageHandler OnLogMessage
        {
            add
            {
                if (value == null) return;
                lock (LogMessageLock)
                {
                    _directHandlers += value;
                    SubscribeBridgeIfNeeded();
                }
            }
            remove
            {
                if (value == null) return;
                lock (LogMessageLock)
                {
                    _directHandlers -= value;
                    UnsubscribeBridgeIfNoHandlers();
                }
            }
        }

        // ─── OnLogMessageMainThread (main-thread dispatched) ────────────────

        /// <summary>
        /// Occurs when the native QR code library emits a log message.
        /// <para>
        /// Unlike <see cref="OnLogMessage"/>, this event guarantees that handlers
        /// are called on the Unity main thread via <c>SynchronizationContext.Post</c>,
        /// or inline when the log already originates from the main thread.
        /// </para>
        /// <para>
        /// <b>Reentrancy:</b> When the log is produced by code running on the main
        /// thread (e.g. a <c>DetectAndDecodeSync</c> call that internally triggers
        /// a log), the handler is invoked synchronously inline.  Avoid long-running
        /// or blocking work inside handlers to prevent reentrancy surprises.
        /// </para>
        /// <para>
        /// If no <c>SynchronizationContext</c> was captured at startup (very early
        /// Unity lifecycle), the event is silently discarded when the callback
        /// arrives from a non-main thread.  Exceptions thrown by handlers are
        /// silently caught.
        /// </para>
        /// </summary>
        public static event LogMessageHandler OnLogMessageMainThread
        {
            add
            {
                if (value == null) return;
                lock (LogMessageLock)
                {
                    _mainThreadHandlers += value;
                    SubscribeBridgeIfNeeded();
                }
            }
            remove
            {
                if (value == null) return;
                lock (LogMessageLock)
                {
                    _mainThreadHandlers -= value;
                    UnsubscribeBridgeIfNoHandlers();
                }
            }
        }

        // ─── OnBridgeLogMessage ─────────────────────────────────────────────

        /// <summary>
        /// Bridge between <see cref="Bridge.LogMessageReceived"/> (raw int level)
        /// and the typed public events.  Called from any native thread.
        /// </summary>
        private static void OnBridgeLogMessage(int level, string message)
        {
            LogMessageHandler directSnapshot;
            LogMessageHandler mainSnapshot;
            int epochAtCapture;

            lock (LogMessageLock)
            {
                directSnapshot = _directHandlers;
                mainSnapshot = _mainThreadHandlers;
                epochAtCapture = _epoch;
            }

            // 1. Direct: invoke immediately on the current (any) thread.
            if (directSnapshot != null)
            {
                foreach (LogMessageHandler handler in directSnapshot.GetInvocationList())
                {
                    try
                    {
                        handler((LogLevel)level, message);
                    }
                    catch
                    {
                        // Swallow — logging must never affect QR behaviour.
                    }
                }
            }

            // 2. Main-thread: dispatch to Unity main thread or inline.
            if (mainSnapshot == null) return;

            if (Environment.CurrentManagedThreadId == _mainThreadId)
            {
                // Already on captured main thread → inline (may be reentrant).
                InvokeMainSnapshot(mainSnapshot, level, message);
            }
            else
            {
                if (_mainThreadContext == null)
                {
                    // No SynchronizationContext captured — cannot dispatch from
                    // a non-main thread. Silently discard to avoid calling
                    // handlers on the wrong thread.
                    return;
                }

                // Off main thread → Post via SynchronizationContext.
                var capturedMessage = message;
                var capturedLevel = level;
                var capturedEpoch = epochAtCapture;
                var capturedSnapshot = mainSnapshot;
                _mainThreadContext.Post(_ =>
                {
                    // Epoch guard: discard if state was reset between Post and execution
                    if (_epoch != capturedEpoch) return;
                    InvokeMainSnapshot(capturedSnapshot, capturedLevel, capturedMessage);
                }, null);
            }
        }

        private static void InvokeMainSnapshot(LogMessageHandler snapshot, int level, string message)
        {
            foreach (LogMessageHandler handler in snapshot.GetInvocationList())
            {
                try
                {
                    handler((LogLevel)level, message);
                }
                catch
                {
                    // Swallow — logging must never affect QR behaviour.
                }
            }
        }

        // ─── Lifecycle / cleanup ────────────────────────────────────────────

        /// <summary>
        /// Clears the native callback on domain reload so stale native pointers
        /// are never invoked after managed code is torn down.
        /// </summary>
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        private static void OnSubsystemRegistration()
        {
            ClearLogCallbackState();
        }

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterAssembliesLoaded)]
        private static void AfterAssembliesLoaded()
        {
            // Capture main-thread identity for later main-thread dispatch.
            _mainThreadId = Environment.CurrentManagedThreadId;
            _mainThreadContext = SynchronizationContext.Current;

            Application.quitting -= OnApplicationQuitting;
            Application.quitting += OnApplicationQuitting;
        }

        private static void OnApplicationQuitting()
        {
            ClearLogCallbackState();
        }

        /// <summary>
        /// Tears down all managed handlers, unsubscribes from Bridge, clears the
        /// native callback, and increments the epoch so any in-flight main-thread
        /// Posts are silently discarded.
        /// </summary>
        private static void ClearLogCallbackState()
        {
            lock (LogMessageLock)
            {
                if (_bridgeLogSubscribed)
                {
                    Bridge.LogMessageReceived -= OnBridgeLogMessage;
                    _bridgeLogSubscribed = false;
                }
                _directHandlers = null;
                _mainThreadHandlers = null;
            }
            Bridge.ClearLogCallback();
            // Increment AFTER clearing so in-flight Posts see a mismatch.
            Interlocked.Increment(ref _epoch);
        }

        /// <summary>
        /// Creates a new QrcodeDetector instance.
        /// <para>
        /// Initializes the native detector.
        /// </para>
        /// </summary>
        public QrcodeDetector()
        {
            _detector = Bridge.CreateDetector();
        }

        /// <summary>
        /// Releases the native detector resources.
        /// <para>
        /// Should be called when the detector is no longer needed to prevent memory leaks.
        /// </para>
        /// </summary>
        public void Dispose()
        {
            Bridge.ReleaseDetector(_detector);
        }

        /// <summary>
        /// Detects and decodes a QR code from an image file path synchronously.
        /// </summary>
        /// <param name="path">The path to the image file.</param>
        /// <returns>The detection results.</returns>
        public QrcodeResults DetectAndDecodeSync(string path)
        {
            int err = Bridge.DetectAndDecode(_detector, path, out Bridge.NativeResult resultPtr);

            QrcodeResults ret = QrcodeResults.Parse(resultPtr, err);
            Bridge.ReleaseResult(resultPtr);

            return ret;
        }

        /// <summary>
        /// Detects and decodes a QR code from an image file path asynchronously.
        /// </summary>
        /// <param name="path">The path to the image file.</param>
        /// <returns>A task that represents the asynchronous operation. The task result contains the detection results.</returns>
        public async Task<QrcodeResults> DetectAndDecode(string path)
        {
            await GlobalSemaphore.WaitAsync();
            try
            {
                return await Task.Run(() => DetectAndDecodeSync(path));
            }
            finally
            {
                GlobalSemaphore.Release();
            }
        }

        /// <summary>
        /// Detects and decodes a QR code from image file data (byte array) synchronously.
        /// </summary>
        /// <param name="data">The image file data (e.g., JPEG or PNG bytes).</param>
        /// <returns>The detection results.</returns>
        public QrcodeResults DetectAndDecodeSync(byte[] data)
        {
            if (data == null || data.Length == 0)
            {
                return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
            }

            int err = Bridge.DetectAndDecode(_detector, in data[0], data.Length, out Bridge.NativeResult resultPtr);

            QrcodeResults ret = QrcodeResults.Parse(resultPtr, err);
            Bridge.ReleaseResult(resultPtr);

            return ret;
        }

        /// <summary>
        /// Detects and decodes a QR code from image file data (byte array) asynchronously.
        /// </summary>
        /// <param name="data">The image file data (e.g., JPEG or PNG bytes).</param>
        /// <returns>A task that represents the asynchronous operation. The task result contains the detection results.</returns>
        public async Task<QrcodeResults> DetectAndDecode(byte[] data)
        {
            await GlobalSemaphore.WaitAsync();
            try
            {
                return await Task.Run(() => DetectAndDecodeSync(data));
            }
            finally
            {
                GlobalSemaphore.Release();
            }
        }

        private QrcodeResults DetectAndDecodeInternal(Span<byte> pixels, PixelFormat format, int width,
            int height, int stride, bool flipVertical = false)
        {
            if (pixels.IsEmpty || width <= 0 || height <= 0)
            {
                return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
            }

            int rowBytes = width * GetBpp(format);
            int exactStride = stride <= 0 ? rowBytes : stride;
            int pixelBytes = pixels.Length;
            int expectedPixelBytes = exactStride * (height - 1) + rowBytes;
            if (pixelBytes < expectedPixelBytes)
            {
                return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
            }

            if (flipVertical)
            {
                byte[] rowBuffer = ArrayPool<byte>.Shared.Rent(rowBytes);
                Span<byte> tempRow = rowBuffer.AsSpan(0, rowBytes);

                try
                {
                    for (int y = 0; y < height / 2; y++)
                    {
                        int srcOffset = (height - 1 - y) * exactStride;
                        int dstOffset = y * exactStride;

                        Span<byte> srcRow = pixels.Slice(srcOffset, rowBytes);
                        Span<byte> dstRow = pixels.Slice(dstOffset, rowBytes);

                        srcRow.CopyTo(tempRow);
                        dstRow.CopyTo(srcRow);
                        tempRow.CopyTo(dstRow);
                    }
                }
                finally
                {
                    ArrayPool<byte>.Shared.Return(rowBuffer);
                }
            }

            int err = Bridge.DetectAndDecode(_detector, MemoryMarshal.GetReference(pixels),
                (int)format, width, height, stride, out Bridge.NativeResult resultPtr);

            QrcodeResults ret = QrcodeResults.Parse(resultPtr, err);
            Bridge.ReleaseResult(resultPtr);

            return ret;
        }

        /// <summary>
        /// Detects and decodes a QR code from raw pixel data synchronously.
        /// </summary>
        /// <param name="pixels">The raw pixel data.</param>
        /// <param name="format">The pixel format.</param>
        /// <param name="width">The image width.</param>
        /// <param name="height">The image height.</param>
        /// <param name="stride">The image row stride (bytes). If 0, it is automatically calculated based on width and format.</param>
        /// <returns>The detection results.</returns>
        public QrcodeResults DetectAndDecodeSync(byte[] pixels, PixelFormat format, int width, int height,
            int stride)
        {
            if (pixels == null || pixels.Length == 0)
            {
                return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
            }

            return DetectAndDecodeInternal(pixels, format, width, height, stride);
        }

        /// <summary>
        /// Detects and decodes a QR code from raw pixel data asynchronously.
        /// </summary>
        /// <param name="pixels">The raw pixel data.</param>
        /// <param name="format">The pixel format.</param>
        /// <param name="width">The image width.</param>
        /// <param name="height">The image height.</param>
        /// <param name="stride">The image row stride (bytes). If 0, it is automatically calculated based on width and format.</param>
        /// <returns>A task that represents the asynchronous operation. The task result contains the detection results.</returns>
        public async Task<QrcodeResults> DetectAndDecode(byte[] pixels, PixelFormat format, int width, int height,
            int stride)
        {
            await GlobalSemaphore.WaitAsync();
            try
            {
                return await Task.Run(() => DetectAndDecodeSync(pixels, format, width, height, stride));
            }
            finally
            {
                GlobalSemaphore.Release();
            }
        }

        private bool GetTextureData(Texture2D texture, out byte[] raw, out Color32[] colors,
            out int width, out int height, out PixelFormat format)
        {
            width = 0;
            height = 0;
            raw = null;
            colors = null;
            format = default;

            if (!texture)
            {
                return false;
            }

            if (!texture.isReadable)
            {
                return false;
            }

            width = texture.width;
            height = texture.height;
            if (FormatMap.TryGetValue(texture.format, out format))
            {
                raw = texture.GetRawTextureData();
                return true;
            }
            else
            {
                colors = texture.GetPixels32();
                format = PixelFormat.RGBA;
                return true;
            }
        }

        /// <summary>
        /// Detects and decodes a QR code from a Texture2D synchronously.
        /// <para>
        /// The texture must be readable.
        /// </para>
        /// <para>
        /// <b>Must be called from the main thread.</b>
        /// </para>
        /// </summary>
        /// <param name="texture">The Unity Texture2D.</param>
        /// <returns>The detection results.</returns>
        public QrcodeResults DetectAndDecodeSync(Texture2D texture)
        {
            if (GetTextureData(texture, out var raw, out var colors, out var width, out var height,
                    out var format))
            {
                if (raw != null)
                {
                    return DetectAndDecodeInternal(raw, format, width, height, 0, true);
                }

                if (colors != null)
                {
                    return DetectAndDecodeInternal(MemoryMarshal.AsBytes(colors.AsSpan()), format, width, height, 0, true);
                }
            }

            return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
        }

        /// <summary>
        /// Detects and decodes a QR code from a Texture2D asynchronously.
        /// <para>
        /// The texture must be readable.
        /// </para>
        /// <para>
        /// <b>Must be called from the main thread.</b>
        /// </para>
        /// </summary>
        /// <param name="texture">The Unity Texture2D.</param>
        /// <returns>A task that represents the asynchronous operation. The task result contains the detection results.</returns>
        public async Task<QrcodeResults> DetectAndDecode(Texture2D texture)
        {
            if (GetTextureData(texture, out var raw, out var colors, out var width, out var height,
                    out var format))
            {
                await GlobalSemaphore.WaitAsync();
                try
                {
                    return await Task.Run(() =>
                    {
                        if (raw != null)
                        {
                            return DetectAndDecodeInternal(raw, format, width, height, 0, true);
                        }

                        if (colors != null)
                        {
                            return DetectAndDecodeInternal(MemoryMarshal.AsBytes(colors.AsSpan()), format, width, height, 0, true);
                        }

                        return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
                    });
                }
                finally
                {
                    GlobalSemaphore.Release();
                }
            }

            return QrcodeResults.FromErrorCode(QrcodeResults.ErrorCode.ErrorInvalidArgument);
        }
    }
}
