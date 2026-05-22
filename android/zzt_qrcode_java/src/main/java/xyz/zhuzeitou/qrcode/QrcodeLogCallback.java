package xyz.zhuzeitou.qrcode;

/**
 * Callback interface for receiving log messages from the native QR code library.
 * <p>
 * Register an instance via {@link NativeLib#addLogCallback(QrcodeLogCallback)}
 * for arbitrary-thread dispatch, or via
 * {@link NativeLib#addMainThreadLogCallback(QrcodeLogCallback)} for guaranteed
 * main-thread dispatch.
 * <p>
 * By default, no callbacks are registered and all native log output is suppressed.
 * <p>
 * <b>Threading:</b>
 * <ul>
 *   <li>Callbacks registered with {@code addLogCallback} are invoked synchronously
 *       on the calling (JNI/native) thread — the implementation must be thread-safe.</li>
 *   <li>Callbacks registered with {@code addMainThreadLogCallback} are invoked on
 *       the Android main (UI) thread via the main {@link android.os.Handler}.</li>
 * </ul>
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
