package xyz.zhuzeitou.qrcode;

import android.os.Handler;
import android.os.Looper;

import java.util.concurrent.CopyOnWriteArrayList;

public class NativeLib {

    private static final CopyOnWriteArrayList<QrcodeLogCallback> logCallbacks = new CopyOnWriteArrayList<>();
    private static final CopyOnWriteArrayList<QrcodeLogCallback> mainThreadLogCallbacks = new CopyOnWriteArrayList<>();
    private static final Handler mainHandler = new Handler(Looper.getMainLooper());

    static {
        System.loadLibrary("zzt_qrcode_jni");
    }

    /**
     * Called from JNI to dispatch native log messages to all registered callbacks.
     * This method is intentionally package-private — it is the only JNI-facing bridge
     * for log dispatch. By default, no callbacks are registered, so all native log
     * output is silently suppressed.
     * <p>
     * Direct callbacks added via {@link #addLogCallback(QrcodeLogCallback)} are
     * invoked synchronously on the calling (JNI/native) thread.
     * Main-thread callbacks added via {@link #addMainThreadLogCallback(QrcodeLogCallback)}
     * are dispatched to the Android main {@link Looper} via a snapshot-based mechanism
     * (see main-thread API documentation for details).
     */
    static void dispatchLog(int level, String message) {
        // --- dispatch to direct (arbitrary-thread) callbacks ---
        for (QrcodeLogCallback callback : logCallbacks) {
            try {
                callback.onLog(level, message);
            } catch (Throwable ignored) {
                // Swallow listener exceptions — logging must never affect QR behavior.
            }
        }

        // --- dispatch to main-thread callbacks ---
        if (!mainThreadLogCallbacks.isEmpty()) {
            // Snapshot the listener list so queued Runnables don't see future mutations
            Object[] snapshot = mainThreadLogCallbacks.toArray();
            Runnable task = () -> {
                for (Object obj : snapshot) {
                    QrcodeLogCallback cb = (QrcodeLogCallback) obj;
                    try {
                        cb.onLog(level, message);
                    } catch (Throwable ignored) {
                        // Swallow — logging must never affect QR behaviour.
                    }
                }
            };

            if (Looper.myLooper() == Looper.getMainLooper()) {
                // Already on main Looper — invoke inline
                task.run();
            } else {
                // Post to main thread; silently drop if the Handler returns false
                if (!mainHandler.post(task)) {
                    // Handler.post returned false — silently discard this log.
                }
            }
        }
    }

    /**
     * Registers a log callback that is invoked on the calling thread (which may be
     * an arbitrary native/JNI thread). Native log messages will be dispatched to all
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
     * Removes a previously registered direct log callback.
     *
     * @param callback the callback to remove.
     */
    public static void removeLogCallback(QrcodeLogCallback callback) {
        logCallbacks.remove(callback);
    }

    /**
     * Removes all registered direct log callbacks, restoring the default silent behavior.
     */
    public static void clearLogCallbacks() {
        logCallbacks.clear();
    }

    /**
     * Registers a log callback whose {@link QrcodeLogCallback#onLog(int, String)} method
     * will always be invoked on the Android main (UI) thread.
     * <p>
     * The callback list is snapshot at the moment
     * {@link #dispatchLog(int, String)} is called from JNI. The snapshot is then
     * posted to the main {@link Handler} via
     * {@link Handler#post(Runnable) Handler.post}. If the current thread
     * is already the main Looper thread, the snapshot is invoked synchronously
     * (inline).
     * <p>
     * <b>Important caveats:</b>
     * <ul>
     *   <li>Callback invocations are <b>asynchronous</b> — there is no ordering
     *       guarantee relative to direct callbacks.</li>
     *   <li>If {@link Handler#post(Runnable) Handler.post} returns
     *       {@code false} (indicating the Handler's message queue is shutting down)
     *       the log message for that dispatch cycle is <b>silently dropped</b>.</li>
     *   <li>If a callback is removed <b>after</b> a dispatch snapshot has been taken,
     *       it may still receive log messages from that snapshot.</li>
     *   <li>Exceptions thrown by the callback are silently swallowed so that logging
     *       never affects QR detection and decoding behaviour.</li>
     *   <li>This API is only meaningful on Android. On other platforms / in unit tests
     *       where no Android main Looper is available, the behaviour is undefined
     *       (the callbacks will never fire).</li>
     * </ul>
     *
     * @param callback the callback to register (null is silently ignored).
     */
    public static void addMainThreadLogCallback(QrcodeLogCallback callback) {
        if (callback != null) {
            mainThreadLogCallbacks.addIfAbsent(callback);
        }
    }

    /**
     * Removes a previously registered main-thread log callback.
     * <p>
     * Note: if the callback was already captured in a dispatch snapshot (taken at the
     * moment {@link #dispatchLog(int, String)} was called) it may still receive the
     * log messages from that snapshot even after removal.
     *
     * @param callback the callback to remove.
     */
    public static void removeMainThreadLogCallback(QrcodeLogCallback callback) {
        mainThreadLogCallbacks.remove(callback);
    }

    /**
     * Removes all registered main-thread log callbacks.
     * <p>
     * Already-queued dispatch snapshots (Runnables posted to the main Handler) may
     * still deliver log messages to previously registered callbacks.
     */
    public static void clearMainThreadLogCallbacks() {
        mainThreadLogCallbacks.clear();
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
    
    /**
     * Sets the minimum log level for the QR code library.
     * Native log messages below this level will be filtered out before dispatch.
     * <p>
     * This is a process-wide setting. The default level is 3 (WARN).
     * Valid values are:
     * <ul>
     *   <li>0: VERBOSE (enables performance diagnostics)</li>
     *   <li>1: DEBUG</li>
     *   <li>2: INFO</li>
     *   <li>3: WARN</li>
     *   <li>4: ERROR</li>
     * </ul>
     *
     * @param minLevel the minimum log level.
     * @return 0 on success (ZZT_QRCODE_OK), or a non-zero error code if the level is invalid
     *         (e.g., ZZT_QRCODE_ERROR_INVALID_ARGUMENT).
     */
    public static native int setLogLevel(int minLevel);
}
