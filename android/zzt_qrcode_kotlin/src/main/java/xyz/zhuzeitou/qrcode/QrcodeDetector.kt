package xyz.zhuzeitou.qrcode

import android.graphics.Bitmap
import android.util.Log
import kotlinx.coroutines.suspendCancellableCoroutine
import java.nio.ByteBuffer
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import kotlin.coroutines.resume
import kotlin.math.max

/**
 * QR Code Detector.
 *
 * This class provides methods to detect and decode QR codes from image files or raw pixel data.
 * It wraps the native C++ implementation.
 *
 * Implements [AutoCloseable] to ensure the native detector is released.
 */
class QrcodeDetector : AutoCloseable {
    /**
     * Pixel format definition.
     */
    enum class PixelFormat {
        /** Single channel Gray */
        GRAY,

        /** 3 channels RGB */
        RGB,

        /** 3 channels BGR */
        BGR,

        /** 4 channels RGBA */
        RGBA,

        /** 4 channels BGRA */
        BGRA,

        /** 4 channels ARGB */
        ARGB,

        /** 4 channels ABGR */
        ABGR,
    }

    /**
     * Callback interface for asynchronous detection.
     */
    fun interface QrcodeCallback {
        /**
         * Called when detection is complete.
         *
         * @param results The detection results.
         */
        fun onResult(results: QrcodeResults)
    }

    private var nativeDetector: Long

    init {
        nativeDetector = NativeLib.createDetector()
    }

    /**
     * Releases the native detector resources.
     *
     * Should be called when the detector is no longer needed to prevent memory leaks.
     */
    override fun close() {
        if (nativeDetector != 0L) {
            NativeLib.releaseDetector(nativeDetector)
            nativeDetector = 0
        }
    }

    /**
     * Detects and decodes a QR code from an image file path synchronously.
     *
     * @param path The path to the image file.
     * @return The detection results.
     */
    fun detectQRCodeSync(path: String?): QrcodeResults {
        Log.i("zzt", "detectQRCode path")
        if (path == null) {
            return QrcodeResults.fromError(QrcodeResults.QrcodeErrorCode.ERROR_INVALID_ARGUMENT)
        }

        var result: Long = 0
        var error = Int.MIN_VALUE

        try {
            result = NativeLib.detectAndDecodePath(nativeDetector, path)
            error = NativeLib.getLastError()
        } catch (e: Exception) {
            e.printStackTrace()
        }

        val qrCodeResults = QrcodeResults.parseResult(result, error)
        NativeLib.releaseResult(result)
        return qrCodeResults
    }

    /**
     * Detects and decodes a QR code from an image file path asynchronously.
     *
     * @param path     The path to the image file.
     * @param callback The callback to receive the results.
     */
    fun detectQRCode(path: String?, callback: QrcodeCallback?) {
        threadPool.execute(Runnable {
            val results = detectQRCodeSync(path)
            callback?.onResult(results)
        })
    }

    /**
     * Detects and decodes a QR code from an image file path using coroutines.
     *
     * This suspend function runs the detection on the internal thread pool and supports
     * structured concurrency with cancellation.
     *
     * @param path The path to the image file.
     * @return The detection results.
     */
    suspend fun detectQRCode(path: String?): QrcodeResults = suspendCancellableCoroutine { cont ->
        val future = threadPool.submit {
            val results = detectQRCodeSync(path)
            if (cont.isActive) cont.resume(results)
        }
        cont.invokeOnCancellation { future.cancel(false) }
    }

    /**
     * Detects and decodes a QR code from image file data (byte array) synchronously.
     *
     * @param image The image file data (e.g., JPEG or PNG bytes).
     * @return The detection results.
     */
    fun detectQRCodeSync(image: ByteArray?): QrcodeResults {
        Log.i("zzt", "detectQRCode data")
        if (image == null) {
            return QrcodeResults.fromError(QrcodeResults.QrcodeErrorCode.ERROR_INVALID_ARGUMENT)
        }

        var result: Long = 0
        var error = Int.MIN_VALUE

        try {
            result = NativeLib.detectAndDecodeData(nativeDetector, image)
            error = NativeLib.getLastError()
        } catch (e: Exception) {
            e.printStackTrace()
        }

        val qrCodeResults = QrcodeResults.parseResult(result, error)
        NativeLib.releaseResult(result)
        return qrCodeResults
    }

    /**
     * Detects and decodes a QR code from image file data (byte array) asynchronously.
     *
     * @param image    The image file data (e.g., JPEG or PNG bytes).
     * @param callback The callback to receive the results.
     */
    fun detectQRCode(image: ByteArray?, callback: QrcodeCallback?) {
        threadPool.execute(Runnable {
            val results = detectQRCodeSync(image)
            callback?.onResult(results)
        })
    }

    /**
     * Detects and decodes a QR code from image file data (byte array) using coroutines.
     *
     * This suspend function runs the detection on the internal thread pool and supports
     * structured concurrency with cancellation.
     *
     * @param image The image file data (e.g., JPEG or PNG bytes).
     * @return The detection results.
     */
    suspend fun detectQRCode(image: ByteArray?): QrcodeResults =
        suspendCancellableCoroutine { cont ->
            val future = threadPool.submit {
                val results = detectQRCodeSync(image)
                if (cont.isActive) cont.resume(results)
            }
            cont.invokeOnCancellation { future.cancel(false) }
        }

    /**
     * Detects and decodes a QR code from an Android Bitmap synchronously.
     *
     * @param image The Android Bitmap image.
     * @return The detection results.
     */
    fun detectQRCodeSync(image: Bitmap?): QrcodeResults {
        Log.i("zzt", "detectQRCode bitmap")
        if (image == null) {
            return QrcodeResults.fromError(QrcodeResults.QrcodeErrorCode.ERROR_INVALID_ARGUMENT)
        }
        var result: Long = 0
        var error = Int.MIN_VALUE

        try {
            when (val config = image.getConfig()) {
                Bitmap.Config.ALPHA_8, Bitmap.Config.ARGB_8888 -> {
                    Log.i("zzt", "detectQRCode bitmap read raw")
                    val pixels = ByteArray(image.getByteCount())
                    image.copyPixelsToBuffer(ByteBuffer.wrap(pixels))
                    val format =
                        if (config == Bitmap.Config.ALPHA_8) PixelFormat.GRAY else PixelFormat.RGBA
                    Log.i("zzt", "detectQRCode bitmap read done")
                    result = NativeLib.detectAndDecodePixels(
                        nativeDetector,
                        pixels,
                        format.ordinal,
                        image.getWidth(),
                        image.getHeight(),
                        0
                    )
                    error = NativeLib.getLastError()
                    Log.i("zzt", "detectQRCode bitmap detect done")
                }

                else -> {
                    Log.i("zzt", "detectQRCode bitmap read colors")
                    val pixels = IntArray(image.getWidth() * image.getHeight())
                    image.getPixels(
                        pixels, 0, image.getWidth(), 0, 0, image.getWidth(), image.getHeight()
                    )
                    val format = PixelFormat.ARGB
                    Log.i("zzt", "detectQRCode bitmap read done")
                    result = NativeLib.detectAndDecodePixels(
                        nativeDetector,
                        pixels,
                        format.ordinal,
                        image.getWidth(),
                        image.getHeight(),
                        0
                    )
                    error = NativeLib.getLastError()
                    Log.i("zzt", "detectQRCode bitmap detect done")
                }

            }
        } catch (e: Exception) {
            e.printStackTrace()
        }

        val qrCodeResults = QrcodeResults.parseResult(result, error)
        NativeLib.releaseResult(result)
        return qrCodeResults
    }

    /**
     * Detects and decodes a QR code from an Android Bitmap asynchronously.
     *
     * @param image    The Android Bitmap image.
     * @param callback The callback to receive the results.
     */
    fun detectQRCode(image: Bitmap?, callback: QrcodeCallback?) {
        threadPool.execute(Runnable {
            val results = detectQRCodeSync(image)
            callback?.onResult(results)
        })
    }

    /**
     * Detects and decodes a QR code from an Android Bitmap using coroutines.
     *
     * This suspend function runs the detection on the internal thread pool and supports
     * structured concurrency with cancellation.
     *
     * @param image The Android Bitmap image.
     * @return The detection results.
     */
    suspend fun detectQRCode(image: Bitmap?): QrcodeResults = suspendCancellableCoroutine { cont ->
        val future = threadPool.submit {
            val results = detectQRCodeSync(image)
            if (cont.isActive) cont.resume(results)
        }
        cont.invokeOnCancellation { future.cancel(false) }
    }

    /**
     * Detects and decodes a QR code from raw pixel data synchronously.
     *
     * @param pixels The raw pixel data.
     * @param format The pixel format.
     * @param width  The image width.
     * @param height The image height.
     * @return The detection results.
     */
    fun detectQRCodeSync(
        pixels: ByteArray?, format: PixelFormat, width: Int, height: Int
    ): QrcodeResults {
        Log.i("zzt", "detectQRCode pixels")
        if (pixels == null) {
            return QrcodeResults.fromError(QrcodeResults.QrcodeErrorCode.ERROR_INVALID_ARGUMENT)
        }
        var result: Long = 0
        var error = Int.Companion.MIN_VALUE

        try {
            result = NativeLib.detectAndDecodePixels(
                nativeDetector, pixels, format.ordinal, width, height, 0
            )
            error = NativeLib.getLastError()
        } catch (e: Exception) {
            e.printStackTrace()
        }

        val qrCodeResults = QrcodeResults.parseResult(result, error)
        NativeLib.releaseResult(result)
        return qrCodeResults
    }

    /**
     * Detects and decodes a QR code from raw pixel data asynchronously.
     *
     * @param pixels   The raw pixel data.
     * @param format   The pixel format.
     * @param width    The image width.
     * @param height   The image height.
     * @param callback The callback to receive the results.
     */
    fun detectQRCode(
        pixels: ByteArray?, format: PixelFormat, width: Int, height: Int, callback: QrcodeCallback?
    ) {
        threadPool.execute(Runnable {
            val results = detectQRCodeSync(pixels, format, width, height)
            callback?.onResult(results)
        })
    }

    /**
     * Detects and decodes a QR code from raw pixel data using coroutines.
     *
     * This suspend function runs the detection on the internal thread pool and supports
     * structured concurrency with cancellation.
     *
     * @param pixels The raw pixel data.
     * @param format The pixel format.
     * @param width  The image width.
     * @param height The image height.
     * @return The detection results.
     */
    suspend fun detectQRCode(
        pixels: ByteArray?, format: PixelFormat, width: Int, height: Int
    ): QrcodeResults = suspendCancellableCoroutine { cont ->
        val future = threadPool.submit {
            val results = detectQRCodeSync(pixels, format, width, height)
            if (cont.isActive) cont.resume(results)
        }
        cont.invokeOnCancellation { future.cancel(false) }
    }

    companion object {
        private val threadPool: ExecutorService =
            Executors.newFixedThreadPool(max(1, Runtime.getRuntime().availableProcessors() - 1))
    }
}
