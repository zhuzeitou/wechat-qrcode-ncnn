package xyz.zhuzeitou.qrcode

import android.os.Handler
import android.os.Looper

/**
 * Callback interface for QR code library log messages.
 *
 * Register an instance via [QrcodeLog.add] to receive native log messages
 * on the calling thread, or via [QrcodeLog.addMainThread] for guaranteed
 * main-thread dispatch.
 *
 * Implementations **must be thread-safe** when registered via [QrcodeLog.add]:
 * the callback may be invoked from arbitrary native (JNI) threads that are not
 * the main thread.
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
 *
 * ## Main-thread dispatch
 *
 * Callbacks registered via [addMainThread] are dispatched to the Android main
 * [Looper] via a snapshot-based mechanism (see [addMainThread] for details).
 */
object QrcodeLog {
    private val listeners = java.util.concurrent.CopyOnWriteArrayList<QrcodeLogCallback>()
    private val mainThreadListeners = java.util.concurrent.CopyOnWriteArrayList<QrcodeLogCallback>()
    @Suppress("DEPRECATION")
    private val mainHandler = Handler(Looper.getMainLooper())

    /**
     * Registers a log callback. Does nothing if [callback] is already registered.
     * The callback is invoked synchronously on the calling (JNI/native) thread.
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
     * Registers a log callback whose [QrcodeLogCallback.onLog] method will always
     * be invoked on the Android main (UI) thread via the main [Looper].
     *
     * ### Dispatch semantics
     *
     * 1. When [dispatch] is called from JNI, the main-thread listener list is
     *    **snapshot** via `toArray()`.
     * 2. If the current thread is already the main Looper thread, the snapshot is
     *    invoked **synchronously** (inline).
     * 3. Otherwise, a [Runnable] capturing the snapshot is posted to the main
     *    [Handler]. If [Handler.post] returns `false`, the log message for that
     *    dispatch cycle is silently dropped.
     *
     * ### Important caveats
     *
     * - Callback invocations are **asynchronous** relative to direct callbacks.
     * - If a callback is removed **after** a dispatch snapshot has been taken, it
     *   may still receive log messages from that snapshot.
     * - If [Handler.post] fails (e.g. during Looper shutdown) the message is
     *   silently dropped.
     * - Exceptions thrown by the callback are silently swallowed.
     * - This API is only meaningful on Android (requires a main Looper).
     */
    fun addMainThread(callback: QrcodeLogCallback) {
        mainThreadListeners.addIfAbsent(callback)
    }

    /**
     * Removes a previously registered main-thread log callback.
     *
     * Note: if the callback was already captured in a dispatch snapshot it may
     * still receive the log messages from that snapshot even after removal.
     */
    fun removeMainThread(callback: QrcodeLogCallback) {
        mainThreadListeners.remove(callback)
    }

    /**
     * Removes all registered main-thread log callbacks.
     *
     * Already-queued dispatch snapshots may still deliver log messages to
     * previously registered callbacks.
     */
    fun clearMainThread() {
        mainThreadListeners.clear()
    }

    /**
     * Dispatches a log message to all registered callbacks.
     *
     * This is the internal entry point called from [NativeLib.dispatchLog].
     * The listener list is snapshot before iteration. Exceptions from individual
     * callbacks are silently discarded.
     */
    internal fun dispatch(level: Int, message: String) {
        // --- dispatch to direct (arbitrary-thread) callbacks ---
        for (cb in listeners) {
            try {
                cb.onLog(level, message)
            } catch (_: Throwable) {
                // Swallow — logging must never affect QR behaviour.
            }
        }

        // --- dispatch to main-thread callbacks ---
        if (mainThreadListeners.isEmpty()) return
        val snapshot = mainThreadListeners.toArray()
        val task = Runnable {
            for (obj in snapshot) {
                val cb = obj as QrcodeLogCallback
                try {
                    cb.onLog(level, message)
                } catch (_: Throwable) {
                    // Swallow — logging must never affect QR behaviour.
                }
            }
        }
        if (Looper.myLooper() == Looper.getMainLooper()) {
            task.run()
        } else {
            if (!mainHandler.post(task)) {
                // Handler.post returned false — silently discard this log.
            }
        }
    }
}
