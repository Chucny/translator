#include "mock_egl.h"

#include <algorithm>
#include <cstring>

std::uint64_t MockEgl::invoke(const char* name,
                              std::initializer_list<std::uint64_t> arguments) {
    calls_.push_back(Call{name, std::vector<std::uint64_t>(arguments)});
    const std::string& called = calls_.back().name;
    const auto& args = calls_.back().arguments;
    if ((called == "eglChooseConfig" && args.size() == 5) ||
        (called == "eglGetConfigs" && args.size() == 4)) {
        const std::size_t configs_index = called == "eglChooseConfig" ? 2 : 1;
        last_attribs_.clear();
        if (called == "eglChooseConfig" && args[1] != 0) {
            const auto* attribs = reinterpret_cast<const zb::EGLint*>(args[1]);
            for (std::size_t i = 0; i < 64; ++i) {
                last_attribs_.push_back(attribs[i]);
                if (attribs[i] == 0x3038) break;  // EGL_NONE
            }
        }
        auto** configs = reinterpret_cast<void**>(args[configs_index]);
        const auto capacity = static_cast<std::size_t>(static_cast<zb::EGLint>(args[configs_index + 1]));
        const std::size_t written = configs == nullptr ? 0 : std::min(capacity, configs_.size());
        for (std::size_t i = 0; i < written; ++i) configs[i] = configs_[i];
        if (args[configs_index + 2] != 0) {
            *reinterpret_cast<zb::EGLint*>(args[configs_index + 2]) =
                static_cast<zb::EGLint>(configs == nullptr ? configs_.size() : written);
        }
        return 1;
    }
    if (called == "eglQueryString" && args.size() == 2) {
        const auto found = strings_.find(static_cast<zb::EGLint>(args[1]));
        return found == strings_.end()
                   ? 0
                   : reinterpret_cast<std::uintptr_t>(found->second.c_str());
    }
    if ((called == "eglGetConfigAttrib" || called == "eglQuerySurface" ||
         called == "eglQueryContext") &&
        args.size() == 4 && args[3] != 0) {
        const auto found = attributes_.find(static_cast<zb::EGLint>(args[2]));
        *reinterpret_cast<zb::EGLint*>(args[3]) = found == attributes_.end() ? 0 : found->second;
        return 1;
    }
    const auto found = results_.find(name);
    if (found != results_.end()) return found->second;
    return 0;
}
