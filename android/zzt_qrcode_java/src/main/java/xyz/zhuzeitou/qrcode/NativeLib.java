package xyz.zhuzeitou.qrcode;

public class NativeLib {

    static {
        System.loadLibrary("zzt_qrcode_jni");
    }

    public static native long createDetector();

    public static native void releaseDetector(long nativeDetector);

    public static native long detectAndDecodePath(long nativeDetector, String path);

    public static native long detectAndDecodeData(long nativeDetector, byte[] data);

    public static native long detectAndDecodePixels(long nativeDetector, byte[] pixels, int format, int width, int height, int stride);

    public static native long detectAndDecodePixels(long nativeDetector, int[] pixels, int format, int width, int height, int stride);

    public static native void releaseResult(long nativeResult);

    public static native int getResultSize(long nativeResult);

    public static native String getResultText(long nativeResult, int index);

    public static native float[][] getResultPoints(long nativeResult, int index);

    public static native int getLastError();
}