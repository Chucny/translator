package zb;

import java.nio.ByteBuffer;

/** Statically named Java_* exports bound before the guest JNI_OnLoad runs. */
public final class T7 {
    static {
        System.loadLibrary("zbt7probe");
    }

    private T7() {}

    public static native int calls(String text);
    public static native int fields(String text);
    public static native int exceptions();
    public static native int strings();
    public static native int arrays();
    public static native int references();
    public static native int directBuffers(ByteBuffer foreign);
    public static native int vm();
    public static native int attach();
}
