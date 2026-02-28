using System;
using System.Runtime.InteropServices;

namespace ZZT.QRCode.Native
{
    internal class Bridge
    {
        private const string DLLName = "zzt_qrcode";

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
    }
}