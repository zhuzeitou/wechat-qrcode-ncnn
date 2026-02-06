package xyz.zhuzeitou.qrcode

import android.util.Log

internal object NativeLib {
    init {
        Log.i("NativeLib", "init")
        System.loadLibrary("zzt_qrcode_jni")
    }

    @JvmStatic
    external fun createDetector(): Long

    @JvmStatic
    external fun releaseDetector(nativeDetector: Long)

    @JvmStatic
    external fun detectAndDecodePath(nativeDetector: Long, path: String): Long

    @JvmStatic
    external fun detectAndDecodeData(nativeDetector: Long, data: ByteArray): Long

    @JvmStatic
    external fun detectAndDecodePixels(
        nativeDetector: Long, pixels: ByteArray, format: Int, width: Int, height: Int, stride: Int
    ): Long

    @JvmStatic
    external fun detectAndDecodePixels(
        nativeDetector: Long, pixels: IntArray, format: Int, width: Int, height: Int, stride: Int
    ): Long

    @JvmStatic
    external fun releaseResult(nativeResult: Long)

    @JvmStatic
    external fun getResultSize(nativeResult: Long): Int

    @JvmStatic
    external fun getResultText(nativeResult: Long, index: Int): String?

    @JvmStatic
    external fun getResultPoints(nativeResult: Long, index: Int): Array<FloatArray>?

    @JvmStatic
    external fun getLastError(): Int
}