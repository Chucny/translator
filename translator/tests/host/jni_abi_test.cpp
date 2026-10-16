// Host AAPCS64 JNI registers -> guest AAPCS32 softfp call layout, and guest results -> host.
#include <cstdio>
#include <cstring>
#include <functional>

#include <sys/wait.h>
#include <unistd.h>

#include "check.h"
#include "zb/native_call.h"

namespace {

std::uint64_t fbits(float f) {
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

std::uint64_t dbits(double d) {
    std::uint64_t b;
    std::memcpy(&b, &d, 8);
    return b;
}

// In this test a local handle is the host reference plus 0x1000.
std::uint32_t to_handle(std::uint64_t ref) {
    return ref == 0 ? 0 : static_cast<std::uint32_t>(ref + 0x1000);
}

zb::GuestCall marshal(const char* shorty, const zb::NativeRegs& regs) {
    return zb::marshal_native_args(shorty, regs, 0xE000, to_handle);
}

bool aborts(const std::function<void()>& function) {
    const pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        function();
        _exit(0);
    }
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

}  // namespace

int main() {
    std::uint64_t stack[8] = {};

    // Lime.onTouch(IFFIFF)I: I F in r2 r3, the rest on the guest stack.
    {
        zb::NativeRegs r{};
        r.x[1] = 0x77;
        r.x[2] = 3;
        r.x[3] = static_cast<std::uint64_t>(-2);
        r.d[0] = fbits(1.5f);
        r.d[1] = fbits(-2.25f);
        r.d[2] = fbits(4.0f);
        r.d[3] = fbits(8.5f);
        r.stack = stack;
        const zb::GuestCall c = marshal("IIFFIFF", r);
        CHECK(c.regs[0] == 0xE000 && c.regs[1] == 0x1077);
        CHECK(c.regs[2] == 3 && c.regs[3] == fbits(1.5f));
        CHECK(c.stack.size() == 4);
        CHECK(c.stack[0] == fbits(-2.25f) && c.stack[1] == 0xFFFFFFFEu);
        CHECK(c.stack[2] == fbits(4.0f) && c.stack[3] == fbits(8.5f));
    }

    // Lime.releaseReference(J)V: the long takes r2:r3, low word first.
    {
        zb::NativeRegs r{};
        r.x[1] = 1;
        r.x[2] = 0x1122334455667788ull;
        r.stack = stack;
        const zb::GuestCall c = marshal("VJ", r);
        CHECK(c.regs[2] == 0x55667788u && c.regs[3] == 0x11223344u && c.stack.empty());
    }

    // (IJ)V: the long cannot start at r3, so it goes on the stack and r3 stays unused.
    {
        zb::NativeRegs r{};
        r.x[1] = 1;
        r.x[2] = 9;
        r.x[3] = 0xAAAABBBBCCCCDDDDull;
        r.stack = stack;
        const zb::GuestCall c = marshal("VIJ", r);
        CHECK(c.regs[2] == 9 && c.regs[3] == 0);
        CHECK(c.stack.size() == 2 && c.stack[0] == 0xCCCCDDDDu && c.stack[1] == 0xAAAABBBBu);
    }

    // (JI)V: the long takes r2:r3, then the int goes to the stack.
    {
        zb::NativeRegs r{};
        r.x[1] = 1;
        r.x[2] = 0x1122334455667788ull;
        r.x[3] = 9;
        r.stack = stack;
        const zb::GuestCall c = marshal("VJI", r);
        CHECK(c.regs[2] == 0x55667788u && c.regs[3] == 0x11223344u);
        CHECK(c.stack.size() == 1 && c.stack[0] == 9);
    }

    // (DI)V has the same guest layout, while the host reads D from d0 and I from x2.
    {
        zb::NativeRegs r{};
        r.x[1] = 1;
        r.x[2] = 11;
        r.d[0] = dbits(3.25);
        r.stack = stack;
        const zb::GuestCall c = marshal("VDI", r);
        CHECK(c.regs[2] == static_cast<std::uint32_t>(dbits(3.25)));
        CHECK(c.regs[3] == static_cast<std::uint32_t>(dbits(3.25) >> 32));
        CHECK(c.stack.size() == 1 && c.stack[0] == 11);
    }

    // (IIIJ)V: the third int is stack word 0, so the long is padded to the 8-byte boundary.
    {
        zb::NativeRegs r{};
        r.x[1] = 1;
        r.x[2] = 1;
        r.x[3] = 2;
        r.x[4] = 3;
        r.x[5] = 0x0000000500000004ull;
        r.stack = stack;
        const zb::GuestCall c = marshal("VIIIJ", r);
        CHECK(c.regs[2] == 1 && c.regs[3] == 2);
        CHECK(c.stack.size() == 4 && c.stack[0] == 3 && c.stack[1] == 0 && c.stack[2] == 4 && c.stack[3] == 5);
    }

    // Eight ints: six come from x2-x7, the last two from the host stack.
    {
        zb::NativeRegs r{};
        r.x[1] = 5;
        for (int i = 2; i < 8; ++i) r.x[i] = static_cast<std::uint64_t>(i * 10 - 10);  // 10..60
        stack[0] = 70;
        stack[1] = 80;
        r.stack = stack;
        const zb::GuestCall c = marshal("VIIIIIIII", r);
        CHECK(c.regs[2] == 10 && c.regs[3] == 20);
        CHECK(c.stack.size() == 6 && c.stack[0] == 30 && c.stack[4] == 70 && c.stack[5] == 80);
    }

    // Interleaved host stack overflow: 7 ints then 9 floats. Ints 1-6 come from x2-x7, the 7th
    // from stack[0]; floats 1-8 come from d0-d7, the 9th from stack[1] -- the two classes share
    // one host stack cursor in argument order. On the guest side, r2/r3 take int1/int2 and the
    // guest stack holds int3..int7 followed by float1..float9 (14 words).
    {
        zb::NativeRegs r{};
        r.x[1] = 1;
        for (int i = 0; i < 6; ++i) r.x[2 + i] = static_cast<std::uint64_t>(100 + i);  // int1..int6
        stack[0] = 700;                                                                // int7
        for (int i = 0; i < 8; ++i) r.d[i] = fbits(static_cast<float>(i + 1));         // float1..float8
        stack[1] = fbits(9.0f);                                                        // float9
        r.stack = stack;
        const zb::GuestCall c = marshal("VIIIIIIIFFFFFFFFF", r);
        CHECK(c.regs[2] == 100 && c.regs[3] == 101);
        CHECK(c.stack.size() == 14);
        CHECK(c.stack[0] == 102 && c.stack[1] == 103 && c.stack[2] == 104 && c.stack[3] == 105 &&
              c.stack[4] == 700);
        for (int i = 0; i < 8; ++i) CHECK(c.stack[5 + i] == fbits(static_cast<float>(i + 1)));
        CHECK(c.stack[13] == fbits(9.0f));
    }

    // More than eight host FP arguments overflow d0-d7 into 8-byte host stack slots.
    {
        zb::NativeRegs r{};
        r.x[1] = 1;
        for (int i = 0; i < 8; ++i) r.d[i] = fbits(static_cast<float>(i + 1));
        stack[0] = fbits(9.0f);
        stack[1] = fbits(10.0f);
        r.stack = stack;
        const zb::GuestCall c = marshal("VFFFFFFFFFF", r);
        CHECK(c.regs[2] == fbits(1.0f) && c.regs[3] == fbits(2.0f));
        CHECK(c.stack.size() == 8 && c.stack[5] == fbits(8.0f));
        CHECK(c.stack[6] == fbits(9.0f) && c.stack[7] == fbits(10.0f));
    }

    {
        zb::NativeRegs r{};
        r.x[1] = 1;
        for (int i = 0; i < 8; ++i) r.d[i] = dbits(static_cast<double>(i + 1));
        stack[0] = dbits(9.0);
        r.stack = stack;
        const zb::GuestCall c = marshal("VDDDDDDDDD", r);
        CHECK(c.regs[2] == static_cast<std::uint32_t>(dbits(1.0)));
        CHECK(c.regs[3] == static_cast<std::uint32_t>(dbits(1.0) >> 32));
        CHECK(c.stack.size() == 16);
        CHECK(c.stack[14] == static_cast<std::uint32_t>(dbits(9.0)));
        CHECK(c.stack[15] == static_cast<std::uint32_t>(dbits(9.0) >> 32));
    }

    // Narrow types are extended per Java type, a null reference stays 0, a double after a stack
    // word is padded.
    {
        zb::NativeRegs r{};
        r.x[1] = 0;
        r.x[2] = 0x1FF;          // Z -> 0xFF
        r.x[3] = 0xFFFFFF80ull;  // B -> -128
        r.x[4] = 0x1FFFF;        // C -> 0xFFFF
        r.x[5] = 0xFFFF8000ull;  // S -> -32768
        r.x[6] = 0x42;           // L -> handle 0x1042
        r.d[0] = dbits(0.5);     // D
        r.stack = stack;
        const zb::GuestCall c = marshal("VZBCSLD", r);
        CHECK(c.regs[1] == 0);
        CHECK(c.regs[2] == 0xFF && c.regs[3] == 0xFFFFFF80u);
        CHECK(c.stack.size() == 6);
        CHECK(c.stack[0] == 0xFFFF && c.stack[1] == 0xFFFF8000u && c.stack[2] == 0x1042 && c.stack[3] == 0);
        CHECK(c.stack[4] == static_cast<std::uint32_t>(dbits(0.5)));
        CHECK(c.stack[5] == static_cast<std::uint32_t>(dbits(0.5) >> 32));
    }

    // Return values.
    {
        zb::NativeRegs r{};
        auto to_ref = [](std::uint32_t h) -> std::uint64_t { return h == 0 ? 0 : h + 0x7F0000000000ull; };
        zb::store_native_result('I', 0xFFFFFFFE, 0, r, to_ref);
        CHECK(r.x[0] == static_cast<std::uint64_t>(-2));
        zb::store_native_result('Z', 0x101, 0, r, to_ref);
        CHECK(r.x[0] == 1);
        zb::store_native_result('C', 0x1FFFF, 0, r, to_ref);
        CHECK(r.x[0] == 0xFFFF);
        zb::store_native_result('S', 0x8000, 0, r, to_ref);
        CHECK(r.x[0] == static_cast<std::uint64_t>(-32768));
        zb::store_native_result('J', 0x55667788, 0x11223344, r, to_ref);
        CHECK(r.x[0] == 0x1122334455667788ull);
        zb::store_native_result('F', static_cast<std::uint32_t>(fbits(3.5f)), 0, r, to_ref);
        CHECK(static_cast<std::uint32_t>(r.d[0]) == fbits(3.5f));
        zb::store_native_result('D', static_cast<std::uint32_t>(dbits(-1.25)),
                                static_cast<std::uint32_t>(dbits(-1.25) >> 32), r, to_ref);
        CHECK(r.d[0] == dbits(-1.25));
        zb::store_native_result('L', 0x40, 0, r, to_ref);
        CHECK(r.x[0] == 0x7F0000000040ull);
        zb::store_native_result('L', 0, 0, r, to_ref);
        CHECK(r.x[0] == 0);
    }

    // Structurally invalid shorties must never silently shift or discard arguments/results.
    {
        zb::NativeRegs r{};
        r.x[1] = 1;
        r.stack = stack;
        CHECK(aborts([&] { (void)marshal("VXI", r); }));
        CHECK(aborts([&] { zb::store_native_result('X', 0, 0, r, to_handle); }));
    }

    std::printf("jni_abi_test ok\n");
    return 0;
}
