// Test library: throws C++ exceptions that must unwind into the calling executable (EXIDX,
// libc++_shared), and runs a static constructor at dlopen/load time.
#include <stdexcept>
#include <string>

namespace {

struct Registrar {
    Registrar() { constructed = 42; }
    int constructed = 0;
};

Registrar registrar;

}  // namespace

extern "C" int zbthrow_constructed() {
    return registrar.constructed;
}

void zbthrow_throw(int code) {
    if (code != 0) throw std::runtime_error("zbthrow " + std::to_string(code));
}

void zbthrow_nested(int depth) {
    if (depth == 0) {
        zbthrow_throw(7);
        return;
    }
    zbthrow_nested(depth - 1);
}
