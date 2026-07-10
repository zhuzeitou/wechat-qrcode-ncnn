package xyz.zhuzeitou.qrcode;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

@RunWith(AndroidJUnit4.class)
public class ExampleInstrumentedTest {
    private static final int WARN = 3;
    private static final long TIMEOUT_SECONDS = 10;

    @After
    public void clearLogListeners() {
        NativeLib.clearLogCallbacks();
        NativeLib.clearMainThreadLogCallbacks();
    }

    @Test
    public void directListenerReceivesInvalidHandleSynchronouslyAndDisables() {
        AtomicInteger callbacks = new AtomicInteger();
        QrcodeLogCallback callback = (level, message) -> {
            assertEquals(WARN, level);
            callbacks.incrementAndGet();
        };

        assertEquals(0, NativeLib.setLogLevel(WARN));
        NativeLib.addLogCallback(callback);
        NativeLib.detectAndDecodePixels(0L, new byte[]{0}, 0, 1, 1, 1);
        assertEquals(1, callbacks.get());

        NativeLib.removeLogCallback(callback);
        NativeLib.detectAndDecodePixels(0L, new byte[]{0}, 0, 1, 1, 1);
        assertEquals(1, callbacks.get());
    }

    @Test
    public void reentrantRemovalDoesNotDeadlockQuiescentDisable() throws Exception {
        CountDownLatch callbackEntered = new CountDownLatch(1);
        CountDownLatch permitReentrantRemoval = new CountDownLatch(1);
        CountDownLatch reentrantRemovalReturned = new CountDownLatch(1);
        CountDownLatch removalStarted = new CountDownLatch(1);
        CountDownLatch emitterFinished = new CountDownLatch(1);
        CountDownLatch removerFinished = new CountDownLatch(1);
        AtomicInteger callbacks = new AtomicInteger();
        AtomicReference<QrcodeLogCallback> callbackRef = new AtomicReference<>();

        QrcodeLogCallback callback = (level, message) -> {
            callbacks.incrementAndGet();
            callbackEntered.countDown();
            try {
                assertTrue(permitReentrantRemoval.await(TIMEOUT_SECONDS, TimeUnit.SECONDS));
                NativeLib.removeLogCallback(callbackRef.get());
                reentrantRemovalReturned.countDown();
            } catch (InterruptedException exception) {
                Thread.currentThread().interrupt();
                throw new AssertionError(exception);
            }
        };
        callbackRef.set(callback);

        assertEquals(0, NativeLib.setLogLevel(WARN));
        NativeLib.addLogCallback(callback);
        Thread emitter = new Thread(() -> {
            NativeLib.detectAndDecodePixels(0L, new byte[]{0}, 0, 1, 1, 1);
            emitterFinished.countDown();
        });
        emitter.start();
        assertTrue(callbackEntered.await(TIMEOUT_SECONDS, TimeUnit.SECONDS));

        Thread remover = new Thread(() -> {
            removalStarted.countDown();
            NativeLib.removeLogCallback(callback);
            removerFinished.countDown();
        });
        remover.start();
        assertTrue(removalStarted.await(TIMEOUT_SECONDS, TimeUnit.SECONDS));

        permitReentrantRemoval.countDown();
        assertTrue(reentrantRemovalReturned.await(TIMEOUT_SECONDS, TimeUnit.SECONDS));
        assertTrue(emitterFinished.await(TIMEOUT_SECONDS, TimeUnit.SECONDS));
        assertTrue(removerFinished.await(TIMEOUT_SECONDS, TimeUnit.SECONDS));
        assertEquals(1, callbacks.get());

        NativeLib.detectAndDecodePixels(0L, new byte[]{0}, 0, 1, 1, 1);
        assertEquals(1, callbacks.get());
    }
}
