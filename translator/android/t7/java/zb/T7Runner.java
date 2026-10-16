package zb;

import java.nio.ByteBuffer;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Consumer;

/** Runs inside the test plugin class loader. */
public final class T7Runner {
    private T7Runner() {}

    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void probe(String name, int line, Consumer<String> report) {
        check(line == 0, name + " failed at guest probe line " + line);
        report.accept("PASS " + name);
    }

    private static void registeredNatives(Consumer<String> report) {
        check(Natives.add(1, 0.5f, 0.25f, 3, 1.5f, -2.0f) == -29, "registered add");
        Natives.wide(-0x0102030405060708L);
        check(Natives.wide == -0x0102030405060708L, "registered wide");
        check(Natives.nest(3) == 1033, "nested Java/guest calls");
        check("art!".equals(new Natives().echo("art")), "registered object return");
        check(Natives.tid() > 0, "registered tid");
        report.accept("PASS RegisterNatives from JNI_OnLoad + nested calls");
    }

    private static void concurrentCallers(Consumer<String> report) throws Exception {
        CountDownLatch start = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        int[] tids = new int[2];
        Thread[] threads = new Thread[2];
        for (int index = 0; index < threads.length; ++index) {
            final int slot = index;
            threads[index] = new Thread(() -> {
                try {
                    start.await();
                    Natives instance = new Natives();
                    for (int i = 0; i < 50; ++i) {
                        check(Natives.add(slot, 1.0f, 0.5f, i, 0.25f, 0.125f)
                                == slot + 2 * i + 16, "concurrent add");
                        check(Natives.nest(2 + slot) == 1000 + 11 * (2 + slot), "concurrent nest");
                        check(("t" + slot + "!").equals(instance.echo("t" + slot)), "concurrent echo");
                        int tid = Natives.tid();
                        check(tid > 0 && (tids[slot] == 0 || tids[slot] == tid), "stable carrier tid");
                        tids[slot] = tid;
                    }
                } catch (Throwable t) {
                    failure.compareAndSet(null, t);
                }
            }, "t7-java-" + index);
            threads[index].start();
        }
        start.countDown();
        for (Thread thread : threads) thread.join();
        if (failure.get() != null) throw new AssertionError("concurrent caller", failure.get());
        check(tids[0] != tids[1], "Java callers must use distinct guest tids");
        report.accept("PASS two concurrent Java callers");
    }

    public static String run(Consumer<String> report) throws Exception {
        probe("Call* methods and all value types", T7.calls("text"), report);
        probe("fields", T7.fields("field"), report);
        probe("strings", T7.strings(), report);
        probe("arrays and release modes", T7.arrays(), report);
        probe("global/weak/local refs and monitors", T7.references(), report);
        probe("direct buffers", T7.directBuffers(ByteBuffer.allocateDirect(16)), report);
        try {
            T7.exceptions();
            throw new AssertionError("guest exception did not reach Java");
        } catch (IllegalStateException expected) {
            check("boom".equals(expected.getMessage()), "exception message");
        }
        report.accept("PASS exceptions guest -> Java -> guest -> Java");
        registeredNatives(report);
        probe("JavaVM GetEnv", T7.vm(), report);
        probe("guest pthread attach/call/detach", T7.attach(), report);
        concurrentCallers(report);
        report.accept("T7 PASS");
        return "T7 PASS";
    }
}
