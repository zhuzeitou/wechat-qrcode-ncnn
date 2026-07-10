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

        // ─── Runtime log sink public API ────────────────────────────────────
        //
        // This facade owns one runtime sink only while managed listeners exist.
        // Its level is sink-local: it never changes another native consumer.

        public enum LogLevel
        {
            Verbose = 0,
            Debug = 1,
            Info = 2,
            Warn = 3,
            Error = 4,
        }

        public static QrcodeResults.ErrorCode SetMinimumLogLevel(LogLevel minLevel)
        {
            int numericLevel = (int)minLevel;
            if (numericLevel < (int)LogLevel.Verbose || numericLevel > (int)LogLevel.Error)
                return QrcodeResults.ErrorCode.ErrorInvalidArgument;

            lock (LogMessageLock)
            {
                _desiredLevel = numericLevel;
                ++_desiredLevelGeneration;
            }
            return (QrcodeResults.ErrorCode)ReconcileLogSink(false);
        }

        public delegate void LogMessageHandler(LogLevel level, string message);

        private enum CompletionOwner
        {
            Initiator,
            DestroyCallback
        }

        private sealed class NativeSinkLifetime
        {
            internal readonly ulong Generation;
            internal readonly ulong TransitionToken;
            internal GCHandle Handle;
            internal bool Destroyed;
            internal bool HandleFreed;

            internal NativeSinkLifetime(ulong generation, ulong transitionToken)
            {
                Generation = generation;
                TransitionToken = transitionToken;
            }
        }

        private static readonly object LogMessageLock = new();
        private static LogMessageHandler _directHandlers;
        private static LogMessageHandler _mainThreadHandlers;
        private static bool _bridgeLogSubscribed;
        private static bool _desiredEnabled;
        private static bool _actualEnabled;
        private static int _desiredLevel = (int)LogLevel.Warn;
        private static int _actualLevel = (int)LogLevel.Warn;
        private static ulong _sinkId;
        private static ulong _desiredEnabledGeneration;
        private static ulong _desiredLevelGeneration;
        private static ulong _transitionToken;
        private static ulong _lifetimeGeneration;
        private static ulong _appliedLevelGeneration;
        private static ulong _transitionDesiredEnabledGeneration;
        private static ulong _transitionDesiredLevelGeneration;
        private static ulong _transitionTargetId;
        private static int _transitionTargetLevel;
        private static bool _transitioning;
        private static bool _unloading;
        private static int _lastLogError;
        private static NativeSinkLifetime _lifetime;
        private static CompletionOwner _completionOwner;

        [ThreadStatic] private static int _nativeLogCallbackDepth;
        [ThreadStatic] private static bool _callbackNeedsReconcile;

        private static int _mainThreadId;
        private static SynchronizationContext _mainThreadContext;
        private static int _epoch;

        private static bool HasHandlersLocked() => _directHandlers != null || _mainThreadHandlers != null;

        private static void SubscribeBridgeIfNeeded()
        {
            if (_bridgeLogSubscribed) return;
            Bridge.LogMessageReceived += OnBridgeLogMessage;
            _bridgeLogSubscribed = true;
        }

        private static void UnsubscribeBridgeIfNoHandlers()
        {
            if (HasHandlersLocked()) return;
            if (_bridgeLogSubscribed)
            {
                Bridge.LogMessageReceived -= OnBridgeLogMessage;
                _bridgeLogSubscribed = false;
            }
        }

        private static void MutateEnabledForHandlersLocked()
        {
            bool enabled = !_unloading && HasHandlersLocked();
            if (_desiredEnabled == enabled) return;
            _desiredEnabled = enabled;
            ++_desiredEnabledGeneration;
        }

        public static event LogMessageHandler OnLogMessage
        {
            add
            {
                if (value == null) return;
                lock (LogMessageLock)
                {
                    _directHandlers += value;
                    SubscribeBridgeIfNeeded();
                    MutateEnabledForHandlersLocked();
                }
                RequestLogSinkReconcile();
            }
            remove
            {
                if (value == null) return;
                lock (LogMessageLock)
                {
                    _directHandlers -= value;
                    UnsubscribeBridgeIfNoHandlers();
                    MutateEnabledForHandlersLocked();
                }
                RequestLogSinkReconcile();
            }
        }

        public static event LogMessageHandler OnLogMessageMainThread
        {
            add
            {
                if (value == null) return;
                lock (LogMessageLock)
                {
                    _mainThreadHandlers += value;
                    SubscribeBridgeIfNeeded();
                    MutateEnabledForHandlersLocked();
                }
                RequestLogSinkReconcile();
            }
            remove
            {
                if (value == null) return;
                lock (LogMessageLock)
                {
                    _mainThreadHandlers -= value;
                    UnsubscribeBridgeIfNoHandlers();
                    MutateEnabledForHandlersLocked();
                }
                RequestLogSinkReconcile();
            }
        }

        private static void RequestLogSinkReconcile()
        {
            if (_nativeLogCallbackDepth != 0)
            {
                _callbackNeedsReconcile = true;
                return;
            }
            ReconcileLogSink(false);
        }

        // A single caller owns each native transition. Other external callers wait;
        // a callback only records new desired state, avoiding callback/remove cycles.
        private static int ReconcileLogSink(bool callbackOrigin)
        {
            NativeSinkLifetime lifetime = null;
            ulong token = 0;
            ulong sinkId = 0;
            int targetLevel = 0;
            int action = 0; // 1 add, 2 update, 3 remove

            lock (LogMessageLock)
            {
                while (_transitioning)
                {
                    if (_nativeLogCallbackDepth != 0) return _lastLogError;
                    Monitor.Wait(LogMessageLock);
                }

                if (_desiredEnabled && !_actualEnabled)
                {
                    action = 1;
                    targetLevel = _desiredLevel;
                    token = ++_transitionToken;
                    lifetime = new NativeSinkLifetime(++_lifetimeGeneration, token);
                    try
                    {
                        lifetime.Handle = GCHandle.Alloc(lifetime);
                    }
                    catch
                    {
                        _lastLogError = (int)QrcodeResults.ErrorCode.ErrorOutOfMemory;
                        return _lastLogError;
                    }
                }
                else if (_desiredEnabled && _actualEnabled && _desiredLevel != _actualLevel)
                {
                    action = 2;
                    token = ++_transitionToken;
                    sinkId = _sinkId;
                    targetLevel = _desiredLevel;
                }
                else if (!_desiredEnabled && _actualEnabled)
                {
                    action = 3;
                    token = ++_transitionToken;
                    sinkId = _sinkId;
                    lifetime = _lifetime;
                    _completionOwner = callbackOrigin
                        ? CompletionOwner.DestroyCallback
                        : CompletionOwner.Initiator;
                }
                else
                {
                    return _lastLogError;
                }

                _transitioning = true;
                if (action != 3) _completionOwner = CompletionOwner.Initiator;
                _transitionDesiredEnabledGeneration = _desiredEnabledGeneration;
                _transitionDesiredLevelGeneration = _desiredLevelGeneration;
                _transitionTargetId = sinkId;
                _transitionTargetLevel = targetLevel;
            }

            int error;
            if (action == 1)
            {
                IntPtr userData = GCHandle.ToIntPtr(lifetime.Handle);
                Bridge.NativeLogSinkOptions options = Bridge.CreateLogSinkOptions(userData, targetLevel);
                error = Bridge.AddRuntimeLogSink(ref options, out sinkId);
                if (error != 0)
                {
                    lifetime.Handle.Free();
                    lock (LogMessageLock)
                    {
                        if (_transitioning && token == _transitionToken)
                        {
                            _lastLogError = error;
                            _transitioning = false;
                            Monitor.PulseAll(LogMessageLock);
                        }
                    }
                    return error;
                }
            }
            else if (action == 2)
            {
                error = Bridge.SetRuntimeLogSinkLevel(sinkId, targetLevel);
            }
            else
            {
                error = Bridge.RemoveRuntimeLogSink(sinkId);
            }

            bool reconcileAgain;
            lock (LogMessageLock)
            {
                if (token != _transitionToken) return error;

                if (action == 1)
                {
                    if (error == 0)
                    {
                        _actualEnabled = true;
                        _actualLevel = targetLevel;
                        _appliedLevelGeneration = _transitionDesiredLevelGeneration;
                        _sinkId = sinkId;
                        _lifetime = lifetime;
                        _lastLogError = 0;
                    }
                    else
                    {
                        _lastLogError = error;
                    }
                    _transitioning = false;
                    Monitor.PulseAll(LogMessageLock);
                }
                else if (action == 2)
                {
                    if (error == 0)
                    {
                        _actualLevel = targetLevel;
                        _appliedLevelGeneration = _transitionDesiredLevelGeneration;
                        _lastLogError = 0;
                    }
                    else
                    {
                        _lastLogError = error;
                    }
                    _transitioning = false;
                    Monitor.PulseAll(LogMessageLock);
                }
                else if (_completionOwner == CompletionOwner.Initiator)
                {
                    if ((error == 0 ||
                         error == (int)QrcodeResults.ErrorCode.ErrorInvalidHandle) &&
                        lifetime != null && lifetime.Destroyed)
                    {
                        CommitDestroyedLocked(lifetime, token);
                    }
                    else
                    {
                        _lastLogError = error == 0 ||
                            error == (int)QrcodeResults.ErrorCode.ErrorInvalidHandle
                            ? (int)QrcodeResults.ErrorCode.ErrorInternal
                            : error;
                        _transitioning = false;
                        Monitor.PulseAll(LogMessageLock);
                    }
                }

                reconcileAgain = !_transitioning &&
                    ((_desiredEnabled && (!_actualEnabled || _desiredLevel != _actualLevel)) ||
                     (!_desiredEnabled && _actualEnabled));
            }

            if (reconcileAgain && error == 0) ReconcileLogSink(false);
            return error;
        }

        internal static void EnterNativeLogCallback()
        {
            ++_nativeLogCallbackDepth;
        }

        internal static void ExitNativeLogCallback()
        {
            if (--_nativeLogCallbackDepth != 0 || !_callbackNeedsReconcile) return;
            _callbackNeedsReconcile = false;
            ReconcileLogSink(true);
        }

        internal static void OnNativeLogSinkDestroyed(IntPtr userData)
        {
            NativeSinkLifetime lifetime = userData == IntPtr.Zero
                ? null
                : GCHandle.FromIntPtr(userData).Target as NativeSinkLifetime;
            if (lifetime == null) return;

            lock (LogMessageLock)
            {
                lifetime.Destroyed = true;
                if (_lifetime != lifetime) return;
                if (_completionOwner == CompletionOwner.DestroyCallback)
                    CommitDestroyedLocked(lifetime, _transitionToken);
            }
        }

        internal static void FinalizeNativeLogSinkLifetime(IntPtr userData)
        {
            if (userData == IntPtr.Zero) return;
            GCHandle handle = GCHandle.FromIntPtr(userData);
            NativeSinkLifetime lifetime = handle.Target as NativeSinkLifetime;
            if (lifetime == null || lifetime.HandleFreed) return;
            lifetime.HandleFreed = true;
            handle.Free();
        }

        private static void CommitDestroyedLocked(NativeSinkLifetime lifetime, ulong token)
        {
            if (token != _transitionToken || _lifetime != lifetime) return;
            _actualEnabled = false;
            _sinkId = 0;
            _lifetime = null;
            _lastLogError = 0;
            _transitioning = false;
            Monitor.PulseAll(LogMessageLock);
        }

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

            if (directSnapshot != null)
            {
                foreach (LogMessageHandler handler in directSnapshot.GetInvocationList())
                {
                    try { handler((LogLevel)level, message); }
                    catch { }
                }
            }

            if (mainSnapshot == null) return;
            if (Environment.CurrentManagedThreadId == _mainThreadId)
            {
                InvokeMainSnapshot(mainSnapshot, level, message);
                return;
            }
            if (_mainThreadContext == null) return;

            var capturedMessage = message;
            var capturedLevel = level;
            var capturedEpoch = epochAtCapture;
            var capturedSnapshot = mainSnapshot;
            _mainThreadContext.Post(_ =>
            {
                if (_epoch != capturedEpoch) return;
                InvokeMainSnapshot(capturedSnapshot, capturedLevel, capturedMessage);
            }, null);
        }

        private static void InvokeMainSnapshot(LogMessageHandler snapshot, int level, string message)
        {
            foreach (LogMessageHandler handler in snapshot.GetInvocationList())
            {
                try { handler((LogLevel)level, message); }
                catch { }
            }
        }

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        private static void OnSubsystemRegistration()
        {
            ClearLogCallbackState();
            lock (LogMessageLock)
            {
                _unloading = false;
            }
        }

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterAssembliesLoaded)]
        private static void AfterAssembliesLoaded()
        {
            _mainThreadId = Environment.CurrentManagedThreadId;
            _mainThreadContext = SynchronizationContext.Current;
            Application.quitting -= OnApplicationQuitting;
            Application.quitting += OnApplicationQuitting;
        }

        private static void OnApplicationQuitting()
        {
            ClearLogCallbackState();
        }

        private static void ClearLogCallbackState()
        {
            lock (LogMessageLock)
            {
                _unloading = true;
                _directHandlers = null;
                _mainThreadHandlers = null;
                UnsubscribeBridgeIfNoHandlers();
                MutateEnabledForHandlersLocked();
            }
            ReconcileLogSink(false);
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
