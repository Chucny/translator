#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace zb {

// Registers of a JNI native call as saved by zb_native_common (thunks.S). The layout is shared
// with the assembly: x0-x7, the raw bits of d0-d7 (a float uses the low 32 bits), then the
// address of the caller's stack arguments.
struct NativeRegs {
    std::uint64_t x[8];
    std::uint64_t d[8];
    std::uint64_t* stack;
    std::uint64_t pad;
};

// Arguments of a guest AAPCS32 softfp call: r0-r3 plus stack words, lowest address first.
struct GuestCall {
    std::array<std::uint32_t, 4> regs{};
    std::vector<std::uint32_t> stack;
};

using RefToHandle = std::function<std::uint32_t(std::uint64_t host_ref)>;
using HandleToRef = std::function<std::uint64_t(std::uint32_t handle)>;

// Builds the guest call for a native method with the given shorty (return type first):
// r0 = guest JNIEnv*, r1 = handle for x1 (jclass or jobject), then the Java arguments.
// shorty must be produced by shorty_from_signature; an unknown letter is a fatal invariant
// violation.
GuestCall marshal_native_args(std::string_view shorty, const NativeRegs& regs, std::uint32_t guest_env,
                              const RefToHandle& ref_to_handle);

// Stores the guest result (r0, r1) in regs.x[0] or regs.d[0] according to the return type.
// return_type must come from a shorty produced by shorty_from_signature.
void store_native_result(char return_type, std::uint32_t r0, std::uint32_t r1, NativeRegs& regs,
                         const HandleToRef& handle_to_ref);

}  // namespace zb
