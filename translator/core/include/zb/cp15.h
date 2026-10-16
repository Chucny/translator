#pragma once

#include <array>  // dynarmic's coprocessor.h uses std::array without including it
#include <cstdint>
#include <optional>

#include <dynarmic/interface/A32/coprocessor.h>

namespace zb {

// Minimal CP15 for user mode: only the thread ID registers used by bionic's TLS access.
//   MRC p15, 0, Rt, c13, c0, 3  -> TPIDRURO (read-only for user mode, set via __ARM_NR_set_tls)
//   MRC/MCR p15, 0, Rt, c13, c0, 2 -> TPIDRURW
class Cp15 final : public Dynarmic::A32::Coprocessor {
public:
    Cp15(std::uint32_t* tpidruro, std::uint32_t* tpidrurw) : tpidruro_(tpidruro), tpidrurw_(tpidrurw) {}

    std::optional<Callback> CompileInternalOperation(bool two, unsigned opc1, Dynarmic::A32::CoprocReg CRd,
                                                     Dynarmic::A32::CoprocReg CRn, Dynarmic::A32::CoprocReg CRm,
                                                     unsigned opc2) override;
    CallbackOrAccessOneWord CompileSendOneWord(bool two, unsigned opc1, Dynarmic::A32::CoprocReg CRn,
                                               Dynarmic::A32::CoprocReg CRm, unsigned opc2) override;
    CallbackOrAccessTwoWords CompileSendTwoWords(bool two, unsigned opc, Dynarmic::A32::CoprocReg CRm) override;
    CallbackOrAccessOneWord CompileGetOneWord(bool two, unsigned opc1, Dynarmic::A32::CoprocReg CRn,
                                              Dynarmic::A32::CoprocReg CRm, unsigned opc2) override;
    CallbackOrAccessTwoWords CompileGetTwoWords(bool two, unsigned opc, Dynarmic::A32::CoprocReg CRm) override;
    std::optional<Callback> CompileLoadWords(bool two, bool long_transfer, Dynarmic::A32::CoprocReg CRd,
                                             std::optional<std::uint8_t> option) override;
    std::optional<Callback> CompileStoreWords(bool two, bool long_transfer, Dynarmic::A32::CoprocReg CRd,
                                              std::optional<std::uint8_t> option) override;

private:
    std::uint32_t* tpidruro_;
    std::uint32_t* tpidrurw_;
};

}  // namespace zb
