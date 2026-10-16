#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "zb/gl_backend.h"

class MockGles final : public zb::GlBackend {
public:
    struct Call {
        std::string name;
        std::vector<std::uint64_t> arguments;
    };

    struct ActiveUniform {
        std::string name;
        zb::GLint size;
        zb::GLenum type;
        zb::GLint location;
    };

    struct ShaderSources {
        std::vector<std::string> strings;
        std::vector<zb::GLint> lengths;
    };

    void set_error(zb::GLenum error) override { error_ = error; }
    zb::GLenum error() const { return error_; }
    const std::vector<Call>& calls() const { return calls_; }
    void clear_calls() { calls_.clear(); }
    void set_result(const std::string& name, std::uint64_t value) { results_[name] = value; }
    void set_integer(zb::GLenum pname, zb::GLint value) { integers_[pname] = value; }
    void set_active_uniforms(zb::GLuint program, std::vector<ActiveUniform> uniforms) {
        uniforms_[program] = std::move(uniforms);
    }
    void set_string(zb::GLenum name, std::string value) { strings_[name] = std::move(value); }
    const ShaderSources& shader_sources() const { return shader_sources_; }

protected:
    std::uint64_t invoke(const char* name,
                         std::initializer_list<std::uint64_t> arguments) override;

private:
    std::vector<Call> calls_;
    std::unordered_map<std::string, std::uint64_t> results_;
    std::unordered_map<zb::GLenum, zb::GLint> integers_;
    std::unordered_map<zb::GLuint, std::vector<ActiveUniform>> uniforms_;
    std::unordered_map<zb::GLenum, std::string> strings_;
    ShaderSources shader_sources_;
    std::uint32_t next_id_ = 1;
    zb::GLenum error_ = 0;
};
