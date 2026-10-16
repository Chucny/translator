#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "zb/egl_backend.h"

// Records the typed EglBackend calls HostEgl makes, without linking a real EGL driver.
class MockEgl final : public zb::EglBackend {
public:
    struct Call {
        std::string name;
        std::vector<std::uint64_t> arguments;
    };

    const std::vector<Call>& calls() const { return calls_; }
    void clear_calls() { calls_.clear(); }
    void set_result(const std::string& name, std::uint64_t value) { results_[name] = value; }
    void set_configs(std::vector<void*> configs) { configs_ = std::move(configs); }
    void set_string(zb::EGLint name, std::string value) { strings_[name] = std::move(value); }
    void set_attribute(zb::EGLint attribute, zb::EGLint value) { attributes_[attribute] = value; }
    // Copied at call time: HostEgl's own list is a temporary.
    const std::vector<zb::EGLint>& last_attribs() const { return last_attribs_; }

protected:
    std::uint64_t invoke(const char* name,
                         std::initializer_list<std::uint64_t> arguments) override;

private:
    std::vector<Call> calls_;
    std::unordered_map<std::string, std::uint64_t> results_;
    std::unordered_map<zb::EGLint, std::string> strings_;
    std::unordered_map<zb::EGLint, zb::EGLint> attributes_;
    std::vector<void*> configs_;
    std::vector<zb::EGLint> last_attribs_;
};
