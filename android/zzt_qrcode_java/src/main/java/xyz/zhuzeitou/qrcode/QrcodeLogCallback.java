package xyz.zhuzeitou.qrcode;

/**
 * Callback interface for receiving log messages from the native QR code library.
 * <p>
 * Register an instance via {@link QrcodeLog#add(QrcodeLogCallback)}
 * for arbitrary-thread dispatch, or via
 * {@link QrcodeLog#addMainThread(QrcodeLogCallback)} for guaranteed
 * main-thread dispatch.
 * <p>
 * By default, no callbacks are registered and all native log output is suppressed.
 * <p>
 * <b>Threading:</b>
 * <ul>
 *   <li>Callbacks registered with {@code QrcodeLog.add} are invoked synchronously
 *       on the calling (JNI/native) thread — the implementation must be thread-safe.</li>
 *   <li>Callbacks registered with {@code QrcodeLog.addMainThread} are invoked on
 *       the Android main (UI) thread via the main {@link android.os.Handler}.</li>
 * </ul>
 */
@FunctionalInterface
public interface QrcodeLogCallback {

    /**
     * Called when a log message is dispatched from the native library.
     *
     * @param level   The log severity.
     * @param message The log message content.
     */
    void onLog(LogLevel level, String message);
}
