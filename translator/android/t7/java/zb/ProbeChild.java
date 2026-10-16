package zb;

public final class ProbeChild extends Probe {
    @Override public boolean echoZ(boolean value) { return value; }
    @Override public byte echoB(byte value) { return (byte) (value + 2); }
    @Override public char echoC(char value) { return (char) (value + 2); }
    @Override public short echoS(short value) { return (short) (value + 2); }
    @Override public int echoI(int value) { return value + 2; }
    @Override public long echoJ(long value) { return value + 2; }
    @Override public float echoF(float value) { return value * 4; }
    @Override public double echoD(double value) { return value * 4; }
    @Override public String echoL(String value) { return null; }
    @Override public void echoV() { calls += 2; }
}
