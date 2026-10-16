#pragma once

#include <cstdint>

namespace zb {

// Host-call indices for the eight ANativeWindow_* functions HostNativeWindow serves (Phase 7a
// Task 5). Hand-written from tools/gen_stubs.py's ANATIVE_WINDOW list (core/src/gen/hostcalls.inc),
// which is the generated source of truth: if gen_stubs.py's window function list or ordering
// changes, update these to match. The window range is 160-167, right after the AAsset* range
// (core/include/zb/asset_hostcalls.h, 142-160) and before EGL (168+).
inline constexpr std::uint32_t ZB_WINDOW_HC_ANativeWindow_acquire = 160u;
inline constexpr std::uint32_t ZB_WINDOW_HC_ANativeWindow_fromSurface = 161u;
inline constexpr std::uint32_t ZB_WINDOW_HC_ANativeWindow_getFormat = 162u;
inline constexpr std::uint32_t ZB_WINDOW_HC_ANativeWindow_getHeight = 163u;
inline constexpr std::uint32_t ZB_WINDOW_HC_ANativeWindow_getWidth = 164u;
inline constexpr std::uint32_t ZB_WINDOW_HC_ANativeWindow_release = 165u;
inline constexpr std::uint32_t ZB_WINDOW_HC_ANativeWindow_setBuffersGeometry = 166u;
inline constexpr std::uint32_t ZB_WINDOW_HC_ANativeWindow_toSurface = 167u;

}  // namespace zb
