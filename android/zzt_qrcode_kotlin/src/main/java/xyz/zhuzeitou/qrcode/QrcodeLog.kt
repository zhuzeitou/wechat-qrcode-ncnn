package xyz.zhuzeitou.qrcode

/**
 * Callback interface for QR code library log messages.
 *
 * Register an instance via [QrcodeLog.add] to receive native log messages.
 *
 * Implementations **must be thread-safe**: the callback may be invoked from
 * arbitrary native (JNI) threads that are not the main thread.
 */
fun interface QrcodeLogCallback {
    /**
     * Called when the native QR code library emits a log message.
     *
     * @param level   Log level constant (0=VERBOSE, 1=DEBUG, 2=INFO,
     *                3=WARN, 4=ERROR).
     * @param message Log message text.
     */
    fun onLog(level: Int, message: String)
}

/**
 * Manages log callbacks for the QR code library.
 *
 * By default all logging is **silent**. Install a [QrcodeLogCallback] via [add]
 * to receive native log messages and forward them to your preferred logging
 * framework.
 *
 * ## Thread safety
 *
 * Listener dispatch is safe under concurrent modification — a snapshot of the
 * current listener list is taken before iteration (backed by
 * [CopyOnWriteArrayList][java.util.concurrent.CopyOnWriteArrayList]).
 * Exceptions thrown by individual listeners are silently swallowed so that
 * logging never affects QR detection and decoding behaviour.
 */
object QrcodeLog {
    private val listeners = java.util.concurrent.CopyOnWriteArrayList<QrcodeLogCallback>()

    /**
     * Registers a log callback. Does nothing if [callback] is already registered.
     */
    fun add(callback: QrcodeLogCallback) {
        listeners.addIfAbsent(callback)
    }

    /**
     * Unregisters a previously registered log callback.
     *
     * @param callback The callback previously passed to [add].
     */
    fun remove(callback: QrcodeLogCallback) {
        listeners.remove(callback)
    }

    /**
     * Removes all registered log callbacks, restoring silent operation.
     */
    fun clear() {
        listeners.clear()
    }

    /**
     * Dispatches a log message to all registered callbacks.
     *
     * This is the internal entry point called from [NativeLib.dispatchLog].
     * The listener list is snapshot before iteration. Exceptions from individual
     * callbacks are silently discarded.
     */
    internal fun dispatch(level: Int, message: String) {
        for (cb in listeners) {
            try {
                cb.onLog(level, message)
            } catch (_: Throwable) {
                // Swallow — logging must never affect QR behaviour.
            }
        }
    }
}
