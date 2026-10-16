// GL visibility diagnostics for the runtime report. The render loop runs with no GL error but
// shows a black screen, and glGetError does not catch shader failures, off-screen framebuffers,
// empty viewports or bad float data. After each accepted GL host call this records a few facts
// (and queries the driver a few times) into RuntimeReport's "gl-<key>" lines.

#include "gl/gl_diagnostics.h"

#include <sys/syscall.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "zb/gl_hostcalls.h"
#include "zb/runtime_report.h"

namespace zb {
namespace {

constexpr GLenum kCompileStatus = 0x8B81;
constexpr GLenum kLinkStatus = 0x8B82;
constexpr GLenum kFramebufferBinding = 0x8CA6;
constexpr GLenum kViewport = 0x0BA2;
constexpr GLenum kScissorBox = 0x0C10;
constexpr GLenum kCurrentProgram = 0x8B8D;
constexpr GLenum kArrayBuffer = 0x8892;
constexpr GLenum kArrayBufferBinding = 0x8894;
constexpr GLenum kColorWritemask = 0x0C23;
constexpr GLenum kScissorTest = 0x0C11;
constexpr GLenum kDepthTest = 0x0B71;
constexpr GLenum kBlend = 0x0BE2;
constexpr GLenum kBlendDstRgb = 0x80C8;
constexpr GLenum kBlendSrcRgb = 0x80C9;
constexpr GLenum kBlendDstAlpha = 0x80CA;
constexpr GLenum kBlendSrcAlpha = 0x80CB;
constexpr GLenum kCullFace = 0x0B44;
constexpr GLenum kStencilTest = 0x0B90;
constexpr GLenum kFramebuffer = 0x8D40;
constexpr GLenum kRgba = 0x1908;
constexpr GLenum kUnsignedByte = 0x1401;
constexpr GLenum kFloat = 0x1406;
constexpr GLenum kTextureBinding2d = 0x8069;
constexpr GLenum kActiveTexture = 0x84E0;
constexpr GLenum kTexture2d = 0x0DE1;
constexpr GLenum kMagFilter = 0x2800;
constexpr GLenum kMinFilter = 0x2801;
constexpr GLenum kWrapS = 0x2802;
constexpr GLenum kWrapT = 0x2803;
constexpr GLenum kAttribEnabled = 0x8622;
constexpr GLenum kAlpha = 0x1906;
constexpr GLenum kRgb = 0x1907;
constexpr GLenum kLuminance = 0x1909;
constexpr GLenum kLuminanceAlpha = 0x190A;
constexpr GLenum kR8 = 0x8229;
// Real value per GLES3/gl3.h; NOT 0x8C8A.
constexpr GLenum kPixelUnpackBufferBinding = 0x88EF;
constexpr GLenum kUniformBuffer = 0x8A11;
constexpr GLenum kUniformBufferBinding = 0x8A28;
constexpr GLenum kElementArrayBuffer = 0x8893;
constexpr GLenum kElementArrayBufferBinding = 0x8895;
constexpr GLenum kCopyReadBuffer = 0x8F36;
constexpr GLenum kCopyReadBufferBinding = 0x8F36;
constexpr GLenum kCopyWriteBuffer = 0x8F37;
constexpr GLenum kCopyWriteBufferBinding = 0x8F37;
constexpr GLenum kPixelPackBuffer = 0x88EB;
constexpr GLenum kPixelPackBufferBinding = 0x88ED;
constexpr GLenum kPixelUnpackBuffer = 0x88EC;
constexpr GLenum kTransformFeedbackBuffer = 0x8C8E;
constexpr GLenum kTransformFeedbackBufferBinding = 0x8C8F;
constexpr GLenum kUniformBufferStart = 0x8A29;
constexpr GLenum kUniformBufferSize = 0x8A2A;
constexpr GLenum kVertexAttribArraySize = 0x8623;
constexpr GLenum kVertexAttribArrayStride = 0x8624;
constexpr GLenum kVertexAttribArrayType = 0x8625;
constexpr GLenum kVertexAttribArrayNormalized = 0x886A;
constexpr GLenum kVertexAttribArrayBufferBinding = 0x889F;
constexpr GLenum kVertexAttribArrayPointer = 0x8645;
constexpr int kMaxTrackedVertexAttribs = 8;

// Bytes per pixel for GL_UNSIGNED_BYTE uploads of the formats seen in practice. Returns 0 for
// anything else (compressed, float, or a format this diagnostic does not know), in which case the
// byte-level inspection below is skipped.
GLint channels_for_format(GLenum format) {
    switch (format) {
    case kRgba: return 4;
    case kRgb: return 3;
    case kLuminanceAlpha: return 2;
    case kAlpha:
    case kLuminance:
    case kR8: return 1;
    default: return 0;
    }
}

struct Attrib {
    bool set = false;
    GLint size = 0;
    GLenum type = 0;
    GLint stride = 0;
    std::uint32_t pointer = 0;
};

// Draw numbers at which the state and a 3x3 pixel grid are sampled after the draw.
constexpr std::uint64_t kSampleDraws[] = {1, 300, 3000, 30000};

struct State {
    struct BufferSnapshot {
        std::vector<std::uint8_t> bytes;
        std::vector<std::uint8_t> known;
    };

    std::mutex mutex;
    std::uint64_t shaders_ok = 0;
    std::uint64_t shaders_failed = 0;
    std::uint64_t programs_ok = 0;
    std::uint64_t programs_failed = 0;
    std::uint64_t draws = 0;
    std::uint64_t empty_draws = 0;
    std::uint64_t offscreen_draws = 0;
    std::uint64_t clears = 0;
    std::uint64_t fb_binds_nonzero = 0;
    std::uint64_t tex_images = 0;
    std::uint64_t attrib_pointers = 0;
    std::uint64_t buffer_datas = 0;
    std::uint64_t matrices = 0;
    std::uint64_t use_programs = 0;
    std::uint64_t tex_uploads = 0;
    std::uint64_t tex_sub_images = 0;
    std::uint64_t tex_parameters = 0;
    std::uint64_t mipmaps = 0;
    std::uint64_t uniform_ints = 0;
    std::uint64_t pixel_stores = 0;
    std::uint64_t shader_sources = 0;
    std::uint64_t clear_samples = 0;
    GLuint bound_framebuffer = 0;
    Attrib attribs[8];

    // Per-glTexSubImage2D detail lines (first 12 calls).
    std::uint64_t texsub_details = 0;

    // The texture id of the glyph atlas: the first glTexImage2D whose size/format matched.
    // 0 means "not seen yet"; real texture ids are never 0.
    GLuint glyph_atlas_texture = 0;
    std::uint64_t glyph_uploads = 0;
    std::uint64_t glyph_bytes = 0;
    std::uint64_t glyph_nonzero = 0;

    // First 4 compressed tex image / sub-image calls, of either function.
    std::uint64_t compressed_calls = 0;

    // First 8 glBufferData/glBufferSubData calls whose target is GL_UNIFORM_BUFFER.
    std::uint64_t ubo_buffer_calls = 0;
    // First 8 glBindBufferRange/glBindBufferBase calls.
    std::uint64_t ubo_bind_calls = 0;
    // First 8 glUniformBlockBinding/glGetUniformBlockIndex calls.
    std::uint64_t uniform_block_calls = 0;

    // Every draw with the glyph atlas texture bound, split by the framebuffer bound at that
    // draw. "onscreen" here means fb 0 with a viewport bigger than 64x64: real content, not
    // Impeller's 2x2 offscreen warm-up draws.
    std::uint64_t atlas_draws_total = 0;
    std::uint64_t atlas_draws_fb0 = 0;
    std::uint64_t atlas_draws_onscreen = 0;

    // Before/after framebuffer readback around the first 3 onscreen draws with the glyph atlas
    // texture bound to the active texture unit.
    std::uint64_t text_draws_captured = 0;
    bool text_snapshot_pending = false;
    GLint text_snapshot_x = 0;
    GLint text_snapshot_y = 0;
    std::uint8_t text_snapshot_before[32 * 32 * 4] = {};

    // Whole-framebuffer before/after readback around the first 2 onscreen glyph-atlas draws,
    // to prove whether the text draw changes the framebuffer at all (not just at the screen
    // centre). Buffers are allocated right before the readback and freed right after the
    // comparison; nothing large is kept between draws.
    std::uint64_t text_fullfb_done = 0;
    bool text_fullfb_pending = false;
    GLint text_fullfb_viewport[4] = {0, 0, 0, 0};
    std::vector<std::uint8_t> text_fullfb_before;

    // Cheap position-only whole-framebuffer diff (no example pixels) for the first 8 onscreen
    // glyph-atlas draws, plus the running union of their boxes. Answers "do all glyphs land in
    // one small region" without the cost of keeping example pixels around.
    std::uint64_t text_box_done = 0;
    bool text_box_pending = false;
    GLint text_box_viewport[4] = {0, 0, 0, 0};
    std::vector<std::uint8_t> text_box_before;
    bool text_box_union_valid = false;
    GLint text_box_union_min_x = 0;
    GLint text_box_union_min_y = 0;
    GLint text_box_union_max_x = -1;
    GLint text_box_union_max_y = -1;

    // FragInfo color classification for the first 32 onscreen glyph-atlas draws: is this
    // white-on-white, or does the text color vary? Independent of text_draws_captured (which
    // caps at 3 for the heavier before/after readback capture above).
    std::uint64_t text_color_draws = 0;
    std::uint64_t text_color_white = 0;
    std::uint64_t text_color_black = 0;
    std::uint64_t text_color_other = 0;
    std::vector<std::array<float, 4>> text_color_distinct;

    // Absolute-pixel box of the first and second recorded glyph draws (gl-text-box-1/2), for the
    // high-resolution single-glyph maps in gl_diagnose_swap(). Index 0 is draw 1, index 1 is
    // draw 2. Set once each, never overwritten after that.
    bool text_box_first_valid[2] = {false, false};
    GLint text_box_first_x[2] = {0, 0};
    GLint text_box_first_y[2] = {0, 0};
    GLint text_box_first_w[2] = {0, 0};
    GLint text_box_first_h[2] = {0, 0};

    // Same idea for one control draw: the first onscreen fb-0 draw that writes colour but does
    // NOT use the glyph atlas texture. Proves the readback/diff machinery itself works.
    bool nontext_diff_done = false;
    bool nontext_diff_pending = false;
    GLint nontext_diff_viewport[4] = {0, 0, 0, 0};
    std::vector<std::uint8_t> nontext_diff_before;

    // Whole-framebuffer before/after diffs selected by FragInfo colour, not draw order: the
    // first 6 onscreen (fb0) glyph-shader draws whose colour is neither white nor degenerate
    // (all-zero), and separately the first 3 such draws that target a non-default framebuffer
    // (text drawn into an offscreen layer, composited later). Grey and amber text draws have
    // never been measured before this; white text is already proven onscreen.
    std::uint64_t coloured_text_fb0_done = 0;
    bool coloured_text_fb0_pending = false;
    GLint coloured_text_fb0_viewport[4] = {0, 0, 0, 0};
    GLint coloured_text_fb0_scissor[4] = {0, 0, 0, 0};
    GLint coloured_text_fb0_program = -1;
    GLint coloured_text_fb0_fb = 0;
    std::array<float, 4> coloured_text_fb0_rgba = {0, 0, 0, 0};
    std::vector<std::uint8_t> coloured_text_fb0_before;

    std::uint64_t coloured_text_off_done = 0;
    bool coloured_text_off_pending = false;
    bool coloured_text_off_seen = false;
    GLint coloured_text_off_viewport[4] = {0, 0, 0, 0};
    GLint coloured_text_off_scissor[4] = {0, 0, 0, 0};
    GLint coloured_text_off_program = -1;
    GLint coloured_text_off_fb = 0;
    std::array<float, 4> coloured_text_off_rgba = {0, 0, 0, 0};
    std::vector<std::uint8_t> coloured_text_off_before;

    // gl-text-class: every glyph-shader draw (onscreen or offscreen), split by colour class.
    std::uint64_t text_class_white_fb0 = 0;
    std::uint64_t text_class_white_off = 0;
    std::uint64_t text_class_coloured_fb0 = 0;
    std::uint64_t text_class_coloured_off = 0;
    std::uint64_t text_class_other = 0;

    std::uint64_t map_calls = 0;
    std::uint64_t map_collisions = 0;
    bool legacy_texture_error_checked = false;

    // Bounded shadow of buffers fed through glBufferData/SubData. Impeller uploads one combined
    // vertex/uniform buffer as GL_ARRAY_BUFFER, then binds slices of it as GL_UNIFORM_BUFFER.
    std::unordered_map<GLuint, BufferSnapshot> buffer_snapshots;
    std::uint64_t buffer_uploads = 0;

    // The GlBackend most recently seen by gl_diagnose/gl_diagnose_before, so gl_diagnose_swap()
    // (called from the EGL side, which has no GlBackend of its own) can issue its own readbacks.
    // Never owned here; it outlives the process the same way HostGl does.
    GlBackend* backend_cache = nullptr;

    // Whole-frame presented-content diagnostics: counts every eglSwapBuffers of the default
    // framebuffer so the 60th and 200th can be captured right before the swap.
    std::uint64_t swap_count = 0;
};

State& state() {
    static State* s = new State();
    return *s;
}

std::string format(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string format(const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof buffer, fmt, args);
    va_end(args);
    return buffer;
}

void detail(const char* key, const std::string& value, bool overwrite = false) {
    runtime_report().note_gl_detail(key, value, overwrite);
}

std::uint64_t fnv1a(const std::uint8_t* bytes, std::uint64_t length) {
    std::uint64_t hash = 14695981039346656037ull;
    for (std::uint64_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

GLenum buffer_binding(GLenum target) {
    switch (target) {
    case kArrayBuffer: return kArrayBufferBinding;
    case kElementArrayBuffer: return kElementArrayBufferBinding;
    case kCopyReadBuffer: return kCopyReadBufferBinding;
    case kCopyWriteBuffer: return kCopyWriteBufferBinding;
    case kPixelPackBuffer: return kPixelPackBufferBinding;
    case kPixelUnpackBuffer: return kPixelUnpackBufferBinding;
    case kTransformFeedbackBuffer: return kTransformFeedbackBufferBinding;
    case kUniformBuffer: return kUniformBufferBinding;
    default: return 0;
    }
}

GLuint current_buffer(HostGl& host, GLenum target) {
    const GLenum binding = buffer_binding(target);
    if (binding == 0) return 0;
    GLint buffer = 0;
    host.backend().glGetIntegerv(binding, &buffer);
    return buffer > 0 ? static_cast<GLuint>(buffer) : 0;
}

std::string shader_text(GlBackend& gl, GLuint object, bool source) {
    char buffer[400] = {};
    GLsizei length = 0;
    if (source) {
        gl.glGetShaderSource(object, sizeof buffer, &length, buffer);
    } else {
        gl.glGetShaderInfoLog(object, sizeof buffer, &length, buffer);
    }
    if (length < 0 || length >= static_cast<GLsizei>(sizeof buffer)) length = 0;
    return std::string(buffer, static_cast<std::size_t>(length));
}

std::string float_words(HostGl& host, std::uint32_t address, std::uint32_t words) {
    if (address == 0) return "(null)";
    const std::uint8_t* data = host.runtime().memory().host_ptr(address, 4ull * words, kPageRead);
    if (data == nullptr) return format("(unreadable 0x%x)", address);
    std::string out;
    for (std::uint32_t i = 0; i < words; ++i) {
        float value;
        std::memcpy(&value, data + 4 * i, 4);
        out += format(i == 0 ? "%g" : " %g", static_cast<double>(value));
    }
    return out;
}

// Short, best-effort guest C-string read for diagnostic labels only (block names). Unlike
// gl_manual.cpp's guest_string(), this never fails the call: it just reports "(unreadable)".
std::string guest_short_string(HostGl& host, std::uint32_t address) {
    if (address == 0) return "(null)";
    constexpr std::uint32_t kMax = 63;
    const std::uint8_t* data = host.runtime().memory().host_ptr(address, kMax, kPageRead);
    if (data == nullptr) return format("(unreadable 0x%x)", address);
    std::uint32_t length = 0;
    while (length < kMax && data[length] != 0) ++length;
    return std::string(reinterpret_cast<const char*>(data), length);
}

// First `words` 32-bit values at a guest address, decoded both as float and as uint32 hex.
std::string dual_words(HostGl& host, std::uint32_t address, std::uint32_t words) {
    if (address == 0 || words == 0) return "floats=(none) hex=(none)";
    const std::uint8_t* data = host.runtime().memory().host_ptr(address, 4ull * words, kPageRead);
    if (data == nullptr) return format("(unreadable 0x%x)", address);
    std::string floats, hexes;
    for (std::uint32_t i = 0; i < words; ++i) {
        float f;
        std::uint32_t u;
        std::memcpy(&f, data + 4 * i, 4);
        std::memcpy(&u, data + 4 * i, 4);
        floats += format(i == 0 ? "%g" : " %g", static_cast<double>(f));
        hexes += format(i == 0 ? "%08x" : " %08x", u);
    }
    return "floats=[" + floats + "] hex=[" + hexes + "]";
}

// glBufferData(target,size,data,usage) and glBufferSubData(target,offset,size,data) both funnel
// here when their target is GL_UNIFORM_BUFFER: that is how Impeller feeds FragInfo's std140
// uniforms to the text pipeline.
void record_ubo_buffer(HostGl& host, HostGl::Call& call, State& s, bool sub_data) {
    const GLenum target = call.arg(0);
    if (target != kUniformBuffer || s.ubo_buffer_calls >= 8) return;
    ++s.ubo_buffer_calls;
    GlBackend& gl = host.backend();
    GLint bound = -1;
    gl.glGetIntegerv(kUniformBufferBinding, &bound);
    std::uint32_t offset_arg = 0, size_arg = 0, data_ptr = 0;
    if (sub_data) {
        offset_arg = call.arg(1);
        size_arg = call.arg(2);
        data_ptr = call.arg(3);
    } else {
        size_arg = call.arg(1);
        data_ptr = call.arg(2);
    }
    const std::uint32_t words = std::min<std::uint32_t>(8, size_arg / 4);
    const std::string key = "ubo-buffer-" + std::to_string(s.ubo_buffer_calls);
    detail(key.c_str(), format("fn=%s bound-buffer=%d offset=%u size=%u ",
                               sub_data ? "glBufferSubData" : "glBufferData", bound, offset_arg,
                               size_arg) +
                             dual_words(host, data_ptr, words));
}

constexpr std::uint64_t kMaxTrackedBufferBytes = 2u << 20;
constexpr std::size_t kMaxTrackedBuffers = 4;

void record_buffer_upload(HostGl& host, HostGl::Call& call, State& s, bool sub_data) {
    const GLenum target = call.arg(0);
    if (target != kArrayBuffer && target != kUniformBuffer) return;
    const GLuint buffer = current_buffer(host, target);
    if (buffer == 0) return;

    const GLintptr signed_offset = sub_data ? call.scalar<GLintptr>(1) : 0;
    const GLsizeiptr signed_size = call.scalar<GLsizeiptr>(sub_data ? 2 : 1);
    if (signed_offset < 0 || signed_size < 0) return;
    const std::uint64_t offset = static_cast<std::uint64_t>(signed_offset);
    const std::uint64_t size = static_cast<std::uint64_t>(signed_size);
    if (offset > kMaxTrackedBufferBytes || size > kMaxTrackedBufferBytes - offset) return;
    const std::uint32_t data_address = call.arg(sub_data ? 3 : 2);
    const std::uint8_t* data = data_address == 0
                                   ? nullptr
                                   : host.runtime().memory().host_ptr(data_address, size, kPageRead);

    auto found = s.buffer_snapshots.find(buffer);
    if (found == s.buffer_snapshots.end()) {
        if (s.buffer_snapshots.size() >= kMaxTrackedBuffers) return;
        found = s.buffer_snapshots.emplace(buffer, State::BufferSnapshot{}).first;
    }
    State::BufferSnapshot& snapshot = found->second;
    if (!sub_data) {
        snapshot.bytes.assign(static_cast<std::size_t>(size), 0);
        snapshot.known.assign(static_cast<std::size_t>(size), data != nullptr ? 1 : 0);
        if (data != nullptr) std::memcpy(snapshot.bytes.data(), data, static_cast<std::size_t>(size));
    } else {
        const std::uint64_t end = offset + size;
        if (snapshot.bytes.size() < end) {
            snapshot.bytes.resize(static_cast<std::size_t>(end));
            snapshot.known.resize(static_cast<std::size_t>(end));
        }
        if (data != nullptr) {
            std::memcpy(snapshot.bytes.data() + offset, data, static_cast<std::size_t>(size));
            std::fill(snapshot.known.begin() + static_cast<std::ptrdiff_t>(offset),
                      snapshot.known.begin() + static_cast<std::ptrdiff_t>(end), 1);
        }
    }

    if (sub_data && ++s.buffer_uploads <= 12) {
        const std::string key = "buffer-upload-" + std::to_string(s.buffer_uploads);
        detail(key.c_str(),
               data == nullptr
                   ? format("target=0x%x buffer=%u offset=%llu size=%llu data=null", target,
                            buffer, (unsigned long long)offset, (unsigned long long)size)
                   : format("target=0x%x buffer=%u offset=%llu size=%llu fnv=%016llx", target,
                            buffer, (unsigned long long)offset, (unsigned long long)size,
                            (unsigned long long)fnv1a(data, size)));
    }
}

void record_bound_ubo_data(HostGl::Call& call, State& s, std::uint64_t bind_number) {
    const GLuint buffer = call.arg(2);
    const std::uint64_t offset = call.arg(3);
    const std::uint64_t size = call.arg(4);
    const std::string key = "ubo-bind-" + std::to_string(bind_number) + "-data";
    const auto found = s.buffer_snapshots.find(buffer);
    if (found == s.buffer_snapshots.end() || offset > found->second.bytes.size() ||
        size > found->second.bytes.size() - offset) {
        detail(key.c_str(), format("buffer=%u offset=%llu size=%llu captured=no", buffer,
                                  (unsigned long long)offset, (unsigned long long)size));
        return;
    }
    const State::BufferSnapshot& snapshot = found->second;
    const auto known_begin = snapshot.known.begin() + static_cast<std::ptrdiff_t>(offset);
    const std::size_t known = static_cast<std::size_t>(
        std::count(known_begin, known_begin + static_cast<std::ptrdiff_t>(size), std::uint8_t{1}));
    if (known != size) {
        detail(key.c_str(), format("buffer=%u offset=%llu size=%llu captured=partial known=%llu/%llu",
                                  buffer, (unsigned long long)offset, (unsigned long long)size,
                                  (unsigned long long)known, (unsigned long long)size));
        return;
    }
    const std::uint8_t* bytes = snapshot.bytes.data() + offset;
    std::string value = format("buffer=%u offset=%llu size=%llu captured=yes fnv=%016llx head=",
                               buffer, (unsigned long long)offset, (unsigned long long)size,
                               (unsigned long long)fnv1a(bytes, size));
    for (std::uint64_t i = 0; i < std::min<std::uint64_t>(size, 32); ++i) {
        value += format("%02x", bytes[i]);
    }
    detail(key.c_str(), value);
}

void sample(HostGl& host, std::uint64_t draw, GLenum mode, GLsizei count) {
    GlBackend& gl = host.backend();
    GLint framebuffer = -1, program = -1, array_buffer = -1;
    GLint viewport[4] = {-1, -1, -1, -1};
    GLint scissor[4] = {-1, -1, -1, -1};
    GLboolean mask[4] = {9, 9, 9, 9};
    gl.glGetIntegerv(kFramebufferBinding, &framebuffer);
    gl.glGetIntegerv(kCurrentProgram, &program);
    gl.glGetIntegerv(kArrayBufferBinding, &array_buffer);
    gl.glGetIntegerv(kViewport, viewport);
    gl.glGetIntegerv(kScissorBox, scissor);
    gl.glGetBooleanv(kColorWritemask, mask);
    const GLenum fb_status = gl.glCheckFramebufferStatus(kFramebuffer);
    std::string text = format(
        "mode=0x%x count=%d fb=%d fb-status=0x%x program=%d array-buffer=%d viewport=%d,%d,%dx%d "
        "scissor-test=%d box=%d,%d,%dx%d depth=%d blend=%d cull=%d stencil=%d colormask=%d%d%d%d",
        mode, count, framebuffer, fb_status, program, array_buffer, viewport[0], viewport[1],
        viewport[2], viewport[3], gl.glIsEnabled(kScissorTest), scissor[0], scissor[1], scissor[2],
        scissor[3], gl.glIsEnabled(kDepthTest), gl.glIsEnabled(kBlend), gl.glIsEnabled(kCullFace),
        gl.glIsEnabled(kStencilTest), mask[0], mask[1], mask[2], mask[3]);
    const std::string key = "sample-draw-" + std::to_string(draw);
    detail(key.c_str(), text);

    GLint texture = -1, active = -1;
    GLint params[4] = {-1, -1, -1, -1};
    gl.glGetIntegerv(kTextureBinding2d, &texture);
    gl.glGetIntegerv(kActiveTexture, &active);
    gl.glGetTexParameteriv(kTexture2d, kMinFilter, &params[0]);
    gl.glGetTexParameteriv(kTexture2d, kMagFilter, &params[1]);
    gl.glGetTexParameteriv(kTexture2d, kWrapS, &params[2]);
    gl.glGetTexParameteriv(kTexture2d, kWrapT, &params[3]);
    const std::string texture_key = key + "-texture";
    detail(texture_key.c_str(), format("active=0x%x bound=%d min=0x%x mag=0x%x wrap-s=0x%x wrap-t=0x%x",
                                       active, texture, params[0], params[1], params[2], params[3]));

    std::string vertices;
    for (GLuint index = 0; index < 8; ++index) {
        GLint enabled = 0;
        gl.glGetVertexAttribiv(index, kAttribEnabled, &enabled);
        if (!enabled) continue;
        const Attrib& attrib = state().attribs[index];
        vertices += format("%sa%u:", vertices.empty() ? "" : " | ", index);
        if (!attrib.set) {
            vertices += "(no pointer)";
            continue;
        }
        const GLint step = attrib.stride != 0 ? attrib.stride
                                              : attrib.size * (attrib.type == kFloat ? 4 : 1);
        for (int vertex = 0; vertex < 3; ++vertex) {
            const std::uint32_t at = attrib.pointer + static_cast<std::uint32_t>(step * vertex);
            if (attrib.type == kFloat) {
                vertices += " [" + float_words(host, at, static_cast<std::uint32_t>(attrib.size)) + "]";
            } else {
                const std::uint8_t* bytes = host.runtime().memory().host_ptr(
                    at, static_cast<std::uint64_t>(attrib.size), kPageRead);
                vertices += " [";
                for (GLint i = 0; bytes != nullptr && i < attrib.size; ++i) {
                    vertices += format(i == 0 ? "%u" : " %u", bytes[i]);
                }
                vertices += "]";
            }
        }
    }
    const std::string vertex_key = key + "-vertices";
    detail(vertex_key.c_str(), vertices);

    if (viewport[2] <= 0 || viewport[3] <= 0) return;
    std::string pixels;
    for (int row = 1; row <= 3; ++row) {
        for (int column = 1; column <= 3; ++column) {
            const GLint x = viewport[0] + viewport[2] * column / 4;
            const GLint y = viewport[1] + viewport[3] * row / 4;
            std::uint8_t rgba[4] = {};
            gl.glReadPixels(x, y, 1, 1, kRgba, kUnsignedByte, rgba);
            pixels += format("%s%02x%02x%02x%02x", pixels.empty() ? "" : " ", rgba[0], rgba[1],
                             rgba[2], rgba[3]);
        }
    }
    const std::string pixel_key = key + "-pixels";
    detail(pixel_key.c_str(), pixels);
}

std::atomic<bool> g_enabled{false};

}  // namespace

void enable_gl_diagnostics() { g_enabled.store(true, std::memory_order_relaxed); }

bool gl_diagnostics_enabled() { return g_enabled.load(std::memory_order_relaxed); }

GlMapDiagnostic gl_diagnose_map(HostGl& host, GLenum target, GLintptr offset, GLsizeiptr length,
                                GLbitfield access, std::uint32_t guest, const std::uint8_t* mirror,
                                const std::uint8_t* driver) {
    if (!gl_diagnostics_enabled()) return {};
    GlMapDiagnostic diagnostic;
    diagnostic.context = host.egl_context();
    diagnostic.buffer = current_buffer(host, target);
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    diagnostic.id = ++s.map_calls;
    if (diagnostic.id > 12) return {};
    const std::string key = "map-" + std::to_string(diagnostic.id);
    detail(key.c_str(),
           format("context=0x%llx target=0x%x buffer=%u offset=%lld length=%lld access=0x%x "
                  "guest=0x%x mirror-fnv=%016llx driver-fnv=%016llx",
                  (unsigned long long)diagnostic.context, target, diagnostic.buffer,
                  (long long)offset, (long long)length, access, guest,
                  (unsigned long long)fnv1a(mirror, static_cast<std::uint64_t>(length)),
                  (unsigned long long)fnv1a(driver, static_cast<std::uint64_t>(length))));
    return diagnostic;
}

void gl_diagnose_map_flush(const GlMapDiagnostic& mapping, GLintptr offset, GLsizeiptr length,
                           const std::uint8_t* mirror, const std::uint8_t* driver) {
    if (mapping.id == 0) return;
    const std::string key = "map-flush-" + std::to_string(mapping.id);
    detail(key.c_str(),
           format("map=%llu offset=%lld length=%lld mirror-fnv=%016llx driver-fnv=%016llx",
                  (unsigned long long)mapping.id, (long long)offset, (long long)length,
                  (unsigned long long)fnv1a(mirror, static_cast<std::uint64_t>(length)),
                  (unsigned long long)fnv1a(driver, static_cast<std::uint64_t>(length))));
}

void gl_diagnose_map_unmap(const GlMapDiagnostic& mapping, std::uint64_t length,
                           const std::uint8_t* mirror, const std::uint8_t* driver) {
    if (mapping.id == 0) return;
    const std::string key = "map-unmap-" + std::to_string(mapping.id);
    detail(key.c_str(),
           format("map=%llu length=%llu mirror-fnv=%016llx driver-fnv=%016llx",
                  (unsigned long long)mapping.id, (unsigned long long)length,
                  (unsigned long long)fnv1a(mirror, length),
                  (unsigned long long)fnv1a(driver, length)));
}

void gl_diagnose_map_collision(HostGl& host, GLenum target,
                               const GlMapDiagnostic& existing) {
    if (!gl_diagnostics_enabled()) return;
    const std::uintptr_t context = host.egl_context();
    const GLuint buffer = current_buffer(host, target);
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    const std::uint64_t number = ++s.map_collisions;
    if (number > 4) return;
    const std::string key = "map-collision-" + std::to_string(number);
    detail(key.c_str(),
           format("context=0x%llx target=0x%x buffer=%u existing-map=%llu "
                  "existing-context=0x%llx existing-buffer=%u",
                  (unsigned long long)context, target, buffer,
                  (unsigned long long)existing.id, (unsigned long long)existing.context,
                  existing.buffer));
}

std::string diff_report(const std::uint8_t* before, const std::uint8_t* after, GLint w, GLint h);
void emit_readback_diff(GlBackend& gl, const char* key, std::vector<std::uint8_t>& before,
                        const GLint* viewport);
void emit_text_box_diff(GlBackend& gl, State& s, std::uint64_t n, std::vector<std::uint8_t>& before,
                        const GLint* viewport);
void emit_coloured_text_diff(GlBackend& gl, const char* key, std::vector<std::uint8_t>& before,
                             const GLint* viewport, const GLint* scissor, GLint program, GLint fb,
                             const std::array<float, 4>& rgba);

void gl_diagnose(HostGl& host, HostGl::Call& call) {
    GlBackend& gl = host.backend();
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.backend_cache = &gl;
    switch (call.index()) {
    case ZB_GL_HC_glCompileShader: {
        const GLuint shader = call.arg(0);
        GLint status = -1;
        gl.glGetShaderiv(shader, kCompileStatus, &status);
        if (status == 1) {
            if (++s.shaders_ok <= 4) {
                const std::string key = "shader-source-" + std::to_string(s.shaders_ok);
                detail(key.c_str(), shader_text(gl, shader, true));
            }
        } else if (++s.shaders_failed == 1) {
            detail("first-shader-failure", format("status=%d log=", status) + shader_text(gl, shader, false));
            detail("first-shader-failure-source", shader_text(gl, shader, true));
        }
        detail("shaders", format("ok=%llu failed=%llu", (unsigned long long)s.shaders_ok,
                                 (unsigned long long)s.shaders_failed), true);
        break;
    }
    case ZB_GL_HC_glLinkProgram: {
        const GLuint program = call.arg(0);
        GLint status = -1;
        gl.glGetProgramiv(program, kLinkStatus, &status);
        if (status == 1) {
            ++s.programs_ok;
        } else if (++s.programs_failed == 1) {
            char log[400] = {};
            GLsizei length = 0;
            gl.glGetProgramInfoLog(program, sizeof log, &length, log);
            if (length < 0 || length >= static_cast<GLsizei>(sizeof log)) length = 0;
            detail("first-link-failure",
                   format("status=%d log=", status) + std::string(log, static_cast<std::size_t>(length)));
        }
        detail("programs", format("ok=%llu failed=%llu", (unsigned long long)s.programs_ok,
                                  (unsigned long long)s.programs_failed), true);
        break;
    }
    case ZB_GL_HC_glUseProgram:
        if (++s.use_programs == 1) detail("first-use-program", format("%u", call.arg(0)));
        break;
    case ZB_GL_HC_glDrawArrays:
    case ZB_GL_HC_glDrawElements: {
        const bool arrays = call.index() == ZB_GL_HC_glDrawArrays;
        const GLenum mode = call.arg(0);
        const GLsizei count = static_cast<GLsizei>(call.arg(arrays ? 2 : 1));
        const std::uint64_t draw = ++s.draws;
        if (count <= 0) ++s.empty_draws;
        if (s.bound_framebuffer != 0) ++s.offscreen_draws;
        if (draw == 1) {
            detail("first-draw-call", arrays ? format("glDrawArrays first=%d count=%d", static_cast<GLint>(call.arg(1)), count)
                                             : format("glDrawElements count=%d type=0x%x indices=0x%x",
                                                      count, call.arg(2), call.arg(3)));
        }
        for (std::uint64_t at : kSampleDraws) {
            if (draw == at) sample(host, draw, mode, count);
        }
        if (s.text_snapshot_pending) {
            s.text_snapshot_pending = false;
            std::uint8_t after[32 * 32 * 4] = {};
            gl.glReadPixels(s.text_snapshot_x, s.text_snapshot_y, 32, 32, kRgba, kUnsignedByte, after);
            const bool changed = std::memcmp(s.text_snapshot_before, after, sizeof after) != 0;
            ++s.text_draws_captured;
            auto pixels4 = [](const std::uint8_t* p) {
                return format("%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
                               p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10],
                               p[11], p[12], p[13], p[14], p[15]);
            };

            GLint program = -1, array_buffer = -1;
            gl.glGetIntegerv(kCurrentProgram, &program);
            gl.glGetIntegerv(kArrayBufferBinding, &array_buffer);
            const GLboolean blend_enabled = gl.glIsEnabled(kBlend);
            GLint blend_src_rgb = -1, blend_dst_rgb = -1, blend_src_alpha = -1, blend_dst_alpha = -1;
            gl.glGetIntegerv(kBlendSrcRgb, &blend_src_rgb);
            gl.glGetIntegerv(kBlendDstRgb, &blend_dst_rgb);
            gl.glGetIntegerv(kBlendSrcAlpha, &blend_src_alpha);
            gl.glGetIntegerv(kBlendDstAlpha, &blend_dst_alpha);
            GLboolean mask[4] = {9, 9, 9, 9};
            gl.glGetBooleanv(kColorWritemask, mask);
            GLint scissor[4] = {0, 0, 0, 0};
            gl.glGetIntegerv(kScissorBox, scissor);
            const GLboolean depth_enabled = gl.glIsEnabled(kDepthTest);
            const GLboolean stencil_enabled = gl.glIsEnabled(kStencilTest);

            const std::string key = "onscreen-text-" + std::to_string(s.text_draws_captured);
            detail(key.c_str(),
                   format("program=%d vertices=%d mode=0x%x region=%d,%d 32x32 texture=%u "
                          "blend=%d blend-src-alpha=0x%x blend-dst-alpha=0x%x blend-src-rgb=0x%x "
                          "blend-dst-rgb=0x%x colormask=%d%d%d%d scissor=%d,%d,%dx%d depth=%d "
                          "stencil=%d array-buffer=%d ",
                          program, count, mode, s.text_snapshot_x, s.text_snapshot_y,
                          s.glyph_atlas_texture, blend_enabled, blend_src_alpha, blend_dst_alpha,
                          blend_src_rgb, blend_dst_rgb, mask[0], mask[1], mask[2], mask[3],
                          scissor[0], scissor[1], scissor[2], scissor[3], depth_enabled,
                          stencil_enabled, array_buffer) +
                       "before=" + pixels4(s.text_snapshot_before) + " after=" + pixels4(after) +
                       format(" changed=%s", changed ? "yes" : "no"));
        }
        if (s.text_fullfb_pending) {
            s.text_fullfb_pending = false;
            ++s.text_fullfb_done;
            const std::string key = "text-diff-" + std::to_string(s.text_fullfb_done);
            emit_readback_diff(gl, key.c_str(), s.text_fullfb_before, s.text_fullfb_viewport);
        }
        if (s.text_box_pending) {
            s.text_box_pending = false;
            ++s.text_box_done;
            emit_text_box_diff(gl, s, s.text_box_done, s.text_box_before, s.text_box_viewport);
        }
        if (s.nontext_diff_pending) {
            s.nontext_diff_pending = false;
            s.nontext_diff_done = true;
            emit_readback_diff(gl, "nontext-diff-1", s.nontext_diff_before, s.nontext_diff_viewport);
        }
        if (s.coloured_text_fb0_pending) {
            s.coloured_text_fb0_pending = false;
            ++s.coloured_text_fb0_done;
            const std::string key = "coloured-text-" + std::to_string(s.coloured_text_fb0_done);
            emit_coloured_text_diff(gl, key.c_str(), s.coloured_text_fb0_before,
                                    s.coloured_text_fb0_viewport, s.coloured_text_fb0_scissor,
                                    s.coloured_text_fb0_program, s.coloured_text_fb0_fb,
                                    s.coloured_text_fb0_rgba);
        }
        if (s.coloured_text_off_pending) {
            s.coloured_text_off_pending = false;
            ++s.coloured_text_off_done;
            const std::string key = "coloured-text-offscreen-" + std::to_string(s.coloured_text_off_done);
            emit_coloured_text_diff(gl, key.c_str(), s.coloured_text_off_before,
                                    s.coloured_text_off_viewport, s.coloured_text_off_scissor,
                                    s.coloured_text_off_program, s.coloured_text_off_fb,
                                    s.coloured_text_off_rgba);
        }
        if (draw == 1 || draw % 256 == 0) {
            detail("atlas-draws",
                   format("total=%llu fb0=%llu onscreen-first-frame=%llu",
                          (unsigned long long)s.atlas_draws_total, (unsigned long long)s.atlas_draws_fb0,
                          (unsigned long long)s.atlas_draws_onscreen),
                   true);
        }
        if (draw == 20000 && s.atlas_draws_onscreen == 0) {
            detail("atlas-draws-onscreen", "none");
        }
        if (draw == 20000 && !s.coloured_text_off_seen) {
            detail("coloured-text-offscreen", "none");
        }
        if (draw == 1 || draw % 256 == 0) {
            detail("draws", format("total=%llu empty=%llu offscreen=%llu clears=%llu",
                                   (unsigned long long)s.draws, (unsigned long long)s.empty_draws,
                                   (unsigned long long)s.offscreen_draws, (unsigned long long)s.clears),
                   true);
        }
        break;
    }
    case ZB_GL_HC_glClear:
        if (++s.clears == 1) detail("first-clear-mask", format("0x%x", call.arg(0)));
        if ((s.clears == 1 || s.clears == 200) && s.bound_framebuffer == 0) {
            std::uint8_t rgba[4] = {};
            GLint viewport[4] = {0, 0, 0, 0};
            gl.glGetIntegerv(kViewport, viewport);
            gl.glReadPixels(viewport[0] + viewport[2] / 2, viewport[1] + viewport[3] / 2, 1, 1, kRgba,
                            kUnsignedByte, rgba);
            const std::string key = "clear-" + std::to_string(s.clears) + "-center-pixel";
            detail(key.c_str(), format("%02x%02x%02x%02x", rgba[0], rgba[1], rgba[2], rgba[3]));
        }
        break;
    case ZB_GL_HC_glShaderSource:
        ++s.shader_sources;
        break;
    case ZB_GL_HC_glTexSubImage2D: {
        if (++s.tex_sub_images == 1) {
            detail("first-texsubimage", format("level=%d at=%d,%d %dx%d format=0x%x type=0x%x pixels=0x%x",
                                               static_cast<GLint>(call.arg(1)), static_cast<GLint>(call.arg(2)),
                                               static_cast<GLint>(call.arg(3)), static_cast<GLint>(call.arg(4)),
                                               static_cast<GLint>(call.arg(5)), call.arg(6), call.arg(7), call.arg(8)));
        }
        detail("texsubimages", std::to_string(s.tex_sub_images), true);

        const GLenum target = call.arg(0);
        const GLint level = static_cast<GLint>(call.arg(1));
        const GLint xoffset = static_cast<GLint>(call.arg(2));
        const GLint yoffset = static_cast<GLint>(call.arg(3));
        const GLint width = static_cast<GLint>(call.arg(4));
        const GLint height = static_cast<GLint>(call.arg(5));
        const GLenum sub_format = call.arg(6);
        const GLenum type = call.arg(7);
        const std::uint32_t pixels = call.arg(8);

        GLint pbo = 0, texture = 0;
        gl.glGetIntegerv(kPixelUnpackBufferBinding, &pbo);
        gl.glGetIntegerv(kTextureBinding2d, &texture);
        const bool pbo_bound = pbo != 0;

        const GLint channels = channels_for_format(sub_format);
        const std::uint64_t bytes = (!pbo_bound && width > 0 && height > 0 && type == kUnsignedByte &&
                                     channels > 0)
                                        ? std::uint64_t(width) * std::uint64_t(height) *
                                              static_cast<std::uint64_t>(channels)
                                        : 0;
        const std::uint8_t* data =
            bytes != 0 ? host.runtime().memory().host_ptr(pixels, bytes, kPageRead) : nullptr;
        std::uint64_t nonzero = 0;
        for (std::uint64_t i = 0; data != nullptr && i < bytes; ++i) nonzero += data[i] != 0;

        if (s.texsub_details < 12) {
            ++s.texsub_details;
            std::string head;
            for (std::uint64_t i = 0; data != nullptr && i < std::min<std::uint64_t>(bytes, 12); ++i) {
                head += format("%02x", data[i]);
            }
            // The thread matters: Flutter uploads on its resource context and draws on the render
            // context, which only works when the two contexts share objects.
            const std::string key = "texsub-" + std::to_string(s.texsub_details) + "-tid" +
                                    std::to_string(::syscall(SYS_gettid));
            detail(key.c_str(),
                   format("target=0x%x level=%d x=%d y=%d %dx%d format=0x%x type=0x%x pbo=%d texture=%d "
                          "readable=%d nonzero=%llu/%llu head=",
                          target, level, xoffset, yoffset, width, height, sub_format, type, pbo_bound ? 1 : 0,
                          texture, data != nullptr ? 1 : 0, (unsigned long long)nonzero,
                          (unsigned long long)bytes) +
                       head);
        }

        if (s.glyph_atlas_texture != 0 && texture == static_cast<GLint>(s.glyph_atlas_texture)) {
            ++s.glyph_uploads;
            s.glyph_bytes += bytes;
            s.glyph_nonzero += nonzero;
            detail("glyph-atlas",
                   format("uploads=%llu bytes=%llu nonzero=%llu texture=%u", (unsigned long long)s.glyph_uploads,
                          (unsigned long long)s.glyph_bytes, (unsigned long long)s.glyph_nonzero,
                          s.glyph_atlas_texture),
                   true);
        }
        break;
    }
    case ZB_GL_HC_glTexParameteri:
    case ZB_GL_HC_glTexParameterf: {
        const bool is_float = call.index() == ZB_GL_HC_glTexParameterf;
        if (++s.tex_parameters <= 8) {
            const std::string key = "texparameter-" + std::to_string(s.tex_parameters);
            detail(key.c_str(), is_float ? format("f target=0x%x pname=0x%x param=%g", call.arg(0), call.arg(1),
                                                  static_cast<double>(call.scalar<GLfloat>(2)))
                                         : format("i target=0x%x pname=0x%x param=0x%x", call.arg(0), call.arg(1),
                                                  call.arg(2)));
        }
        break;
    }
    case ZB_GL_HC_glGenerateMipmap:
        detail("generate-mipmap", std::to_string(++s.mipmaps), true);
        break;
    case ZB_GL_HC_glUniform1i:
        if (++s.uniform_ints <= 3) {
            const std::string key = "uniform1i-" + std::to_string(s.uniform_ints);
            detail(key.c_str(), format("location=%d value=%d", static_cast<GLint>(call.arg(0)),
                                       static_cast<GLint>(call.arg(1))));
        }
        break;
    case ZB_GL_HC_glPixelStorei:
        if (++s.pixel_stores <= 3) {
            const std::string key = "pixelstore-" + std::to_string(s.pixel_stores);
            detail(key.c_str(), format("pname=0x%x param=%d", call.arg(0), static_cast<GLint>(call.arg(1))));
        }
        break;
    case ZB_GL_HC_glClearColor:
        detail("last-clear-color", format("%g %g %g %g", static_cast<double>(call.scalar<GLfloat>(0)),
                                          static_cast<double>(call.scalar<GLfloat>(1)),
                                          static_cast<double>(call.scalar<GLfloat>(2)),
                                          static_cast<double>(call.scalar<GLfloat>(3))), true);
        break;
    case ZB_GL_HC_glBindFramebuffer:
        s.bound_framebuffer = call.arg(1);
        if (s.bound_framebuffer != 0 && ++s.fb_binds_nonzero <= 4) {
            detail("framebuffer-binds-nonzero", format("%llu last=%u", (unsigned long long)s.fb_binds_nonzero,
                                                      s.bound_framebuffer), true);
        }
        break;
    case ZB_GL_HC_glScissor:
        detail("last-scissor", format("%d,%d,%dx%d", static_cast<GLint>(call.arg(0)), static_cast<GLint>(call.arg(1)),
                                      static_cast<GLint>(call.arg(2)), static_cast<GLint>(call.arg(3))), true);
        break;
    case ZB_GL_HC_glTexImage2D:
        if (!s.legacy_texture_error_checked &&
            (call.arg(2) == kAlpha || call.arg(2) == kLuminance ||
             call.arg(2) == kLuminanceAlpha)) {
            s.legacy_texture_error_checked = true;
            const GLenum error = gl.glGetError();
            detail("legacy-texture-error",
                   format("internal=0x%x format=0x%x error=0x%x", call.arg(2), call.arg(6),
                          error));
            // Diagnostics must not consume an error that guest glGetError would have observed.
            if (error != 0) gl.set_error(error);
        }
        if (call.arg(8) != 0 && ++s.tex_uploads <= 3) {
            const GLint width = static_cast<GLint>(call.arg(3));
            const GLint height = static_cast<GLint>(call.arg(4));
            const std::uint64_t bytes = (width > 0 && height > 0 && call.arg(7) == kUnsignedByte)
                                            ? std::uint64_t(width) * std::uint64_t(height) *
                                                  (call.arg(6) == kRgba ? 4 : 3)
                                            : 0;
            const std::uint8_t* data = bytes != 0 ? host.runtime().memory().host_ptr(call.arg(8), bytes, kPageRead)
                                                  : nullptr;
            std::uint64_t nonzero = 0;
            for (std::uint64_t i = 0; data != nullptr && i < bytes; ++i) nonzero += data[i] != 0;
            std::string head;
            for (std::uint64_t i = 0; data != nullptr && i < std::min<std::uint64_t>(bytes, 16); ++i) {
                head += format("%02x", data[i]);
            }
            const std::string key = "texupload-" + std::to_string(s.tex_uploads);
            detail(key.c_str(), format("level=%d internal=0x%x %dx%d format=0x%x type=0x%x pixels=0x%x "
                                       "readable=%d nonzero-bytes=%llu/%llu head=",
                                       static_cast<GLint>(call.arg(1)), call.arg(2), width, height, call.arg(6),
                                       call.arg(7), call.arg(8), data != nullptr ? 1 : 0,
                                       (unsigned long long)nonzero, (unsigned long long)bytes) + head);
        }
        if (s.glyph_atlas_texture == 0) {
            const GLint width = static_cast<GLint>(call.arg(3));
            const GLint height = static_cast<GLint>(call.arg(4));
            const GLenum internal = call.arg(2);
            if (width == 4096 && height == 1024 && (internal == kAlpha || internal == kR8)) {
                GLint texture = 0;
                gl.glGetIntegerv(kTextureBinding2d, &texture);
                if (texture != 0) s.glyph_atlas_texture = static_cast<GLuint>(texture);
            }
        }
        detail("teximages", std::to_string(s.tex_images + 1), true);
        if (++s.tex_images <= 2) {
            const std::string key = "teximage-" + std::to_string(s.tex_images);
            detail(key.c_str(), format("target=0x%x level=%d internal=0x%x %dx%d format=0x%x type=0x%x pixels=0x%x",
                                       call.arg(0), static_cast<GLint>(call.arg(1)), call.arg(2),
                                       static_cast<GLint>(call.arg(3)), static_cast<GLint>(call.arg(4)),
                                       call.arg(6), call.arg(7), call.arg(8)));
        }
        break;
    case ZB_GL_HC_glVertexAttribPointer:
        if (call.arg(0) < 8) {
            Attrib& attrib = s.attribs[call.arg(0)];
            attrib.set = true;
            attrib.size = static_cast<GLint>(call.arg(1));
            attrib.type = call.arg(2);
            attrib.stride = static_cast<GLint>(call.arg(4));
            attrib.pointer = call.arg(5);
        }
        if (++s.attrib_pointers <= 3) {
            const std::string key = "attrib-pointer-" + std::to_string(s.attrib_pointers);
            detail(key.c_str(), format("index=%u size=%d type=0x%x normalized=%u stride=%d pointer=0x%x",
                                       call.arg(0), static_cast<GLint>(call.arg(1)), call.arg(2),
                                       call.arg(3) & 0xff, static_cast<GLint>(call.arg(4)), call.arg(5)));
        }
        break;
    case ZB_GL_HC_glBufferData:
        if (++s.buffer_datas <= 2) {
            const std::string key = "buffer-data-" + std::to_string(s.buffer_datas);
            const std::uint32_t size = call.arg(1);
            detail(key.c_str(), format("target=0x%x size=%u usage=0x%x floats=", call.arg(0), size, call.arg(3)) +
                                    float_words(host, call.arg(2), size >= 32 ? 8 : size / 4));
        }
        record_buffer_upload(host, call, s, false);
        record_ubo_buffer(host, call, s, false);
        break;
    case ZB_GL_HC_glBufferSubData:
        record_buffer_upload(host, call, s, true);
        record_ubo_buffer(host, call, s, true);
        break;
    case ZB_GL_HC_glBindBufferBase:
    case ZB_GL_HC_glBindBufferRange: {
        if (s.ubo_bind_calls < 8) {
            ++s.ubo_bind_calls;
            const bool is_range = call.index() == ZB_GL_HC_glBindBufferRange;
            const GLuint index = call.arg(1);
            const GLuint buffer = call.arg(2);
            const std::uint32_t offset_raw = is_range ? call.arg(3) : 0;
            const std::uint32_t size_raw = is_range ? call.arg(4) : 0;
            // GLintptr/GLsizeiptr are 32-bit in the guest, 64-bit on the host; this is exactly
            // the sign-extending conversion the generated dispatcher applies via call.scalar<T>
            // before calling the driver, so it shows what actually reached glBindBufferRange.
            const std::int64_t offset_passed =
                is_range ? static_cast<std::int64_t>(call.scalar<GLintptr>(3)) : 0;
            const std::int64_t size_passed =
                is_range ? static_cast<std::int64_t>(call.scalar<GLsizeiptr>(4)) : 0;
            const std::string key = "ubo-bind-" + std::to_string(s.ubo_bind_calls);
            detail(key.c_str(),
                   format("fn=%s index=%u buffer=%u offset-raw32=0x%x offset-passed64=%lld "
                          "size-raw32=0x%x size-passed64=%lld",
                          is_range ? "glBindBufferRange" : "glBindBufferBase", index, buffer,
                          offset_raw, (long long)offset_passed, size_raw, (long long)size_passed));
            if (is_range) record_bound_ubo_data(call, s, s.ubo_bind_calls);
        }
        break;
    }
    case ZB_GL_HC_glUniformBlockBinding:
    case ZB_GL_HC_glGetUniformBlockIndex: {
        if (s.uniform_block_calls < 8) {
            ++s.uniform_block_calls;
            const bool is_binding = call.index() == ZB_GL_HC_glUniformBlockBinding;
            const GLuint program = call.arg(0);
            std::string extra;
            if (is_binding) {
                extra = format("block-index=%u binding=%u", call.arg(1), call.arg(2));
            } else {
                extra = "name=" + guest_short_string(host, call.arg(1));
            }
            const std::string key = "uniform-block-" + std::to_string(s.uniform_block_calls);
            detail(key.c_str(), format("fn=%s program=%u ",
                                       is_binding ? "glUniformBlockBinding" : "glGetUniformBlockIndex",
                                       program) +
                                     extra);
        }
        break;
    }
    case ZB_GL_HC_glCompressedTexImage2D:
    case ZB_GL_HC_glCompressedTexSubImage2D: {
        if (s.compressed_calls < 4) {
            ++s.compressed_calls;
            const bool is_sub = call.index() == ZB_GL_HC_glCompressedTexSubImage2D;
            const std::string key = "compressed-" + std::to_string(s.compressed_calls);
            if (is_sub) {
                detail(key.c_str(), format("glCompressedTexSubImage2D format=0x%x %dx%d imageSize=%u",
                                           call.arg(6), static_cast<GLint>(call.arg(4)),
                                           static_cast<GLint>(call.arg(5)), call.arg(7)));
            } else {
                detail(key.c_str(), format("glCompressedTexImage2D internal=0x%x %dx%d imageSize=%u",
                                           call.arg(2), static_cast<GLint>(call.arg(3)),
                                           static_cast<GLint>(call.arg(4)), call.arg(6)));
            }
        }
        break;
    }
    case ZB_GL_HC_glUniformMatrix4fv:
        if (++s.matrices == 1) {
            detail("first-matrix4", format("location=%d count=%d transpose=%u values=",
                                           static_cast<GLint>(call.arg(0)), static_cast<GLint>(call.arg(1)),
                                           call.arg(2) & 0xff) +
                                        float_words(host, call.arg(3), 16));
        }
        break;
    default:
        break;
    }
}

// Compares two RGBA8 w*h buffers and formats "pixels-changed=N box=x,y,WxH examples=...".
// Up to 3 example pixels are reported as "(x,y)=before->after".
std::string diff_report(const std::uint8_t* before, const std::uint8_t* after, GLint w, GLint h) {
    std::uint64_t changed = 0;
    GLint min_x = w, min_y = h, max_x = -1, max_y = -1;
    struct Example { GLint x, y; std::uint8_t before[4]; std::uint8_t after[4]; };
    Example examples[3];
    std::uint32_t example_count = 0;
    for (GLint y = 0; y < h; ++y) {
        for (GLint x = 0; x < w; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                     static_cast<std::size_t>(x)) * 4;
            if (std::memcmp(before + idx, after + idx, 4) == 0) continue;
            ++changed;
            if (x < min_x) min_x = x;
            if (y < min_y) min_y = y;
            if (x > max_x) max_x = x;
            if (y > max_y) max_y = y;
            if (example_count < 3) {
                Example& e = examples[example_count++];
                e.x = x;
                e.y = y;
                std::memcpy(e.before, before + idx, 4);
                std::memcpy(e.after, after + idx, 4);
            }
        }
    }
    if (changed == 0) return "pixels-changed=0 box=none examples=none";
    std::string result = format("pixels-changed=%llu box=%d,%d %dx%d examples=", (unsigned long long)changed,
                                min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
    for (std::uint32_t i = 0; i < example_count; ++i) {
        const Example& e = examples[i];
        if (i != 0) result += " ";
        result += format("(%d,%d)=%02x%02x%02x%02x->%02x%02x%02x%02x", e.x, e.y, e.before[0], e.before[1],
                         e.before[2], e.before[3], e.after[0], e.after[1], e.after[2], e.after[3]);
    }
    return result;
}

// Reads back the same viewport rect that was captured into `before`, diffs it and emits a
// "gl-<key>: ..." detail line, then frees `before`. On a glReadPixels/driver error the line
// records that instead of a (misleading) zero-changed result.
void emit_readback_diff(GlBackend& gl, const char* key, std::vector<std::uint8_t>& before,
                        const GLint* viewport) {
    const GLint w = viewport[2];
    const GLint h = viewport[3];
    std::vector<std::uint8_t> after(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    gl.glReadPixels(viewport[0], viewport[1], w, h, kRgba, kUnsignedByte, after.data());
    const GLenum error = gl.glGetError();
    if (error != 0) {
        detail(key, format("readback-failed glGetError=0x%x", error));
        // Diagnostics must not consume an error the guest glGetError would have observed.
        gl.set_error(error);
    } else {
        detail(key, diff_report(before.data(), after.data(), w, h));
    }
    std::vector<std::uint8_t>().swap(before);
}

// Single pass over a before/after RGBA8 pair: total pixels changed and their bounding box, no
// example pixels. Cheaper than diff_report() and meant to run on every one of the first 8
// onscreen glyph-atlas draws.
void box_only_diff(const std::uint8_t* before, const std::uint8_t* after, GLint w, GLint h,
                   std::uint64_t& changed, GLint& min_x, GLint& min_y, GLint& max_x, GLint& max_y) {
    changed = 0;
    min_x = w;
    min_y = h;
    max_x = -1;
    max_y = -1;
    for (GLint y = 0; y < h; ++y) {
        for (GLint x = 0; x < w; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                     static_cast<std::size_t>(x)) * 4;
            if (std::memcmp(before + idx, after + idx, 4) == 0) continue;
            ++changed;
            if (x < min_x) min_x = x;
            if (y < min_y) min_y = y;
            if (x > max_x) max_x = x;
            if (y > max_y) max_y = y;
        }
    }
}

// Emits "gl-text-box-N: pixels-changed=... box=x,y,WxH" for one of the first 8 onscreen
// glyph-atlas draws, and folds the box into the running "gl-text-box-union" line.
void emit_text_box_diff(GlBackend& gl, State& s, std::uint64_t n, std::vector<std::uint8_t>& before,
                        const GLint* viewport) {
    const GLint w = viewport[2];
    const GLint h = viewport[3];
    const std::string key = "text-box-" + std::to_string(n);
    std::vector<std::uint8_t> after(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    gl.glReadPixels(viewport[0], viewport[1], w, h, kRgba, kUnsignedByte, after.data());
    const GLenum error = gl.glGetError();
    if (error != 0) {
        detail(key.c_str(), format("readback-failed glGetError=0x%x", error));
        gl.set_error(error);
        std::vector<std::uint8_t>().swap(before);
        return;
    }
    std::uint64_t changed = 0;
    GLint min_x = 0, min_y = 0, max_x = -1, max_y = -1;
    box_only_diff(before.data(), after.data(), w, h, changed, min_x, min_y, max_x, max_y);
    std::vector<std::uint8_t>().swap(before);
    if (changed == 0) {
        detail(key.c_str(), "pixels-changed=0 box=none");
        return;
    }
    detail(key.c_str(), format("pixels-changed=%llu box=%d,%d %dx%d", (unsigned long long)changed,
                               min_x, min_y, max_x - min_x + 1, max_y - min_y + 1));
    if ((n == 1 || n == 2) && !s.text_box_first_valid[n - 1]) {
        s.text_box_first_valid[n - 1] = true;
        s.text_box_first_x[n - 1] = viewport[0] + min_x;
        s.text_box_first_y[n - 1] = viewport[1] + min_y;
        s.text_box_first_w[n - 1] = max_x - min_x + 1;
        s.text_box_first_h[n - 1] = max_y - min_y + 1;
    }
    if (!s.text_box_union_valid) {
        s.text_box_union_valid = true;
        s.text_box_union_min_x = min_x;
        s.text_box_union_min_y = min_y;
        s.text_box_union_max_x = max_x;
        s.text_box_union_max_y = max_y;
    } else {
        s.text_box_union_min_x = std::min(s.text_box_union_min_x, min_x);
        s.text_box_union_min_y = std::min(s.text_box_union_min_y, min_y);
        s.text_box_union_max_x = std::max(s.text_box_union_max_x, max_x);
        s.text_box_union_max_y = std::max(s.text_box_union_max_y, max_y);
    }
    detail("text-box-union",
           format("%d,%d %dx%d", s.text_box_union_min_x, s.text_box_union_min_y,
                  s.text_box_union_max_x - s.text_box_union_min_x + 1,
                  s.text_box_union_max_y - s.text_box_union_min_y + 1),
           true);
}

// Emits "gl-coloured-text[-offscreen]-N: rgba=... program=... fb=... viewport=WxH
// scissor=x,y,WxH pixels-changed=N box=x,y,WxH" for a glyph-shader draw selected by FragInfo
// colour (not white, not degenerate) rather than draw order. Same before/after whole-region
// readback and box_only_diff() as emit_text_box_diff(), but with the colour/program/framebuffer
// header this task asked for and no "examples=" tail.
void emit_coloured_text_diff(GlBackend& gl, const char* key, std::vector<std::uint8_t>& before,
                             const GLint* viewport, const GLint* scissor, GLint program, GLint fb,
                             const std::array<float, 4>& rgba) {
    const GLint w = viewport[2];
    const GLint h = viewport[3];
    const std::string prefix =
        format("rgba=%.4g,%.4g,%.4g,%.4g program=%d fb=%d viewport=%dx%d scissor=%d,%d %dx%d ",
               static_cast<double>(rgba[0]), static_cast<double>(rgba[1]), static_cast<double>(rgba[2]),
               static_cast<double>(rgba[3]), program, fb, w, h, scissor[0], scissor[1], scissor[2],
               scissor[3]);
    std::vector<std::uint8_t> after(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    gl.glReadPixels(viewport[0], viewport[1], w, h, kRgba, kUnsignedByte, after.data());
    const GLenum error = gl.glGetError();
    if (error != 0) {
        detail(key, prefix + format("readback-failed glGetError=0x%x", error));
        gl.set_error(error);
        std::vector<std::uint8_t>().swap(before);
        return;
    }
    std::uint64_t changed = 0;
    GLint min_x = 0, min_y = 0, max_x = -1, max_y = -1;
    box_only_diff(before.data(), after.data(), w, h, changed, min_x, min_y, max_x, max_y);
    std::vector<std::uint8_t>().swap(before);
    if (changed == 0) {
        detail(key, prefix + "pixels-changed=0 box=none");
        return;
    }
    detail(key, prefix + format("pixels-changed=%llu box=%d,%d %dx%d", (unsigned long long)changed,
                                min_x, min_y, max_x - min_x + 1, max_y - min_y + 1));
}

// Decodes up to `max_floats` little-endian floats at `offset` bytes into the shadow copy of
// uniform-buffer-bound `buffer`, from the same glBufferData/glBufferSubData shadow used by
// record_bound_ubo_data(). Returns "(no-shadow)"/"(partially-known)" when the bytes were never
// captured (e.g. uploaded before diagnostics started tracking that buffer).
std::string shadow_floats(const State& s, GLint buffer, GLint offset, std::uint32_t max_floats) {
    if (buffer <= 0 || offset < 0) return "(no-buffer)";
    const auto found = s.buffer_snapshots.find(static_cast<GLuint>(buffer));
    if (found == s.buffer_snapshots.end()) return "(no-shadow)";
    const State::BufferSnapshot& snapshot = found->second;
    const std::uint64_t o = static_cast<std::uint64_t>(offset);
    if (o >= snapshot.bytes.size()) return "(offset-beyond-shadow)";
    const std::uint64_t avail_bytes = snapshot.bytes.size() - o;
    const std::uint32_t words = static_cast<std::uint32_t>(std::min<std::uint64_t>(max_floats, avail_bytes / 4));
    if (words == 0) return "(none)";
    for (std::uint64_t i = 0; i < static_cast<std::uint64_t>(words) * 4; ++i) {
        if (!snapshot.known[o + i]) return "(partially-known)";
    }
    std::string out = "[";
    for (std::uint32_t i = 0; i < words; ++i) {
        float value;
        std::memcpy(&value, snapshot.bytes.data() + o + 4 * i, 4);
        out += format(i == 0 ? "%g" : " %g", static_cast<double>(value));
    }
    out += "]";
    return out;
}

// Decoded FragInfo fields, matching the [is_color_glyph, use_text_color, pad, pad, r, g, b, a]
// layout already established by the text-fraginfo-N lines. `known` is false when any of the 8
// floats were never captured in the buffer shadow (same condition shadow_floats() checks).
struct FragInfoFields {
    bool known = false;
    float is_color_glyph = 0.0f;
    float use_text_color = 0.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
};

FragInfoFields decode_text_fraginfo(const State& s, GLint buffer, GLint offset) {
    FragInfoFields out;
    if (buffer <= 0 || offset < 0) return out;
    const auto found = s.buffer_snapshots.find(static_cast<GLuint>(buffer));
    if (found == s.buffer_snapshots.end()) return out;
    const State::BufferSnapshot& snapshot = found->second;
    const std::uint64_t o = static_cast<std::uint64_t>(offset);
    const std::uint64_t need = 8 * 4;
    if (o + need > snapshot.bytes.size()) return out;
    for (std::uint64_t i = 0; i < need; ++i) {
        if (!snapshot.known[o + i]) return out;
    }
    float vals[8];
    std::memcpy(vals, snapshot.bytes.data() + o, sizeof vals);
    out.known = true;
    out.is_color_glyph = vals[0];
    out.use_text_color = vals[1];
    out.r = vals[4];
    out.g = vals[5];
    out.b = vals[6];
    out.a = vals[7];
    return out;
}

// Records the FragInfo color for the first 32 onscreen glyph-atlas draws, then updates a running
// summary: how many of those draws are white, black or something else. Answers "does EVERY text
// draw use white, or only some" without any framebuffer readback.
void record_text_color(GlBackend& gl, State& s, std::uint64_t n) {
    GLint buffer = 0, offset = 0, size = 0;
    gl.glGetIntegeri_v(kUniformBufferBinding, 0, &buffer);
    gl.glGetIntegeri_v(kUniformBufferStart, 0, &offset);
    gl.glGetIntegeri_v(kUniformBufferSize, 0, &size);
    GLint program = -1;
    gl.glGetIntegerv(kCurrentProgram, &program);

    const FragInfoFields f = decode_text_fraginfo(s, buffer, offset);
    const std::string rgba_str =
        f.known ? format("%.4g,%.4g,%.4g,%.4g", static_cast<double>(f.r), static_cast<double>(f.g),
                          static_cast<double>(f.b), static_cast<double>(f.a))
                : std::string("(unknown)");
    detail(("text-color-" + std::to_string(n)).c_str(),
           format("is_color_glyph=%.4g use_text_color=%.4g rgba=", static_cast<double>(f.is_color_glyph),
                  static_cast<double>(f.use_text_color)) +
               rgba_str + format(" program=%d offset=%d size=%d", program, offset, size));

    ++s.text_color_draws;
    if (f.known) {
        const bool white = f.r >= 0.99f && f.g >= 0.99f && f.b >= 0.99f && f.a >= 0.99f;
        const bool black = f.r <= 0.01f && f.g <= 0.01f && f.b <= 0.01f;
        if (white) {
            ++s.text_color_white;
        } else if (black) {
            ++s.text_color_black;
        } else {
            ++s.text_color_other;
        }
        bool seen = false;
        for (const auto& d : s.text_color_distinct) {
            if (std::fabs(d[0] - f.r) < 1e-4f && std::fabs(d[1] - f.g) < 1e-4f &&
                std::fabs(d[2] - f.b) < 1e-4f && std::fabs(d[3] - f.a) < 1e-4f) {
                seen = true;
                break;
            }
        }
        if (!seen && s.text_color_distinct.size() < 6) {
            s.text_color_distinct.push_back({f.r, f.g, f.b, f.a});
        }
    } else {
        ++s.text_color_other;
    }

    std::string distinct_str;
    for (const auto& d : s.text_color_distinct) {
        if (!distinct_str.empty()) distinct_str += " ";
        distinct_str += format("%.4g,%.4g,%.4g,%.4g", static_cast<double>(d[0]), static_cast<double>(d[1]),
                                static_cast<double>(d[2]), static_cast<double>(d[3]));
    }
    detail("text-color-summary",
           format("draws=%llu white=%llu black=%llu other=%llu distinct=",
                  (unsigned long long)s.text_color_draws, (unsigned long long)s.text_color_white,
                  (unsigned long long)s.text_color_black, (unsigned long long)s.text_color_other) +
               distinct_str,
           true);
}

// Records the uniform buffer ranges bound at UBO binding points 0 (FragInfo) and 1 (FrameInfo,
// the vertex transform) at one of the first 3 onscreen glyph-atlas draws.
void record_text_ubo(GlBackend& gl, const State& s, std::uint64_t n) {
    auto describe = [&](GLuint index, std::uint32_t max_floats) {
        GLint buffer = 0, offset = 0, size = 0;
        gl.glGetIntegeri_v(kUniformBufferBinding, index, &buffer);
        gl.glGetIntegeri_v(kUniformBufferStart, index, &offset);
        gl.glGetIntegeri_v(kUniformBufferSize, index, &size);
        return format("buffer=%d offset=%d size=%d m=", buffer, offset, size) +
               shadow_floats(s, buffer, offset, max_floats);
    };
    detail(("text-mvp-" + std::to_string(n)).c_str(), "binding=1 " + describe(1, 16));
    detail(("text-fraginfo-" + std::to_string(n)).c_str(), "binding=0 " + describe(0, 8));
}

// Records the enabled vertex attributes at one of the first 3 onscreen glyph-atlas draws: index,
// size, type, normalized, stride, byte offset and the bound array buffer for each enabled
// attribute. Does not read any buffer contents from the GPU.
void record_text_attribs(GlBackend& gl, std::uint64_t n) {
    std::string line;
    for (GLuint index = 0; index < static_cast<GLuint>(kMaxTrackedVertexAttribs); ++index) {
        GLint enabled = 0;
        gl.glGetVertexAttribiv(index, kAttribEnabled, &enabled);
        if (enabled == 0) continue;
        GLint size = 0, type = 0, normalized = 0, stride = 0, array_buffer = 0;
        gl.glGetVertexAttribiv(index, kVertexAttribArraySize, &size);
        gl.glGetVertexAttribiv(index, kVertexAttribArrayType, &type);
        gl.glGetVertexAttribiv(index, kVertexAttribArrayNormalized, &normalized);
        gl.glGetVertexAttribiv(index, kVertexAttribArrayStride, &stride);
        gl.glGetVertexAttribiv(index, kVertexAttribArrayBufferBinding, &array_buffer);
        void* pointer = nullptr;
        gl.glGetVertexAttribPointerv(index, kVertexAttribArrayPointer, &pointer);
        const std::uint32_t offset = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
        if (!line.empty()) line += "; ";
        line += format("idx=%u size=%d type=0x%x normalized=%d stride=%d offset=%u buffer=%d",
                       index, size, type, normalized, stride, offset, array_buffer);
    }
    if (line.empty()) line = "(none enabled)";
    detail(("text-attrib-" + std::to_string(n)).c_str(), line);
}

void gl_diagnose_before(HostGl& host, HostGl::Call& call) {
    const std::uint32_t index = call.index();
    if (index != ZB_GL_HC_glDrawArrays && index != ZB_GL_HC_glDrawElements) return;
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    GlBackend& gl = host.backend();
    s.backend_cache = &gl;

    GLint active_texture = -1;
    gl.glGetIntegerv(kTextureBinding2d, &active_texture);
    const bool uses_glyph_atlas =
        s.glyph_atlas_texture != 0 && active_texture == static_cast<GLint>(s.glyph_atlas_texture);

    // Control: the first onscreen fb-0 draw that writes colour but does NOT use the glyph
    // atlas texture. If this one shows changed pixels while the text draws below show none,
    // the readback/diff machinery works and the text draw is provably a no-op.
    if (!s.nontext_diff_done && !uses_glyph_atlas) {
        GLint framebuffer = -1;
        gl.glGetIntegerv(kFramebufferBinding, &framebuffer);
        if (framebuffer == 0) {
            GLint viewport[4] = {0, 0, 0, 0};
            gl.glGetIntegerv(kViewport, viewport);
            if (viewport[2] > 64 && viewport[3] > 64) {
                GLboolean mask[4] = {0, 0, 0, 0};
                gl.glGetBooleanv(kColorWritemask, mask);
                if (mask[0] || mask[1] || mask[2] || mask[3]) {
                    std::memcpy(s.nontext_diff_viewport, viewport, sizeof viewport);
                    s.nontext_diff_before.assign(
                        static_cast<std::size_t>(viewport[2]) * static_cast<std::size_t>(viewport[3]) * 4, 0);
                    gl.glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], kRgba, kUnsignedByte,
                                   s.nontext_diff_before.data());
                    s.nontext_diff_pending = true;
                }
            }
        }
    }

    if (!uses_glyph_atlas) return;

    ++s.atlas_draws_total;
    GLint framebuffer = -1;
    gl.glGetIntegerv(kFramebufferBinding, &framebuffer);
    const bool fb0 = framebuffer == 0;
    if (fb0) ++s.atlas_draws_fb0;
    GLint viewport[4] = {0, 0, 0, 0};
    gl.glGetIntegerv(kViewport, viewport);
    const bool onscreen = fb0 && viewport[2] > 64 && viewport[3] > 64;
    if (onscreen) ++s.atlas_draws_onscreen;

    // Colour classification and selection, independent of "onscreen": needs to see every
    // glyph-shader draw, fb0 or offscreen, to answer whether the grey/amber text (which the
    // user never sees) ever reaches a framebuffer at all.
    {
        GLint program = -1;
        gl.glGetIntegerv(kCurrentProgram, &program);
        GLint frag_buffer = 0, frag_offset = 0;
        gl.glGetIntegeri_v(kUniformBufferBinding, 0, &frag_buffer);
        gl.glGetIntegeri_v(kUniformBufferStart, 0, &frag_offset);
        const FragInfoFields f = decode_text_fraginfo(s, frag_buffer, frag_offset);
        const bool white = f.known && f.r >= 0.99f && f.g >= 0.99f && f.b >= 0.99f && f.a >= 0.99f;
        const bool degenerate = f.known && f.r == 0.0f && f.g == 0.0f && f.b == 0.0f && f.a == 0.0f;
        const bool coloured = f.known && !white && !degenerate;

        if (!f.known || degenerate) {
            ++s.text_class_other;
        } else if (white) {
            if (fb0) ++s.text_class_white_fb0; else ++s.text_class_white_off;
        } else {
            if (fb0) ++s.text_class_coloured_fb0; else ++s.text_class_coloured_off;
        }
        detail("text-class",
               format("white-fb0=%llu white-off=%llu coloured-fb0=%llu coloured-off=%llu other=%llu",
                      (unsigned long long)s.text_class_white_fb0, (unsigned long long)s.text_class_white_off,
                      (unsigned long long)s.text_class_coloured_fb0,
                      (unsigned long long)s.text_class_coloured_off, (unsigned long long)s.text_class_other),
               true);

        GLint scissor[4] = {0, 0, 0, 0};
        if (coloured) gl.glGetIntegerv(kScissorBox, scissor);

        if (coloured && onscreen && s.coloured_text_fb0_done < 6 && !s.coloured_text_fb0_pending) {
            std::memcpy(s.coloured_text_fb0_viewport, viewport, sizeof viewport);
            std::memcpy(s.coloured_text_fb0_scissor, scissor, sizeof scissor);
            s.coloured_text_fb0_program = program;
            s.coloured_text_fb0_fb = framebuffer;
            s.coloured_text_fb0_rgba = {f.r, f.g, f.b, f.a};
            s.coloured_text_fb0_before.assign(
                static_cast<std::size_t>(viewport[2]) * static_cast<std::size_t>(viewport[3]) * 4, 0);
            gl.glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], kRgba, kUnsignedByte,
                           s.coloured_text_fb0_before.data());
            s.coloured_text_fb0_pending = true;
        }

        if (coloured && !fb0 && s.coloured_text_off_done < 3 && !s.coloured_text_off_pending &&
            viewport[2] > 0 && viewport[3] > 0 &&
            static_cast<std::uint64_t>(viewport[2]) * static_cast<std::uint64_t>(viewport[3]) <=
                4096ull * 4096ull) {
            std::memcpy(s.coloured_text_off_viewport, viewport, sizeof viewport);
            std::memcpy(s.coloured_text_off_scissor, scissor, sizeof scissor);
            s.coloured_text_off_program = program;
            s.coloured_text_off_fb = framebuffer;
            s.coloured_text_off_rgba = {f.r, f.g, f.b, f.a};
            s.coloured_text_off_before.assign(
                static_cast<std::size_t>(viewport[2]) * static_cast<std::size_t>(viewport[3]) * 4, 0);
            gl.glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], kRgba, kUnsignedByte,
                           s.coloured_text_off_before.data());
            s.coloured_text_off_pending = true;
            s.coloured_text_off_seen = true;
        }
    }

    if (!onscreen) return;

    // Whole-framebuffer before readback, for the first 2 onscreen glyph-atlas draws.
    if (s.text_fullfb_done < 2) {
        std::memcpy(s.text_fullfb_viewport, viewport, sizeof viewport);
        s.text_fullfb_before.assign(
            static_cast<std::size_t>(viewport[2]) * static_cast<std::size_t>(viewport[3]) * 4, 0);
        gl.glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], kRgba, kUnsignedByte,
                       s.text_fullfb_before.data());
        s.text_fullfb_pending = true;
    }

    // Same whole-framebuffer before readback, cheaper on emit (box only, no example pixels), for
    // the first 8 onscreen glyph-atlas draws: answers "do all glyphs land in one small region".
    if (s.text_box_done < 8) {
        std::memcpy(s.text_box_viewport, viewport, sizeof viewport);
        s.text_box_before.assign(
            static_cast<std::size_t>(viewport[2]) * static_cast<std::size_t>(viewport[3]) * 4, 0);
        gl.glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], kRgba, kUnsignedByte,
                       s.text_box_before.data());
        s.text_box_pending = true;
    }

    // FragInfo color classification: cheap (no readback), so cover many more draws than the
    // readback-based captures above.
    if (s.text_color_draws < 32) {
        record_text_color(gl, s, s.text_color_draws + 1);
    }

    if (s.text_draws_captured >= 3) return;

    // The transform and vertex setup actually in effect at this draw: the uniform buffer ranges
    // bound at binding 0/1, decoded from the same shadow used for the ubo-bind-N-data lines, and
    // the enabled vertex attributes' size/type/stride/offset/buffer.
    record_text_ubo(gl, s, s.text_draws_captured + 1);
    record_text_attribs(gl, s.text_draws_captured + 1);

    GLint scissor[4] = {0, 0, 0, 0};
    gl.glGetIntegerv(kScissorBox, scissor);
    GLint x = scissor[0] + scissor[2] / 2 - 16;
    GLint y = scissor[1] + scissor[3] / 2 - 16;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    s.text_snapshot_x = x;
    s.text_snapshot_y = y;
    std::memset(s.text_snapshot_before, 0, sizeof s.text_snapshot_before);
    gl.glReadPixels(x, y, 32, 32, kRgba, kUnsignedByte, s.text_snapshot_before);
    s.text_snapshot_pending = true;
}

namespace {

constexpr char kLumaChars[] = " .:-=+*#%@";
constexpr int kLumaCharCount = 10;  // sizeof(kLumaChars) - 1, spelled out for clang -Wunused.

constexpr int kGlyphMapMaxCols = 96;
constexpr int kGlyphMapMaxRows = 48;

// Renders a w*h RGBA8 region (as returned by glReadPixels: row 0 is the BOTTOM of the region) at
// near-native resolution: one output column per source pixel column (capped at
// kGlyphMapMaxCols, centre-cropping wider regions) and one output row per 2 source pixel rows
// (capped at kGlyphMapMaxRows, centre-cropping taller regions). Meant to make a single ~30x50
// glyph legible, unlike the coarse 64x16 whole-frame map. Same luma ramp as luminance_grid().
void high_res_grid(const std::uint8_t* pixels, GLint w, GLint h, std::vector<std::string>& out_rows) {
    const int col_count = static_cast<int>(std::min<GLint>(w, kGlyphMapMaxCols));
    const GLint col_start = w > kGlyphMapMaxCols ? (w - kGlyphMapMaxCols) / 2 : 0;
    const int row_count = static_cast<int>(std::min<GLint>((h + 1) / 2, kGlyphMapMaxRows));
    const GLint covered_rows = static_cast<GLint>(row_count) * 2;
    const GLint row_start_from_top = covered_rows < h ? (h - covered_rows) / 2 : 0;

    out_rows.assign(static_cast<std::size_t>(row_count), std::string());
    for (int out_row = 0; out_row < row_count; ++out_row) {
        // out_row 0 = top of the region. glReadPixels rows are bottom-to-top, so the row at
        // `top_offset` rows down from the top sits at buffer row (h - 1 - top_offset).
        const GLint top_offset0 = row_start_from_top + static_cast<GLint>(out_row) * 2;
        const GLint top_offset1 = top_offset0 + 1;
        const GLint y0 = h - 1 - top_offset0;
        const GLint y1 = h - 1 - top_offset1;
        std::string line;
        line.resize(static_cast<std::size_t>(col_count));
        for (int out_col = 0; out_col < col_count; ++out_col) {
            const GLint x = col_start + out_col;
            std::uint64_t luma_sum = 0;
            std::uint64_t count = 0;
            for (const GLint y : {y0, y1}) {
                if (y < 0 || y >= h || x < 0 || x >= w) continue;
                const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                         static_cast<std::size_t>(x)) * 4;
                const std::uint8_t r = pixels[idx];
                const std::uint8_t g = pixels[idx + 1];
                const std::uint8_t b = pixels[idx + 2];
                luma_sum += (77u * r + 150u * g + 29u * b) >> 8;
                ++count;
            }
            if (count == 0) {
                line[static_cast<std::size_t>(out_col)] = ' ';
                continue;
            }
            const std::uint64_t luma = luma_sum / count;
            int bucket = static_cast<int>((luma * kLumaCharCount) / 256);
            if (bucket >= kLumaCharCount) bucket = kLumaCharCount - 1;
            line[static_cast<std::size_t>(out_col)] = kLumaChars[bucket];
        }
        out_rows[static_cast<std::size_t>(out_row)] = line;
    }
}

// Emits a high-resolution "gl-frame-map-<suffix>: ..." map plus per-row and per-region stats
// lines for one recorded glyph box, padded by `pad` pixels and clamped to the framebuffer. When
// `box_valid` is false (no such glyph draw recorded yet at this swap), emits a says-so line for
// both the map and the stats instead of reading back anything.
void emit_glyph_box_region(GlBackend& gl, const std::string& map_suffix, const char* stats_key,
                           bool box_valid, GLint box_x, GLint box_y, GLint box_w, GLint box_h,
                           GLint pad, const GLint* viewport) {
    const std::string map_key = "frame-map-" + map_suffix;
    const std::string stats_line_key = std::string("frame-stats-") + stats_key;
    if (!box_valid) {
        detail(map_key.c_str(), "region=none (no glyph box recorded yet)");
        detail(stats_line_key.c_str(), "region=none (no glyph box recorded yet)");
        return;
    }
    GLint x = box_x - pad;
    GLint y = box_y - pad;
    GLint w = box_w + 2 * pad;
    GLint h = box_h + 2 * pad;
    // Clamp to the current framebuffer's viewport.
    const GLint fb_x0 = viewport[0], fb_y0 = viewport[1];
    const GLint fb_x1 = viewport[0] + viewport[2], fb_y1 = viewport[1] + viewport[3];
    if (x < fb_x0) x = fb_x0;
    if (y < fb_y0) y = fb_y0;
    GLint x1 = std::min(x + w, fb_x1);
    GLint y1 = std::min(y + h, fb_y1);
    w = x1 - x;
    h = y1 - y;
    if (w <= 0 || h <= 0) {
        detail(map_key.c_str(), format("region=%d,%d %dx%d (empty after clamp)", x, y, w, h));
        detail(stats_line_key.c_str(), "region=none (empty after clamp)");
        return;
    }
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    gl.glReadPixels(x, y, w, h, kRgba, kUnsignedByte, pixels.data());
    const GLenum error = gl.glGetError();
    if (error != 0) {
        detail(map_key.c_str(),
               format("region=%d,%d %dx%d readback-failed glGetError=0x%x", x, y, w, h, error));
        detail(stats_line_key.c_str(), format("readback-failed glGetError=0x%x", error));
        gl.set_error(error);
        return;
    }
    detail(map_key.c_str(), format("region=%d,%d %dx%d (glyph box=%d,%d %dx%d pad=%d)", x, y, w, h,
                                   box_x, box_y, box_w, box_h, pad));
    std::vector<std::string> rows;
    high_res_grid(pixels.data(), w, h, rows);
    for (std::size_t row = 0; row < rows.size(); ++row) {
        detail((map_key + "-row" + std::to_string(row)).c_str(), rows[row]);
    }
    std::uint8_t min_c[4] = {255, 255, 255, 255};
    std::uint8_t max_c[4] = {0, 0, 0, 0};
    std::uint64_t sum_c[4] = {0, 0, 0, 0};
    const std::uint64_t count = static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h);
    for (std::uint64_t i = 0; i < count; ++i) {
        const std::uint8_t* p = pixels.data() + i * 4;
        for (int c = 0; c < 4; ++c) {
            if (p[c] < min_c[c]) min_c[c] = p[c];
            if (p[c] > max_c[c]) max_c[c] = p[c];
            sum_c[c] += p[c];
        }
    }
    detail(stats_line_key.c_str(),
           format("min=%u,%u,%u,%u max=%u,%u,%u,%u mean=%.1f,%.1f,%.1f,%.1f", min_c[0], min_c[1],
                  min_c[2], min_c[3], max_c[0], max_c[1], max_c[2], max_c[3],
                  count == 0 ? 0.0 : static_cast<double>(sum_c[0]) / static_cast<double>(count),
                  count == 0 ? 0.0 : static_cast<double>(sum_c[1]) / static_cast<double>(count),
                  count == 0 ? 0.0 : static_cast<double>(sum_c[2]) / static_cast<double>(count),
                  count == 0 ? 0.0 : static_cast<double>(sum_c[3]) / static_cast<double>(count)));
}

}  // namespace

void gl_diagnose_swap() {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    const std::uint64_t swap = ++s.swap_count;
    int moment = 0;
    if (swap == 60) moment = 1;
    else if (swap == 200) moment = 2;
    if (moment == 0) return;

    const std::string a_suffix = std::to_string(moment) + "a";
    const std::string b_suffix = std::to_string(moment) + "b";
    const std::string stats_key = std::to_string(moment);

    if (s.backend_cache == nullptr) {
        detail(("frame-map-" + a_suffix).c_str(), "no-gl-backend-seen-yet");
        detail(("frame-map-" + b_suffix).c_str(), "no-gl-backend-seen-yet");
        detail(("frame-stats-" + stats_key).c_str(), "no-gl-backend-seen-yet");
        return;
    }
    GlBackend& gl = *s.backend_cache;

    GLint framebuffer = -1;
    gl.glGetIntegerv(kFramebufferBinding, &framebuffer);
    if (framebuffer != 0) {
        detail(("frame-map-" + a_suffix).c_str(), format("non-default-framebuffer=%d", framebuffer));
        detail(("frame-map-" + b_suffix).c_str(), format("non-default-framebuffer=%d", framebuffer));
        detail(("frame-stats-" + stats_key).c_str(), format("non-default-framebuffer=%d", framebuffer));
        return;
    }
    GLint viewport[4] = {0, 0, 0, 0};
    gl.glGetIntegerv(kViewport, viewport);

    // Region A: the exact box of the first recorded glyph draw (gl-text-box-1), padded 8px on
    // each side, printed at near-native resolution so a single glyph is legible.
    emit_glyph_box_region(gl, a_suffix, a_suffix.c_str(), s.text_box_first_valid[0],
                          s.text_box_first_x[0], s.text_box_first_y[0], s.text_box_first_w[0],
                          s.text_box_first_h[0], 8, viewport);

    // Region B: the exact box of the second recorded glyph draw (gl-text-box-2), same treatment.
    emit_glyph_box_region(gl, b_suffix, b_suffix.c_str(), s.text_box_first_valid[1],
                          s.text_box_first_x[1], s.text_box_first_y[1], s.text_box_first_w[1],
                          s.text_box_first_h[1], 8, viewport);
}

}  // namespace zb
