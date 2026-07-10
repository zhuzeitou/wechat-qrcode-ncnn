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

        [StructLayout(LayoutKind.Sequential)]
        internal struct NativeLogEvent
        {
            internal uint struct_size;
            internal int level;
            internal int error_code;
            internal ulong detector_id;
            internal ulong result_id;
            internal IntPtr operation;
            internal uint operation_len;
            internal IntPtr message;
            internal uint message_len;
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void NativeLogCallback(IntPtr eventPtr, IntPtr userData);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void NativeLogUserDataDestroy(IntPtr userData);

        [StructLayout(LayoutKind.Sequential)]
        internal struct NativeLogSinkOptions
        {
            internal uint struct_size;
            [MarshalAs(UnmanagedType.FunctionPtr)]
            internal NativeLogCallback callback;
            internal IntPtr user_data;
            [MarshalAs(UnmanagedType.FunctionPtr)]
            internal NativeLogUserDataDestroy destroy_user_data;
            internal int min_level;
        }

        // Native keeps both delegates after registration. These fields therefore remain
        // rooted for the complete managed process lifetime.
        private static readonly NativeLogCallback NativeLogCallbackRoot = OnNativeLogCallback;
        private static readonly NativeLogUserDataDestroy NativeLogDestroyRoot = OnNativeLogDestroy;

        internal static event Action<int, string> LogMessageReceived;

        [DllImport(DLLName, EntryPoint = "zzt_qrcode_add_runtime_log_sink",
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int AddRuntimeLogSink(ref NativeLogSinkOptions options, out ulong sinkId);

        [DllImport(DLLName, EntryPoint = "zzt_qrcode_remove_runtime_log_sink",
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RemoveRuntimeLogSink(ulong sinkId);

        [DllImport(DLLName, EntryPoint = "zzt_qrcode_set_runtime_log_sink_level",
            CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SetRuntimeLogSinkLevel(ulong sinkId, int minLevel);

        internal static NativeLogSinkOptions CreateLogSinkOptions(IntPtr userData, int minLevel)
        {
            return new NativeLogSinkOptions
            {
                struct_size = (uint)Marshal.SizeOf<NativeLogSinkOptions>(),
                callback = NativeLogCallbackRoot,
                user_data = userData,
                destroy_user_data = NativeLogDestroyRoot,
                min_level = minLevel
            };
        }

#if (UNITY_IOS || ENABLE_IL2CPP) && !UNITY_EDITOR
        [AOT.MonoPInvokeCallback(typeof(NativeLogCallback))]
#endif
        private static void OnNativeLogCallback(IntPtr eventPtr, IntPtr userData)
        {
            bool entered = false;
            try
            {
                QrcodeDetector.EnterNativeLogCallback();
                entered = true;
                if (eventPtr == IntPtr.Zero) return;
                NativeLogEvent nativeEvent = Marshal.PtrToStructure<NativeLogEvent>(eventPtr);
                string message = PtrToUTF8(nativeEvent.message, nativeEvent.message_len);
                LogMessageReceived?.Invoke(nativeEvent.level, message);
            }
            catch
            {
                // No managed exception may cross a reverse P/Invoke boundary.
            }
            finally
            {
                if (entered)
                {
                    try
                    {
                        QrcodeDetector.ExitNativeLogCallback();
                    }
                    catch
                    {
                        // Callback bookkeeping must not leak across the ABI boundary.
                    }
                }
            }
        }

#if (UNITY_IOS || ENABLE_IL2CPP) && !UNITY_EDITOR
        [AOT.MonoPInvokeCallback(typeof(NativeLogUserDataDestroy))]
#endif
        private static void OnNativeLogDestroy(IntPtr userData)
        {
            try
            {
                QrcodeDetector.OnNativeLogSinkDestroyed(userData);
            }
            catch
            {
                // Native teardown must complete even if managed cleanup fails.
            }
            finally
            {
                try
                {
                    QrcodeDetector.FinalizeNativeLogSinkLifetime(userData);
                }
                catch
                {
                    // A malformed stale pointer must not cross into native code.
                }
            }
        }

        private static string PtrToUTF8(IntPtr ptr, uint length)
        {
            if (ptr == IntPtr.Zero) return null;
            if (length == 0) return string.Empty;
            if (length > int.MaxValue) return string.Empty;
            byte[] buffer = new byte[(int)length];
            Marshal.Copy(ptr, buffer, 0, buffer.Length);
            return Encoding.UTF8.GetString(buffer);
        }
    }
}
