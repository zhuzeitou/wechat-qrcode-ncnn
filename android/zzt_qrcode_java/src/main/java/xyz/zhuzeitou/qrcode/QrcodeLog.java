package xyz.zhuzeitou.qrcode;

/** Library-wide runtime logging facade. */
public final class QrcodeLog {
    private QrcodeLog() {
    }

    public static void add(QrcodeLogCallback callback) {
        NativeLib.addLogCallback(callback);
    }

    public static void remove(QrcodeLogCallback callback) {
        NativeLib.removeLogCallback(callback);
    }

    public static void clear() {
        NativeLib.clearLogCallbacks();
    }

    public static void addMainThread(QrcodeLogCallback callback) {
        NativeLib.addMainThreadLogCallback(callback);
    }

    public static void removeMainThread(QrcodeLogCallback callback) {
        NativeLib.removeMainThreadLogCallback(callback);
    }

    public static void clearMainThread() {
        NativeLib.clearMainThreadLogCallbacks();
    }

    public static int setMinimumLevel(LogLevel minLevel) {
        return minLevel == null ? -5 : NativeLib.setLogLevel(minLevel);
    }
}
