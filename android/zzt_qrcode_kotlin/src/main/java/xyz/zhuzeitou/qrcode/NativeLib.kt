package xyz.zhuzeitou.qrcode

internal object NativeLib {
    init {
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

    /**
     * Bridge method called by JNI ([native_log_callback]) to dispatch native
     * log messages to the managed [QrcodeLog] listener list.
     *
     * This is the **only** JNI-facing entry point for logging. Business callback
     * objects are never exposed to JNI.
     */
    @JvmStatic
    fun dispatchLog(level: Int, message: String) {
        QrcodeLog.dispatch(level, message)
    }
}
