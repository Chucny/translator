// Argument and result conversion between a host JNI call (AAPCS64) and the guest native
// function (AAPCS32 softfp).
#include "zb/native_call.h"

#include <cstddef>
#include <cstdlib>

#include "zb/log.h"

namespace zb {

namespace {

// AAPCS64 on Linux/Android: integer class in x0-x7, floating point in d0-d7, then 8-byte stack
// slots in argument order.
class HostArgReader {
public:
    explicit HostArgReader(const NativeRegs& regs) : regs_(regs) {}

    std::uint64_t next_int() { return next_int_ < 8 ? regs_.x[next_int_++] : regs_.stack[next_stack_++]; }
    std::uint64_t next_fp() { return next_fp_ < 8 ? regs_.d[next_fp_++] : regs_.stack[next_stack_++]; }

private:
    const NativeRegs& regs_;
    int next_int_ = 2;  // x0 = JNIEnv*, x1 = jclass/jobject
    int next_fp_ = 0;
    std::size_t next_stack_ = 0;
};

// AAPCS32 base standard: core registers r0-r3, then 4-byte stack words.
class GuestArgWriter {
public:
    explicit GuestArgWriter(GuestCall& call) : call_(call) {}

    void put32(std::uint32_t value) {
        if (ncrn_ < 4) {
            call_.regs[ncrn_++] = value;
        } else {
            call_.stack.push_back(value);
        }
    }

    void put64(std::uint64_t value) {
        const auto lo = static_cast<std::uint32_t>(value);
        const auto hi = static_cast<std::uint32_t>(value >> 32);
        if (ncrn_ % 2 != 0) ++ncrn_;  // doubleword alignment: next even register
        if (ncrn_ <= 2) {
            call_.regs[ncrn_] = lo;
            call_.regs[ncrn_ + 1] = hi;
            ncrn_ += 2;
            return;
        }
        ncrn_ = 4;  // no core register is used after this
        if (call_.stack.size() % 2 != 0) call_.stack.push_back(0);  // 8-byte stack alignment
        call_.stack.push_back(lo);
        call_.stack.push_back(hi);
    }

private:
    GuestCall& call_;
    int ncrn_ = 0;
};

// Passing int8_t/int16_t to these wider parameters performs the sign extension by implicit
// integral promotion before the bit-preserving unsigned conversion.
std::uint32_t sign_extend32(std::int32_t value) {
    return static_cast<std::uint32_t>(value);
}

std::uint64_t sign_extend64(std::int64_t value) {
    return static_cast<std::uint64_t>(value);
}

[[noreturn]] void invalid_shorty(std::string_view shorty) {
    log("invalid JNI shorty: '%.*s'", static_cast<int>(shorty.size()), shorty.data());
    std::abort();
}

}  // namespace

GuestCall marshal_native_args(std::string_view shorty, const NativeRegs& regs, std::uint32_t guest_env,
                              const RefToHandle& ref_to_handle) {
    GuestCall call;
    GuestArgWriter out(call);
    HostArgReader in(regs);
    out.put32(guest_env);
    out.put32(ref_to_handle(regs.x[1]));
    for (std::size_t i = 1; i < shorty.size(); ++i) {
        switch (shorty[i]) {
        case 'Z':
            out.put32(static_cast<std::uint8_t>(in.next_int()));
            break;
        case 'B':
            out.put32(sign_extend32(static_cast<std::int8_t>(in.next_int())));
            break;
        case 'C':
            out.put32(static_cast<std::uint16_t>(in.next_int()));
            break;
        case 'S':
            out.put32(sign_extend32(static_cast<std::int16_t>(in.next_int())));
            break;
        case 'I':
            out.put32(static_cast<std::uint32_t>(in.next_int()));
            break;
        case 'J':
            out.put64(in.next_int());
            break;
        case 'F':
            out.put32(static_cast<std::uint32_t>(in.next_fp()));
            break;
        case 'D':
            out.put64(in.next_fp());
            break;
        case 'L':
            out.put32(ref_to_handle(in.next_int()));
            break;
        default:
            invalid_shorty(shorty);
        }
    }
    return call;
}

void store_native_result(char return_type, std::uint32_t r0, std::uint32_t r1, NativeRegs& regs,
                         const HandleToRef& handle_to_ref) {
    const std::uint64_t pair = (static_cast<std::uint64_t>(r1) << 32) | r0;
    switch (return_type) {
    case 'Z':
        regs.x[0] = static_cast<std::uint8_t>(r0);
        break;
    case 'B':
        regs.x[0] = sign_extend64(static_cast<std::int8_t>(r0));
        break;
    case 'C':
        regs.x[0] = static_cast<std::uint16_t>(r0);
        break;
    case 'S':
        regs.x[0] = sign_extend64(static_cast<std::int16_t>(r0));
        break;
    case 'I':
        regs.x[0] = sign_extend64(static_cast<std::int32_t>(r0));
        break;
    case 'J':
        regs.x[0] = pair;
        break;
    case 'F':
        regs.d[0] = r0;
        break;
    case 'D':
        regs.d[0] = pair;
        break;
    case 'L':
        regs.x[0] = handle_to_ref(r0);
        break;
    case 'V':
        break;
    default:
        invalid_shorty(std::string_view(&return_type, 1));
    }
}

}  // namespace zb
