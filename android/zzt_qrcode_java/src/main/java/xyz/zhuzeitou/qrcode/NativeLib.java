package xyz.zhuzeitou.qrcode;

import java.util.concurrent.CopyOnWriteArrayList;

public class NativeLib {

    private static final CopyOnWriteArrayList<QrcodeLogCallback> logCallbacks = new CopyOnWriteArrayList<>();

    static {
        System.loadLibrary("zzt_qrcode_jni");
    }

    /**
     * Called from JNI to dispatch native log messages to all registered callbacks.
     * This method is intentionally package-private — it is the only JNI-facing bridge
     * for log dispatch. By default, no callbacks are registered, so all native log
     * output is silently suppressed.
     */
    static void dispatchLog(int level, String message) {
        for (QrcodeLogCallback callback : logCallbacks) {
            try {
                callback.onLog(level, message);
            } catch (Throwable ignored) {
                // Swallow listener exceptions — logging must never affect QR behavior.
            }
        }
    }

    /**
     * Registers a log callback. Native log messages will be dispatched to all
     * registered callbacks in the order they were added.
     *
     * @param callback the callback to register (null is silently ignored).
     */
    public static void addLogCallback(QrcodeLogCallback callback) {
        if (callback != null) {
            logCallbacks.addIfAbsent(callback);
        }
    }

    /**
     * Removes a previously registered log callback.
     *
     * @param callback the callback to remove.
     */
    public static void removeLogCallback(QrcodeLogCallback callback) {
        logCallbacks.remove(callback);
    }

    /**
     * Removes all registered log callbacks, restoring the default silent behavior.
     */
    public static void clearLogCallbacks() {
        logCallbacks.clear();
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
