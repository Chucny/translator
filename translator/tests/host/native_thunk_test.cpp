// Calls precompiled thunks like JNI functions and checks what the dispatcher receives: the slot
// number, x0-x7, d0-d7, host stack arguments, and the result coming back through x0 / d0.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "check.h"
#include "zb/native_thunks.h"

namespace {

std::uint32_t g_slot = 0xFFFFFFFF;
zb::NativeRegs g_seen{};

void record(std::uint32_t slot, zb::NativeRegs& regs) {
    g_slot = slot;
    g_seen = regs;
    regs.x[0] = 0x1122334455667788ull;
    const double result = 2.5;
    std::memcpy(&regs.d[0], &result, 8);
}

// A dispatcher that always throws, to exercise the exception barrier in zb_native_dispatch.
void throwing_dispatch(std::uint32_t, zb::NativeRegs&) {
    throw std::runtime_error("boom");
}

// Forks, runs `function` in the child and expects it to die of SIGABRT.
bool child_aborts(const std::function<void()>& function) {
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

// -- Multi-thread stress test -----------------------------------------------------------------
// Four threads hammer disjoint slot ranges concurrently through a function type with 10 int64
// arguments and 9 double arguments, so both classes overflow onto the host stack. The dispatcher
// recomputes the expected values from (slot, counter) alone, so it needs no shared state with the
// callers beyond the mismatch counter.

using StressFn = std::int64_t (*)(std::int64_t, std::int64_t, std::int64_t, std::int64_t, std::int64_t,
                                  std::int64_t, std::int64_t, std::int64_t, std::int64_t, std::int64_t, double,
                                  double, double, double, double, double, double, double, double);

constexpr int kStressThreads = 4;
constexpr int kStressCallsPerThread = 5000;
constexpr int kStressSlotsPerThread = 8;
constexpr std::uint32_t kStressBaseSlot = 200;

std::atomic<int> g_stress_mismatches{0};

std::uint64_t stress_expected_int(std::uint32_t slot, std::uint64_t n, int i) {
    return (static_cast<std::uint64_t>(slot) * 1000003ull) ^ (n * 2654435761ull) ^
           (static_cast<std::uint64_t>(i) * 97ull + 1);
}

double stress_expected_double(std::uint32_t slot, std::uint64_t n, int j) {
    return static_cast<double>(slot) * 0.015625 + static_cast<double>(n) * 0.0009765625 +
           static_cast<double>(j) * 3.0;
}

std::uint64_t stress_expected_return(std::uint32_t slot, std::uint64_t n) {
    return (static_cast<std::uint64_t>(slot) << 32) ^ n;
}

void stress_dispatch(std::uint32_t slot, zb::NativeRegs& regs) {
    bool ok = (reinterpret_cast<std::uintptr_t>(&regs) % 16 == 0);
    const std::uint64_t n = regs.x[0];
    for (int i = 1; i <= 7; ++i) {
        if (regs.x[i] != stress_expected_int(slot, n, i)) ok = false;
    }
    if (regs.stack[0] != stress_expected_int(slot, n, 8)) ok = false;
    if (regs.stack[1] != stress_expected_int(slot, n, 9)) ok = false;
    for (int j = 0; j < 8; ++j) {
        double got;
        std::memcpy(&got, &regs.d[j], 8);
        if (got != stress_expected_double(slot, n, j)) ok = false;
    }
    double got8;
    std::memcpy(&got8, &regs.stack[2], 8);
    if (got8 != stress_expected_double(slot, n, 8)) ok = false;
    if (!ok) g_stress_mismatches.fetch_add(1);
    regs.x[0] = stress_expected_return(slot, n);
}

void stress_thread(int thread_idx) {
    const std::uint32_t base = kStressBaseSlot + static_cast<std::uint32_t>(thread_idx) * kStressSlotsPerThread;
    for (int call = 0; call < kStressCallsPerThread; ++call) {
        const std::uint32_t slot = base + static_cast<std::uint32_t>(call % kStressSlotsPerThread);
        const auto n = static_cast<std::uint64_t>(call);
        const auto fn = reinterpret_cast<StressFn>(zb::native_thunk_address(slot));
        const std::int64_t result = fn(static_cast<std::int64_t>(n),
                                       static_cast<std::int64_t>(stress_expected_int(slot, n, 1)),
                                       static_cast<std::int64_t>(stress_expected_int(slot, n, 2)),
                                       static_cast<std::int64_t>(stress_expected_int(slot, n, 3)),
                                       static_cast<std::int64_t>(stress_expected_int(slot, n, 4)),
                                       static_cast<std::int64_t>(stress_expected_int(slot, n, 5)),
                                       static_cast<std::int64_t>(stress_expected_int(slot, n, 6)),
                                       static_cast<std::int64_t>(stress_expected_int(slot, n, 7)),
                                       static_cast<std::int64_t>(stress_expected_int(slot, n, 8)),
                                       static_cast<std::int64_t>(stress_expected_int(slot, n, 9)),
                                       stress_expected_double(slot, n, 0), stress_expected_double(slot, n, 1),
                                       stress_expected_double(slot, n, 2), stress_expected_double(slot, n, 3),
                                       stress_expected_double(slot, n, 4), stress_expected_double(slot, n, 5),
                                       stress_expected_double(slot, n, 6), stress_expected_double(slot, n, 7),
                                       stress_expected_double(slot, n, 8));
        if (static_cast<std::uint64_t>(result) != stress_expected_return(slot, n)) {
            g_stress_mismatches.fetch_add(1);
        }
    }
}

}  // namespace

int main() {
    // No dispatcher has been set yet in the parent: a thunk call must abort, not crash or hang.
    CHECK(child_aborts([] {
        using Void2 = void (*)(void*, void*);
        const auto fn = reinterpret_cast<Void2>(zb::native_thunk_address(3));
        fn(nullptr, nullptr);
    }));

    zb::set_native_dispatcher(record);

    CHECK(zb::native_thunk_pool_bytes() == 8 * zb::kNativeThunkCount);

    // A dispatcher that throws must never let the C++ exception cross into the caller.
    CHECK(child_aborts([] {
        zb::set_native_dispatcher(throwing_dispatch);
        using Void2 = void (*)(void*, void*);
        const auto fn = reinterpret_cast<Void2>(zb::native_thunk_address(4));
        fn(nullptr, nullptr);
    }));

    // Ten integer-class arguments: x0-x7 take env, class and six ints; 8 and 9 go on the stack.
    using Wide = std::int64_t (*)(void*, void*, std::int64_t, std::int64_t, std::int64_t, std::int64_t,
                                  std::int64_t, std::int64_t, std::int64_t, std::int64_t, float, double);
    const auto wide = reinterpret_cast<Wide>(zb::native_thunk_address(5));
    const std::int64_t result = wide(reinterpret_cast<void*>(0xE0), reinterpret_cast<void*>(0xC1), 2, 3, 4, 5,
                                     6, 7, 8, 9, 1.5f, 3.25);
    CHECK(g_slot == 5);
    CHECK(result == 0x1122334455667788ll);
    CHECK(g_seen.x[0] == 0xE0 && g_seen.x[1] == 0xC1 && g_seen.x[2] == 2 && g_seen.x[7] == 7);
    CHECK(g_seen.stack[0] == 8 && g_seen.stack[1] == 9);
    float f;
    std::memcpy(&f, &g_seen.d[0], 4);
    CHECK(f == 1.5f);
    double d;
    std::memcpy(&d, &g_seen.d[1], 8);
    CHECK(d == 3.25);

    // The last thunk, returning a double through d0.
    using Fp = double (*)(void*, void*);
    const auto last = reinterpret_cast<Fp>(zb::native_thunk_address(zb::kNativeThunkCount - 1));
    CHECK(last(nullptr, nullptr) == 2.5);
    CHECK(g_slot == zb::kNativeThunkCount - 1);
    CHECK(zb::native_thunk_address(zb::kNativeThunkCount) == nullptr);

    zb::NativeSlots slots(2);
    CHECK(slots.allocate({0x10001, "VI", true}) == 0);
    CHECK(slots.allocate({0x20000, "IIFFIFF", false}) == 1);
    CHECK(slots.allocate({0x30000, "V", true}) == -1);  // exhausted
    CHECK(slots.target(1) != nullptr && slots.target(1)->shorty == "IIFFIFF" && !slots.target(1)->is_static);
    CHECK(slots.target(2) == nullptr);
    // Only a slot whose registration failed is released; it is handed out again before new slots.
    slots.release(1);
    CHECK(slots.target(1) == nullptr);
    std::atomic<bool> start_reuse{false};
    std::atomic<bool> bad_reuse{false};
    std::thread reuse_reader([&] {
        while (!start_reuse.load()) std::this_thread::yield();
        const zb::NativeTarget* reused = nullptr;
        while ((reused = slots.target(1)) == nullptr) std::this_thread::yield();
        if (reused->guest_function != 0x40000 || reused->shorty != "J" || reused->is_static) {
            bad_reuse = true;
        }
    });
    start_reuse = true;
    CHECK(slots.allocate({0x40000, "J", false}) == 1);
    reuse_reader.join();
    CHECK(!bad_reuse.load());
    CHECK(slots.target(1) != nullptr && slots.target(1)->guest_function == 0x40000 && slots.target(1)->shorty == "J");
    CHECK(slots.allocate({0x50000, "V", true}) == -1);
    CHECK(child_aborts([] {
        zb::NativeSlots fresh(2);
        fresh.release(0);  // never allocated
    }));

    // Multi-thread stress: 4 threads x 5000 calls each, disjoint slot ranges, both int and FP
    // arguments overflowing onto the host stack.
    zb::set_native_dispatcher(stress_dispatch);
    {
        std::vector<std::thread> threads;
        for (int t = 0; t < kStressThreads; ++t) threads.emplace_back(stress_thread, t);
        for (auto& th : threads) th.join();
    }
    CHECK(g_stress_mismatches.load() == 0);

    std::printf("native_thunk_test ok\n");
    return 0;
}
