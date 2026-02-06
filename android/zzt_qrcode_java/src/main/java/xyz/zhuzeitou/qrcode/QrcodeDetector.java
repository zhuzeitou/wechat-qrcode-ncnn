package xyz.zhuzeitou.qrcode;

import android.graphics.Bitmap;
import android.util.Log;

import java.nio.ByteBuffer;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * QR Code Detector.
 * <p>
 * This class provides methods to detect and decode QR codes from image files or raw pixel data.
 * It wraps the native C++ implementation.
 * <p>
 * Implements {@link AutoCloseable} to ensure the native detector is released.
 */
public class QrcodeDetector implements AutoCloseable {
    /**
     * Pixel format definition.
     */
    public enum PixelFormat {
        /**
         * Single channel Gray
         */
        GRAY,
        /**
         * 3 channels RGB
         */
        RGB,
        /**
         * 3 channels BGR
         */
        BGR,
        /**
         * 4 channels RGBA
         */
        RGBA,
        /**
         * 4 channels BGRA
         */
        BGRA,
        /**
         * 4 channels ARGB
         */
        ARGB,
        /**
         * 4 channels ABGR
         */
        ABGR,
    }

    /**
     * Callback interface for asynchronous detection.
     */
    public interface QrcodeCallback {
        /**
         * Called when detection is complete.
         *
         * @param results The detection results.
         */
        void onResult(QrcodeResults results);
    }

    private long nativeDetector;
    private static final ExecutorService threadPool = Executors.newFixedThreadPool(Math.max(1, Runtime.getRuntime().availableProcessors() - 1));

    /**
     * Creates a new QrcodeDetector instance.
     * <p>
     * Initializes the native detector.
     */
    public QrcodeDetector() {
        nativeDetector = NativeLib.createDetector();
    }

    /**
     * Releases the native detector resources.
     * <p>
     * Should be called when the detector is no longer needed to prevent memory leaks.
     */
    @Override
    public void close() {
        if (nativeDetector != 0) {
            NativeLib.releaseDetector(nativeDetector);
            nativeDetector = 0;
        }
    }

    /**
     * Detects and decodes a QR code from an image file path synchronously.
     *
     * @param path The path to the image file.
     * @return The detection results.
     */
    public QrcodeResults detectQRCodeSync(String path) {
        Log.i("zzt", "detectQRCode path");
        if (path == null) {
            return QrcodeResults.fromError(QrcodeResults.QrcodeErrorCode.ERROR_INVALID_ARGUMENT);
        }

        long result = 0;
        int error = Integer.MIN_VALUE;

        try {
            result = NativeLib.detectAndDecodePath(nativeDetector, path);
            error = NativeLib.getLastError();
        } catch (Exception e) {
            e.printStackTrace();
        }

        QrcodeResults qrCodeResults = QrcodeResults.parseResult(result, error);
        NativeLib.releaseResult(result);
        return qrCodeResults;
    }

    /**
     * Detects and decodes a QR code from an image file path asynchronously.
     *
     * @param path     The path to the image file.
     * @param callback The callback to receive the results.
     */
    public void detectQRCode(String path, QrcodeCallback callback) {
        threadPool.execute(() -> {
            QrcodeResults results = detectQRCodeSync(path);
            if (callback != null) {
                callback.onResult(results);
            }
        });
    }

    /**
     * Detects and decodes a QR code from image file data (byte array) synchronously.
     *
     * @param image The image file data (e.g., JPEG or PNG bytes).
     * @return The detection results.
     */
    public QrcodeResults detectQRCodeSync(byte[] image) {
        Log.i("zzt", "detectQRCode data");
        if (image == null) {
            return QrcodeResults.fromError(QrcodeResults.QrcodeErrorCode.ERROR_INVALID_ARGUMENT);
        }

        long result = 0;
        int error = Integer.MIN_VALUE;

        try {
            result = NativeLib.detectAndDecodeData(nativeDetector, image);
            error = NativeLib.getLastError();
        } catch (Exception e) {
            e.printStackTrace();
        }

        QrcodeResults qrCodeResults = QrcodeResults.parseResult(result, error);
        NativeLib.releaseResult(result);
        return qrCodeResults;
    }

    /**
     * Detects and decodes a QR code from image file data (byte array) asynchronously.
     *
     * @param image    The image file data (e.g., JPEG or PNG bytes).
     * @param callback The callback to receive the results.
     */
    public void detectQRCode(byte[] image, QrcodeCallback callback) {
        threadPool.execute(() -> {
            QrcodeResults results = detectQRCodeSync(image);
            if (callback != null) {
                callback.onResult(results);
            }
        });
    }

    /**
     * Detects and decodes a QR code from an Android Bitmap synchronously.
     *
     * @param image The Android Bitmap image.
     * @return The detection results.
     */
    public QrcodeResults detectQRCodeSync(Bitmap image) {
        Log.i("zzt", "detectQRCode bitmap");
        if (image == null) {
            return QrcodeResults.fromError(QrcodeResults.QrcodeErrorCode.ERROR_INVALID_ARGUMENT);
        }
        long result = 0;
        int error = Integer.MIN_VALUE;

        try {
            Bitmap.Config config = image.getConfig();
            switch (config) {
                case ALPHA_8:
                case ARGB_8888: {
                    Log.i("zzt", "detectQRCode bitmap read raw");
                    byte[] pixels = new byte[image.getByteCount()];
                    image.copyPixelsToBuffer(ByteBuffer.wrap(pixels));
                    PixelFormat format = config == Bitmap.Config.ALPHA_8 ? PixelFormat.GRAY : PixelFormat.RGBA;
                    Log.i("zzt", "detectQRCode bitmap read done");
                    result = NativeLib.detectAndDecodePixels(nativeDetector, pixels, format.ordinal(), image.getWidth(), image.getHeight(), 0);
                    error = NativeLib.getLastError();
                    Log.i("zzt", "detectQRCode bitmap detect done");
                    break;
                }
                default: {
                    Log.i("zzt", "detectQRCode bitmap read colors");
                    int[] pixels = new int[image.getWidth() * image.getHeight()];
                    image.getPixels(pixels, 0, image.getWidth(), 0, 0, image.getWidth(), image.getHeight());
                    PixelFormat format = PixelFormat.ARGB;
                    Log.i("zzt", "detectQRCode bitmap read done");
                    result = NativeLib.detectAndDecodePixels(nativeDetector, pixels, format.ordinal(), image.getWidth(), image.getHeight(), 0);
                    error = NativeLib.getLastError();
                    Log.i("zzt", "detectQRCode bitmap detect done");
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
        }

        QrcodeResults qrCodeResults = QrcodeResults.parseResult(result, error);
        NativeLib.releaseResult(result);
        return qrCodeResults;
    }

    /**
     * Detects and decodes a QR code from an Android Bitmap asynchronously.
     *
     * @param image    The Android Bitmap image.
     * @param callback The callback to receive the results.
     */
    public void detectQRCode(Bitmap image, QrcodeCallback callback) {
        threadPool.execute(() -> {
            QrcodeResults results = detectQRCodeSync(image);
            if (callback != null) {
                callback.onResult(results);
            }
        });
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
    public QrcodeResults detectQRCodeSync(byte[] pixels, PixelFormat format, int width, int height) {
        Log.i("zzt", "detectQRCode pixels");
        if (pixels == null) {
            return QrcodeResults.fromError(QrcodeResults.QrcodeErrorCode.ERROR_INVALID_ARGUMENT);
        }
        long result = 0;
        int error = Integer.MIN_VALUE;

        try {
            result = NativeLib.detectAndDecodePixels(nativeDetector, pixels, format.ordinal(), width, height, 0);
            error = NativeLib.getLastError();
        } catch (Exception e) {
            e.printStackTrace();
        }

        QrcodeResults qrCodeResults = QrcodeResults.parseResult(result, error);
        NativeLib.releaseResult(result);
        return qrCodeResults;
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
    public void detectQRCode(byte[] pixels, PixelFormat format, int width, int height, QrcodeCallback callback) {
        threadPool.execute(() -> {
            QrcodeResults results = detectQRCodeSync(pixels, format, width, height);
            if (callback != null) {
                callback.onResult(results);
            }
        });
    }
}
