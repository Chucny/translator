package com.chucny.translator.t7;

// Declared-native discovery fixture for JniEnvBackend::find_declared_natives on real ART.
// Expected (name, descriptor, static):
//   primitive   (ZBCSIJFD)I                              instance
//   reference   (Ljava/lang/String;Ljava/lang/Object;)Ljava/lang/String;  instance
//   arrays      ([B[[Ljava/lang/String;)[[I              instance
//   overloaded  (I)I                                     instance
//   overloaded  (Ljava/lang/String;[D)J                  static
//   nothing     ()V                                      static
//   Nested.inner ([C)Lcom/chucny/translator/t7/ReflectionSmoke$Nested;  instance
// notNative must never be reported.
public final class ReflectionSmoke {
    public native int primitive(boolean z, byte b, char c, short s, int i, long j, float f, double d);
    public native String reference(String value, Object other);
    public native int[][] arrays(byte[] bytes, String[][] names);
    public native int overloaded(int value);
    public static native long overloaded(String value, double[] numbers);
    static native void nothing();

    public int notNative(int value) {
        return value;
    }

    public static final class Nested {
        native Nested inner(char[] chars);
    }
}
