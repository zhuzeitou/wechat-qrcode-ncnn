package xyz.zhuzeitou.qrcode

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference

@RunWith(AndroidJUnit4::class)
class ExampleInstrumentedTest {
    @After
    fun clearLogListeners() {
        QrcodeLog.clear()
        QrcodeLog.clearMainThread()
    }

    @Test
    fun directListenerReceivesInvalidHandleSynchronouslyAndDisables() {
        val callbacks = AtomicInteger()
        val callback = QrcodeLogCallback { level, _ ->
            assertEquals(WARN, level)
            callbacks.incrementAndGet()
        }

        assertEquals(0, QrcodeLog.setMinimumLevel(WARN))
        QrcodeLog.add(callback)
        NativeLib.detectAndDecodePixels(0L, byteArrayOf(0), 0, 1, 1, 1)
        assertEquals(1, callbacks.get())

        QrcodeLog.remove(callback)
        NativeLib.detectAndDecodePixels(0L, byteArrayOf(0), 0, 1, 1, 1)
        assertEquals(1, callbacks.get())
    }

    @Test
    fun reentrantRemovalDoesNotDeadlockQuiescentDisable() {
        val callbackEntered = CountDownLatch(1)
        val permitReentrantRemoval = CountDownLatch(1)
        val reentrantRemovalReturned = CountDownLatch(1)
        val removalStarted = CountDownLatch(1)
        val emitterFinished = CountDownLatch(1)
        val removerFinished = CountDownLatch(1)
        val callbacks = AtomicInteger()
        val callbackRef = AtomicReference<QrcodeLogCallback>()
        val callback = QrcodeLogCallback { _, _ ->
            callbacks.incrementAndGet()
            callbackEntered.countDown()
            try {
                assertTrue(permitReentrantRemoval.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
                QrcodeLog.remove(callbackRef.get())
                reentrantRemovalReturned.countDown()
            } catch (exception: InterruptedException) {
                Thread.currentThread().interrupt()
                throw AssertionError(exception)
            }
        }
        callbackRef.set(callback)

        assertEquals(0, QrcodeLog.setMinimumLevel(WARN))
        QrcodeLog.add(callback)
        val emitter = Thread {
            NativeLib.detectAndDecodePixels(0L, byteArrayOf(0), 0, 1, 1, 1)
            emitterFinished.countDown()
        }
        emitter.start()
        assertTrue(callbackEntered.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))

        val remover = Thread {
            removalStarted.countDown()
            QrcodeLog.remove(callback)
            removerFinished.countDown()
        }
        remover.start()
        assertTrue(removalStarted.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))

        permitReentrantRemoval.countDown()
        assertTrue(reentrantRemovalReturned.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
        assertTrue(emitterFinished.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
        assertTrue(removerFinished.await(TIMEOUT_SECONDS, TimeUnit.SECONDS))
        assertEquals(1, callbacks.get())

        NativeLib.detectAndDecodePixels(0L, byteArrayOf(0), 0, 1, 1, 1)
        assertEquals(1, callbacks.get())
    }

    private companion object {
        const val WARN = 3
        const val TIMEOUT_SECONDS = 10L
    }
}
