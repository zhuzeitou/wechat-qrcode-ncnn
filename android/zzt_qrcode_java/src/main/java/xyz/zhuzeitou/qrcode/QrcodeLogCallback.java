package xyz.zhuzeitou.qrcode;

/**
 * Callback interface for receiving log messages from the native QR code library.
 * <p>
 * Register an instance via {@link NativeLib#addLogCallback(QrcodeLogCallback)}.
 * By default, no callbacks are registered and all native log output is suppressed.
 * Callbacks may be invoked from arbitrary native/JNI threads.
 */
@FunctionalInterface
public interface QrcodeLogCallback {

    /**
     * Called when a log message is dispatched from the native library.
     *
     * @param level   The log level (0=VERBOSE, 1=DEBUG, 2=INFO, 3=WARN, 4=ERROR).
     * @param message The log message content.
     */
    void onLog(int level, String message);
}
