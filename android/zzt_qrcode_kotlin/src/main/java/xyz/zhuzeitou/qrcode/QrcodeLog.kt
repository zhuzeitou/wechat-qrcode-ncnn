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
     * @param level   Log severity.
     * @param message Log message text.
     */
    fun onLog(level: LogLevel, message: String)
}

/**
 * Static Android logging facade.  Its native runtime sink is created only while
 * at least one direct or main-thread listener exists; the level is sink-local.
 */
object QrcodeLog {
    private val lock = java.lang.Object()
    private val listeners = java.util.concurrent.CopyOnWriteArrayList<QrcodeLogCallback>()
    private val mainThreadListeners = java.util.concurrent.CopyOnWriteArrayList<QrcodeLogCallback>()
    @Suppress("DEPRECATION")
    private val mainHandler = Handler(Looper.getMainLooper())
    private val dispatching = ThreadLocal<Boolean>()
    private var desiredEnabled = false
    private var actualEnabled = false
    private var desiredLevel = 3
    private var actualLevel = 3
    private var desiredGeneration = 0L
    private var appliedGeneration = 0L
    private var reconcilePosted = false
    private var reconciling = false
    private var lastError = 0

    fun add(callback: QrcodeLogCallback) {
        listeners.addIfAbsent(callback)
        listenersChanged()
    }
    fun remove(callback: QrcodeLogCallback) {
        listeners.remove(callback)
        listenersChanged()
    }
    fun clear() {
        listeners.clear()
        listenersChanged()
    }
    fun addMainThread(callback: QrcodeLogCallback) {
        mainThreadListeners.addIfAbsent(callback)
        listenersChanged()
    }
    fun removeMainThread(callback: QrcodeLogCallback) {
        mainThreadListeners.remove(callback)
        listenersChanged()
    }
    fun clearMainThread() {
        mainThreadListeners.clear()
        listenersChanged()
    }

    /** Sets this Android sink's minimum level, not a process-wide level. */
    fun setMinimumLevel(minLevel: LogLevel): Int {
        val nativeLevel = minLevel.nativeValue
        synchronized(lock) {
            if (desiredLevel != nativeLevel) {
                desiredLevel = nativeLevel
                ++desiredGeneration
            }
        }
        scheduleReconcile()
        return synchronized(lock) { lastError }
    }

    private fun listenersChanged() {
        synchronized(lock) {
            val enabled = listeners.isNotEmpty() || mainThreadListeners.isNotEmpty()
            if (enabled != desiredEnabled) {
                desiredEnabled = enabled
                ++desiredGeneration
            }
        }
        scheduleReconcile()
    }

    private fun scheduleReconcile() {
        if (dispatching.get() == true) {
            synchronized(lock) {
                if (!reconcilePosted) {
                    reconcilePosted = mainHandler.post { reconcile() }
                }
            }
        } else {
            reconcile()
        }
    }

    private fun reconcile() {
        var interrupted = false
        synchronized(lock) {
            reconcilePosted = false
            while (reconciling) {
                try {
                    lock.wait()
                } catch (_: InterruptedException) {
                    interrupted = true
                }
            }
            if (desiredLevel in 0..4 && actualEnabled == desiredEnabled &&
                (!actualEnabled || actualLevel == desiredLevel)) {
                return
            }
            reconciling = true
        }
        try {
            while (true) {
                val enabled: Boolean
                val level: Int
                val generation: Long
                synchronized(lock) {
                    enabled = desiredEnabled
                    level = desiredLevel
                    generation = desiredGeneration
                }
                val error = NativeLib.configureLogSinkFromFacade(enabled, level)
                synchronized(lock) {
                    lastError = error
                    if (error == 0 && generation == desiredGeneration) {
                        actualEnabled = enabled
                        actualLevel = level
                        appliedGeneration = generation
                    }
                    if (generation == desiredGeneration || error != 0) return
                }
            }
        } finally {
            synchronized(lock) {
                reconciling = false
                lock.notifyAll()
            }
            if (interrupted) Thread.currentThread().interrupt()
        }
    }

    internal fun dispatch(level: Int, message: String) {
        val logLevel = LogLevel.fromNativeValue(level) ?: return

        dispatching.set(true)
        try {
            for (cb in listeners) {
                try {
                    cb.onLog(logLevel, message)
                } catch (_: Throwable) {
                    // Logging must not alter native QR operation.
                }
            }
            if (mainThreadListeners.isNotEmpty()) {
                val snapshot = mainThreadListeners.toArray()
                val task = Runnable {
                    for (entry in snapshot) {
                        try {
                            (entry as QrcodeLogCallback).onLog(logLevel, message)
                        } catch (_: Throwable) {
                            // Logging must not alter native QR operation.
                        }
                    }
                }
                if (Looper.myLooper() == Looper.getMainLooper()) task.run()
                else mainHandler.post(task)
            }
        } finally {
            dispatching.remove()
        }
    }
}
