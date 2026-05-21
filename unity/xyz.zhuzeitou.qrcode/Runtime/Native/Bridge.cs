using System;
using System.Runtime.InteropServices;
using System.Text;

namespace ZZT.QRCode.Native
{
    internal class Bridge
    {
#if UNITY_IOS && !UNITY_EDITOR
        private const string DLLName = "__Internal";
#else
        private const string DLLName = "zzt_qrcode";
#endif

        internal struct NativeDetector
        {
            private IntPtr _nativePtr;

            public NativeDetector(IntPtr nativePtr)
            {
                _nativePtr = nativePtr;
            }

            public static NativeDetector Null => new() { _nativePtr = IntPtr.Zero };
        }

        internal struct NativeResult
        {
            private IntPtr _nativePtr;

            public NativeResult(IntPtr nativePtr)
            {
                _nativePtr = nativePtr;
            }
        }

        [DllImport(DLLName, EntryPoint = "zzt_qrcode_create_detector")]
        internal static extern NativeDetector CreateDetector();

        [DllImport(DLLName, EntryPoint = "zzt_qrcode_release_detector")]
        internal static extern int ReleaseDetector(NativeDetector detector);

        [DllImport(DLLName, EntryPoint = "zzt_qrcode_detect_and_decode_data")]
        internal static extern int DetectAndDecode(NativeDetector detector, in byte data, int dataLen,
            out NativeResult resultPtr);

        [DllImport(DLLName, EntryPoint = "zzt_qrcode_detect_and_decode_path_u16")]
        internal static extern int DetectAndDecode(NativeDetector detector,
            [MarshalAs(UnmanagedType.LPWStr)] string path, out NativeResult result);

        [DllImport(DLLName, EntryPoint = "zzt_qrcode_detect_and_decode_pixels")]
        internal static extern int DetectAndDecode(NativeDetector detector, in byte pixels, int format, int width,
            int height, int stride, out NativeResult result);

        [DllImport(DLLName, EntryPoint = "zzt_qrcode_release_result")]
        internal static extern int ReleaseResult(NativeResult result);

        [DllImport(DLLName, EntryPoint = "zzt_qrcode_get_result_size")]
        internal static extern int GetResultSize(NativeResult result, ref int size);

        [DllImport(DLLName, EntryPoint = "zzt_qrcode_get_result_text")]
        internal static extern int GetResultText(NativeResult result, int index, [Out] byte[] buf, ref int bufLen);

        [DllImport(DLLName, EntryPoint = "zzt_qrcode_get_result_points")]
        internal static extern int GetResultPoints(NativeResult result, int index, [Out] float[] pts, ref int ptsLen);

        // ─── Log callback support ────────────────────────────────────────────────
        //
        // The native C API (zzt_qrcode_set_log_callback) accepts one process-wide
        // callback.  We root a single P/Invoke delegate in a static field so the GC
        // never collects it, then forward all calls to a managed event that users
        // subscribe to via QrcodeDetector.OnLogMessage.
        //
        // Callbacks may arrive from any native thread.  The message is copied out
        // of the IntPtr immediately — native pointers are never retained.

        /// <summary>
        /// P/Invoke delegate matching <c>zzt_qrcode_log_callback_t</c>.
        /// Uses <c>int</c> for the level parameter to avoid coupling to the public enum.
        /// </summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void NativeLogCallback(int level, IntPtr message);

        /// <summary>
        /// Rooted delegate instance — <b>must</b> be a static field to prevent GC
        /// collection while native code holds the pointer.
        /// </summary>
        private static readonly NativeLogCallback _nativeLogCallback = OnNativeLogCallback;
        private static readonly object LogCallbackLock = new();
        private static bool _nativeLogCallbackInstalled;

        /// <summary>
        /// Internal event that receives despatched native log messages.
        /// QrcodeDetector.OnLogMessage bridges this to the public API.
        /// </summary>
        internal static event Action<int, string> LogMessageReceived;

        [DllImport(DLLName, EntryPoint = "zzt_qrcode_set_log_callback", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SetLogCallback(NativeLogCallback callback);

        /// <summary>
        /// Ensure the native callback is installed.  Safe to call multiple times.
        /// </summary>
        internal static void EnsureLogCallbackInstalled()
        {
            lock (LogCallbackLock)
            {
                if (_nativeLogCallbackInstalled) return;
                if (SetLogCallback(_nativeLogCallback) == 0)
                {
                    _nativeLogCallbackInstalled = true;
                }
            }
        }

        /// <summary>
        /// Clear the native callback.  Subsequent native log events are discarded.
        /// </summary>
        internal static void ClearLogCallback()
        {
            lock (LogCallbackLock)
            {
                if (!_nativeLogCallbackInstalled) return;
                if (SetLogCallback(null) == 0)
                {
                    _nativeLogCallbackInstalled = false;
                }
            }
        }

        /// <summary>
        /// Native callback — called from unmanaged code (any thread).
        /// Copies the UTF-8 message immediately and dispatches to managed subscribers.
        /// All exceptions are swallowed so logging never disrupts QR detection.
        /// </summary>
#if (UNITY_IOS || ENABLE_IL2CPP) && !UNITY_EDITOR
        [AOT.MonoPInvokeCallback(typeof(NativeLogCallback))]
#endif
        private static void OnNativeLogCallback(int level, IntPtr message)
        {
            string msgStr = PtrToUTF8(message);

            try
            {
                LogMessageReceived?.Invoke(level, msgStr);
            }
            catch
            {
                // Swallow — logging must never affect native QR processing.
            }
        }

        /// <summary>
        /// Convert a null-terminated UTF-8 IntPtr to a managed string.
        /// Returns <c>null</c> for a null pointer and <see cref="string.Empty"/> for
        /// a zero-length string.
        /// </summary>
        private static string PtrToUTF8(IntPtr ptr)
        {
            if (ptr == IntPtr.Zero) return null;
            int len = 0;
            while (Marshal.ReadByte(ptr, len) != 0) len++;
            if (len == 0) return string.Empty;
            byte[] buf = new byte[len];
            Marshal.Copy(ptr, buf, 0, len);
            return Encoding.UTF8.GetString(buf);
        }
    }
}
