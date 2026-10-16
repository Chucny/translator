# Phase 5 acceptance: Orange Roulette draws on the device

Date: 2026-09-17. Device: OnePlus 13 (Snapdragon 8 Elite, no AArch32), launcher debug APK built
from `codex/phase4d-launcher` at `e705006`.

## Result

Orange Roulette 1.0.0 (armeabi, Haxe/OpenFL legacy, GLES 2.0) runs as a plugin:
- the warning screen, the main menu and the gameplay scene render correctly
  (textures, fonts, alpha blending);
- touch input works (menu navigation, entering a game);
- music plays (OpenAL -> guest `AudioTrack` through JNI).

That meets the Phase 5 acceptance ("draws its intro screen") and most of Phase 6 ("playable").
Phase 6 is not formally closed: the full game has not been played start to finish, and the
remaining `AAsset*` functions are still stubs.

## How the black screen was found

The previous build ran the render loop with no GL error and no unimplemented host call, but
showed a black screen. GL visibility diagnostics were added to the runtime report
(`core/src/gl/gl_diagnostics.cpp`, enabled only by the Android runtime):
1. `78759dc`:
   - shader compile and link status;
   - draw counts and framebuffer binds;
   - state and a 3x3 `glReadPixels` grid at draws 1/300/3000/30000.

   Result: shaders fine, drawing to framebuffer 0, correct viewport and matrix, but every
   sampled pixel opaque black while the clear color was 0.2 gray.
2. `1916b51`:
   - texture uploads and parameters, bound textures, vertex data;
   - all shader sources;
   - a readback after `glClear`.

   Result: the clear worked (`313131ff`) and vertices and colors were correct, but only 3
   `glTexImage2D` calls were ever accepted. The sprite textures had no data, so they sampled
   as (0,0,0,1).
3. `e705006`: `gl_pixel_bytes` accepted only core GLES2 formats. The game uploads in
   `GL_BGRA_EXT` (0x80E1), and the guest never calls `glGetError`, so the rejections were
   silent. That commit:
   - accepts `GL_BGRA_EXT`;
   - records every rejected GL call in the report (`gl-rejections`, first 4 with arguments).

   Result: the game renders.

## Lessons

- A GL call rejected by host marshaling must be visible in the report, not only in logcat
  (OxygenOS hides third-party logcat) and `glGetError`.
- Marshaling tables built from the core GLES2 spec must also cover widely used extensions
  (`EXT_texture_format_BGRA8888`, and likely `OES_texture_half_float`, `OES_depth_texture` and
  the compressed texture formats used by other guests).
