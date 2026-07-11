package xyz.zhuzeitou.qrcode;

import android.os.Handler;
import android.os.Looper;

import java.util.concurrent.CopyOnWriteArrayList;

public class NativeLib {
    private static final Object logLock = new Object();
    private static final CopyOnWriteArrayList<QrcodeLogCallback> logCallbacks = new CopyOnWriteArrayList<>();
    private static final CopyOnWriteArrayList<QrcodeLogCallback> mainThreadLogCallbacks = new CopyOnWriteArrayList<>();
    private static final Handler mainHandler = new Handler(Looper.getMainLooper());
    private static final ThreadLocal<Boolean> dispatchingLog = new ThreadLocal<>();
    private static boolean desiredLogEnabled;
    private static boolean actualLogEnabled;
    private static int desiredLogLevel = 3;
    private static int actualLogLevel = 3;
    private static long desiredLogGeneration;
    private static long appliedLogGeneration;
    private static boolean reconcilePosted;
    private static boolean reconcilingLogSink;
    private static int lastLogError;

    static {
        System.loadLibrary("zzt_qrcode_jni");
    }

    static void dispatchLog(int level, String message) {
        LogLevel logLevel = LogLevel.fromNativeValue(level);
        if (logLevel == null) return;

        dispatchingLog.set(Boolean.TRUE);
        try {
            for (QrcodeLogCallback callback : logCallbacks) {
                try {
                    callback.onLog(logLevel, message);
                } catch (Throwable ignored) {
                    // Logging must not alter native QR operation.
                }
            }
            if (!mainThreadLogCallbacks.isEmpty()) {
                Object[] snapshot = mainThreadLogCallbacks.toArray();
                Runnable task = () -> {
                    for (Object object : snapshot) {
                        try {
                            ((QrcodeLogCallback) object).onLog(logLevel, message);
                        } catch (Throwable ignored) {
                            // Logging must not alter native QR operation.
                        }
                    }
                };
                if (Looper.myLooper() == Looper.getMainLooper()) task.run();
                else mainHandler.post(task);
            }
        } finally {
            dispatchingLog.remove();
        }
    }

    private static boolean listenersPresentLocked() {
        return !logCallbacks.isEmpty() || !mainThreadLogCallbacks.isEmpty();
    }

    private static void changedListeners() {
        synchronized (logLock) {
            boolean enabled = listenersPresentLocked();
            if (enabled != desiredLogEnabled) {
                desiredLogEnabled = enabled;
                ++desiredLogGeneration;
            }
        }
        scheduleReconcileIfNeeded();
    }

    private static void changedLevel(int level) {
        synchronized (logLock) {
            if (level != desiredLogLevel) {
                desiredLogLevel = level;
                ++desiredLogGeneration;
            }
        }
        scheduleReconcileIfNeeded();
    }

    private static void scheduleReconcileIfNeeded() {
        if (Boolean.TRUE.equals(dispatchingLog.get())) {
            synchronized (logLock) {
                if (!reconcilePosted) {
                    reconcilePosted = true;
                    if (!mainHandler.post(NativeLib::reconcileLogSink)) reconcilePosted = false;
                }
            }
            return;
        }
        reconcileLogSink();
    }
    private static void reconcileLogSink() {
        boolean interrupted = false;
        synchronized (logLock) {
            reconcilePosted = false;
            while (reconcilingLogSink) {
                try {
                    logLock.wait();
                } catch (InterruptedException ignored) {
                    interrupted = true;
                }
            }
            if (desiredLogLevel >= 0 && desiredLogLevel <= 4 &&
                    actualLogEnabled == desiredLogEnabled &&
                    (!actualLogEnabled || actualLogLevel == desiredLogLevel)) {
                return;
            }
            reconcilingLogSink = true;
        }
        try {
            while (true) {
                final boolean enabled;
                final int level;
                final long generation;
                synchronized (logLock) {
                    enabled = desiredLogEnabled;
                    level = desiredLogLevel;
                    generation = desiredLogGeneration;
                }
                int error = configureLogSink(enabled, level);
                synchronized (logLock) {
                    lastLogError = error;
                    if (generation == desiredLogGeneration && error == 0) {
                        actualLogEnabled = enabled;
                        actualLogLevel = level;
                        appliedLogGeneration = generation;
                    }
                    if (generation == desiredLogGeneration || error != 0) break;
                }
            }
        } finally {
            synchronized (logLock) {
                reconcilingLogSink = false;
                logLock.notifyAll();
            }
            if (interrupted) Thread.currentThread().interrupt();
        }
    }

    static void addLogCallback(QrcodeLogCallback callback) {
        if (callback != null) {
            logCallbacks.addIfAbsent(callback);
            changedListeners();
        }
    }
    static void removeLogCallback(QrcodeLogCallback callback) {
        logCallbacks.remove(callback);
        changedListeners();
    }
    static void clearLogCallbacks() {
        logCallbacks.clear();
        changedListeners();
    }
    static void addMainThreadLogCallback(QrcodeLogCallback callback) {
        if (callback != null) {
            mainThreadLogCallbacks.addIfAbsent(callback);
            changedListeners();
        }
    }
    static void removeMainThreadLogCallback(QrcodeLogCallback callback) {
        mainThreadLogCallbacks.remove(callback);
        changedListeners();
    }
    static void clearMainThreadLogCallbacks() {
        mainThreadLogCallbacks.clear();
        changedListeners();
    }

    static int setLogLevel(LogLevel minLevel) {
        changedLevel(minLevel.nativeValue());
        synchronized (logLock) {
            return lastLogError;
        }
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
    private static native int configureLogSink(boolean enabled, int minLevel);
}
