package xyz.zhuzeitou.qrcode

import android.util.Log

/**
 * QR Code detection results.
 *
 * Contains the list of detected QR codes and the error code if any.
 *
 * @property qrcodeResults Array of detected QR codes. Null if an error occurred.
 * @property errorCode Error code of the operation.
 */
class QrcodeResults private constructor(
    val qrcodeResults: Array<QrcodeResult>?, val errorCode: QrcodeErrorCode?
) {
    /**
     * Point coordinates of the QR code position.
     *
     * @property x X coordinate.
     * @property y Y coordinate.
     */
    class ResultPoint internal constructor(
        val x: Float = 0f, val y: Float = 0f
    ) {
        override fun toString(): String {
            return "ResultPoint{x=$x, y=$y}"
        }
    }

    /**
     * Single QR Code result.
     *
     * @property text Decoded text content.
     * @property resultPoints Corner points of the QR code.
     */
    class QrcodeResult internal constructor(
        val text: String, val resultPoints: Array<ResultPoint>
    ) {
        override fun toString(): String {
            return "QRCodeResult{text='$text', resultPoints=${resultPoints.contentToString()}}"
        }
    }

    /**
     * Error codes for QR code detection.
     */
    enum class QrcodeErrorCode(val value: Int) {
        /** Success */
        OK(0),
        /** Invalid handle */
        ERROR_INVALID_HANDLE(-1),
        /** Invalid index */
        ERROR_INVALID_INDEX(-2),
        /** Buffer too small */
        ERROR_BUFFER_TOO_SMALL(-3),
        /** Image decode failed */
        ERROR_DECODE_FAILED(-4),
        /** Invalid argument (e.g. null pointer or invalid size) */
        ERROR_INVALID_ARGUMENT(-5),
        /** Out of memory */
        ERROR_OUT_OF_MEMORY(-6),
        /** Unknown error */
        ERROR_UNKNOWN(Int.MIN_VALUE);

        companion object {
            private val VALUE_MAP: Map<Int, QrcodeErrorCode> =
                entries.filter { it != ERROR_UNKNOWN }.associateBy({ it.value }, { it })

            internal fun fromValue(value: Int): QrcodeErrorCode {
                return VALUE_MAP[value] ?: ERROR_UNKNOWN
            }
        }
    }

    override fun toString(): String {
        return "QrcodeResults{qrcodeResults=${qrcodeResults.contentToString()}, errorCode=$errorCode}"
    }

    companion object {
        @JvmStatic
        internal fun parseResult(result: Long, error: Int): QrcodeResults {
            var error = error
            try {
                if (error != QrcodeErrorCode.OK.value) {
                    return QrcodeResults(null, QrcodeErrorCode.fromValue(error))
                }

                val size = NativeLib.getResultSize(result)
                error = NativeLib.getLastError()
                if (error != QrcodeErrorCode.OK.value) {
                    return fromError(QrcodeErrorCode.fromValue(error))
                }
                val results = if (size == 0) {
                    emptyArray()
                } else {
                    Array(size) { i ->
                        val text = NativeLib.getResultText(result, i) ?: ""
                        error = NativeLib.getLastError()
                        if (error != QrcodeErrorCode.OK.value) {
                            return QrcodeResults(null, QrcodeErrorCode.fromValue(error))
                        }

                        val points: Array<FloatArray>? = NativeLib.getResultPoints(result, i)
                        error = NativeLib.getLastError()
                        if (error != QrcodeErrorCode.OK.value) {
                            return QrcodeResults(null, QrcodeErrorCode.fromValue(error))
                        }

                        val resultPoints = if (points != null) {
                            Array(points.size) { ResultPoint(points[it][0], points[it][1]) }
                        } else {
                            emptyArray()
                        }
                        val qrCodeResult = QrcodeResult(text, resultPoints)
                        qrCodeResult
                    }
                }

                return fromResult(results)
            } catch (e: Exception) {
                e.printStackTrace()
                return fromError(QrcodeErrorCode.ERROR_UNKNOWN)
            }
        }

        @JvmStatic
        internal fun fromResult(result: Array<QrcodeResult>): QrcodeResults {
            return QrcodeResults(result, QrcodeErrorCode.OK)
        }

        @JvmStatic
        internal fun fromError(errorCode: QrcodeErrorCode): QrcodeResults {
            return QrcodeResults(null, errorCode)
        }
    }
}
