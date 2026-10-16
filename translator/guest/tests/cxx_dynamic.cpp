// T4: C++ exceptions thrown in a shared library and caught in the executable, plus
// setjmp/longjmp and static constructors.
#include <csetjmp>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" int zbthrow_constructed();
void zbthrow_throw(int code);
void zbthrow_nested(int depth);

namespace {

std::jmp_buf jump_target;

[[noreturn]] void jump_back(int value) {
    std::longjmp(jump_target, value);
}

}  // namespace

int main() {
    std::printf("constructor=%s\n", zbthrow_constructed() == 42 ? "PASS" : "FAIL");

    bool caught = false;
    try {
        zbthrow_throw(3);
    } catch (const std::runtime_error& e) {
        caught = std::string(e.what()) == "zbthrow 3";
    }
    std::printf("throw=%s\n", caught ? "PASS" : "FAIL");

    bool nested = false;
    try {
        std::vector<std::string> keep_alive(3, "destructor runs during unwinding");
        zbthrow_nested(20);
    } catch (const std::exception& e) {
        nested = std::string(e.what()) == "zbthrow 7";
    }
    std::printf("nested=%s\n", nested ? "PASS" : "FAIL");

    volatile int passes = 0;
    const int value = setjmp(jump_target);
    if (value == 0) {
        ++passes;
        jump_back(5);
    }
    std::printf("longjmp=%s\n", value == 5 && passes == 1 ? "PASS" : "FAIL");
    return 0;
}
