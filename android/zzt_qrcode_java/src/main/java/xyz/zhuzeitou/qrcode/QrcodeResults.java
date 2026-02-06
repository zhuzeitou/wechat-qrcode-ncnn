package xyz.zhuzeitou.qrcode;

import android.util.Log;

import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

/**
 * QR Code detection results.
 * <p>
 * Contains the list of detected QR codes and the error code if any.
 */
public class QrcodeResults {
    /**
     * Point coordinates of the QR code position.
     */
    public static class ResultPoint {
        /** X coordinate */
        public float x;
        /** Y coordinate */
        public float y;

        @Override
        public String toString() {
            return "ResultPoint{" + "x=" + x + ", y=" + y + '}';
        }
    }

    /**
     * Single QR Code result.
     */
    public static class QrcodeResult {
        /** Decoded text content */
        public final String text;
        /** Corner points of the QR code */
        public final ResultPoint[] resultPoints;

        QrcodeResult(String text, ResultPoint[] resultPoints) {
            this.text = text;
            this.resultPoints = resultPoints;
        }

        @Override
        public String toString() {
            return "QRCodeResult{" + "text='" + text + '\'' + ", resultPoints=" + Arrays.toString(resultPoints) + '}';
        }
    }

    /**
     * Error codes for QR code detection.
     */
    public enum QrcodeErrorCode {
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
        ERROR_UNKNOWN(Integer.MIN_VALUE);

        private final int value;

        private static final Map<Integer, QrcodeErrorCode> VALUE_MAP;

        static {
            Map<Integer, QrcodeErrorCode> map = new HashMap<>();
            for (QrcodeErrorCode code : values()) {
                if (code != ERROR_UNKNOWN) {  // UNKNOWN 不放入 map
                    map.put(code.value, code);
                }
            }
            VALUE_MAP = Collections.unmodifiableMap(map);
        }

        QrcodeErrorCode(int value) {
            this.value = value;
        }

        public int getValue() {
            return value;
        }

        private static QrcodeErrorCode fromValue(int value) {
            return VALUE_MAP.getOrDefault(value, ERROR_UNKNOWN);
        }
    }

    /** Array of detected QR codes. Null if an error occurred. */
    public final QrcodeResult[] qrcodeResults;
    /** Error code of the operation. */
    public final QrcodeErrorCode errorCode;

    private QrcodeResults(QrcodeResult[] qrcodeResults, QrcodeErrorCode errorCode) {
        this.qrcodeResults = qrcodeResults;
        this.errorCode = errorCode;
    }

    @Override
    public String toString() {
        return "QrcodeResults{" + "qrcodeResults=" + Arrays.toString(qrcodeResults) + ", errorCode=" + errorCode + '}';
    }

    static QrcodeResults parseResult(long result, int error) {
        try {
            if (error != QrcodeErrorCode.OK.getValue()) {
                return fromError(QrcodeErrorCode.fromValue(error));
            }

            int size = NativeLib.getResultSize(result);
            error = NativeLib.getLastError();
            if (error != QrcodeErrorCode.OK.getValue()) {
                return fromError(QrcodeErrorCode.fromValue(error));
            }

            Log.i("zzt", "detectQRCode result size=" + size);
            QrcodeResult[] results = new QrcodeResult[size];
            for (int i = 0; i < size; ++i) {
                String text = NativeLib.getResultText(result, i);
                error = NativeLib.getLastError();
                if (error != QrcodeErrorCode.OK.getValue()) {
                    return fromError(QrcodeErrorCode.fromValue(error));
                }

                float[][] points = NativeLib.getResultPoints(result, i);
                error = NativeLib.getLastError();
                if (error != QrcodeErrorCode.OK.getValue()) {
                    return fromError(QrcodeErrorCode.fromValue(error));
                }

                ResultPoint[] resultPoints = null;
                if (points != null) {
                    resultPoints = new ResultPoint[points.length];
                    for (int j = 0; j < points.length; ++j) {
                        float[] point = points[j];
                        ResultPoint resultPoint = new ResultPoint();
                        if (point != null && point.length > 0) {
                            resultPoint.x = point[0];
                        }
                        if (point != null && point.length > 1) {
                            resultPoint.y = point[1];
                        }
                        resultPoints[j] = resultPoint;
                    }
                } else {
                    resultPoints = new ResultPoint[0];
                }
                QrcodeResult qrCodeResult = new QrcodeResult(text, resultPoints);
                results[i] = qrCodeResult;
            }

            return fromResults(results);
        } catch (Exception e) {
            e.printStackTrace();
            return fromError(QrcodeErrorCode.ERROR_UNKNOWN);
        }
    }

    static QrcodeResults fromResults(QrcodeResult[] results) {
        return new QrcodeResults(results, QrcodeErrorCode.OK);
    }

    static QrcodeResults fromError(QrcodeErrorCode errorCode) {
        return new QrcodeResults(null, errorCode);
    }
}
