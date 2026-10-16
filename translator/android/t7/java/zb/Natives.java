package zb;

/** Methods registered by the guest library from JNI_OnLoad. */
public final class Natives {
    public static long wide;

    public static native int add(int a, float b, float c, int d, float e, float f);
    public static native void wide(long value);
    public static native int nest(int depth);
    public native String echo(String text);
    public static native int tid();

    public static int callback(int depth) {
        return nest(depth) + 10;
    }
}
