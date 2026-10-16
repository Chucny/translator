// Shorty: return type first, then one letter per parameter; references and arrays are 'L'.
#include <cstdio>
#include <string>

#include "check.h"
#include "zb/jni_shorty.h"

int main() {
    CHECK(zb::shorty_from_signature("(IFFIFF)I") == std::string("IIFFIFF"));
    CHECK(zb::shorty_from_signature("(J)V") == std::string("VJ"));
    CHECK(zb::shorty_from_signature("(JLjava/lang/String;[Ljava/lang/Object;)D") == std::string("DJLL"));
    CHECK(zb::shorty_from_signature("()V") == std::string("V"));
    CHECK(zb::shorty_from_signature("([[I[Z)Lorg/haxe/lime/HaxeObject;") == std::string("LLL"));
    CHECK(zb::shorty_from_signature("(ZBCS)C") == std::string("CZBCS"));
    CHECK(zb::shorty_from_signature("()[I") == std::string("L"));
    CHECK(zb::shorty_from_signature("()J") == std::string("J"));
    CHECK(zb::shorty_from_signature("(D)D") == std::string("DD"));
    CHECK(zb::shorty_from_signature("(Ljava/lang/String;[[Ljava/lang/Object;)Ljava/lang/String;") ==
          std::string("LLL"));
    {
        const std::string sig255 = "(" + std::string(255, '[') + "I)V";
        CHECK(zb::shorty_from_signature(sig255) == std::string("VL"));
    }

    CHECK(!zb::shorty_from_signature("(V)V"));
    CHECK(!zb::shorty_from_signature("(I"));
    CHECK(!zb::shorty_from_signature("(Ljava/lang/String)V"));
    CHECK(!zb::shorty_from_signature("(L;)V"));
    CHECK(!zb::shorty_from_signature("(I)"));
    CHECK(!zb::shorty_from_signature("(I)VX"));
    CHECK(!zb::shorty_from_signature("I)V"));
    CHECK(!zb::shorty_from_signature("([)V"));
    CHECK(!zb::shorty_from_signature("(Lfoo)V;)V"));
    CHECK(!zb::shorty_from_signature("(Ljava/lang/String)IJ;)V"));
    CHECK(!zb::shorty_from_signature("()Lfoo)bar;"));
    CHECK(!zb::shorty_from_signature("(La[b;)V"));
    CHECK(!zb::shorty_from_signature("(La.b;)V"));
    CHECK(!zb::shorty_from_signature("(L/a;)V"));
    CHECK(!zb::shorty_from_signature("(La/;)V"));
    CHECK(!zb::shorty_from_signature("(La//b;)V"));
    CHECK(!zb::shorty_from_signature("()[V"));
    CHECK(!zb::shorty_from_signature("(I)L;"));
    CHECK(!zb::shorty_from_signature(""));
    {
        const std::string sig256 = "(" + std::string(256, '[') + "I)V";
        CHECK(!zb::shorty_from_signature(sig256));
    }

    std::printf("jni_shorty_test ok\n");
    return 0;
}
