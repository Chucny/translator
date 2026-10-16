package zb;

/** Java side called by the arm32 T7 probe through the synthesized guest JNIEnv. */
public class Probe {
    public boolean z;
    public byte b;
    public char c;
    public short s;
    public int i;
    public long j;
    public float f;
    public double d;
    public String l;

    public static boolean sz;
    public static byte sb;
    public static char sc;
    public static short ss;
    public static int si;
    public static long sj;
    public static float sf;
    public static double sd;
    public static String sl;
    public static int calls;

    public Probe() {}

    public Probe(int value, String text) {
        i = value;
        l = text;
    }

    public boolean echoZ(boolean value) { return !value; }
    public byte echoB(byte value) { return (byte) (value + 1); }
    public char echoC(char value) { return (char) (value + 1); }
    public short echoS(short value) { return (short) (value + 1); }
    public int echoI(int value) { return value + 1; }
    public long echoJ(long value) { return value + 1; }
    public float echoF(float value) { return value * 2; }
    public double echoD(double value) { return value * 2; }
    public String echoL(String value) { return value; }
    public void echoV() { calls += 1; }

    public static boolean sechoZ(boolean value) { return !value; }
    public static byte sechoB(byte value) { return (byte) (value + 1); }
    public static char sechoC(char value) { return (char) (value + 1); }
    public static short sechoS(short value) { return (short) (value + 1); }
    public static int sechoI(int value) { return value + 1; }
    public static long sechoJ(long value) { return value + 1; }
    public static float sechoF(float value) { return value * 2; }
    public static double sechoD(double value) { return value * 2; }
    public static String sechoL(String value) { return value; }
    public static void sechoV() { calls += 4; }

    public static int mix(boolean z, byte b, char c, short s, int i, long j, float f, double d, String text) {
        return z && b == -2 && c == 0x1234 && s == -3 && i == 4 && j == 0x1122334455667788L
                && f == 1.5f && d == -2.25 && "text".equals(text) ? 42 : 0;
    }

    public static void fail() {
        throw new IllegalStateException("boom");
    }

    public static String threadName() {
        return Thread.currentThread().getName();
    }

    public static boolean threadDaemon() {
        return Thread.currentThread().isDaemon();
    }
}
