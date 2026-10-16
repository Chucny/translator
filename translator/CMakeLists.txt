cmake_minimum_required(VERSION 3.22)
project(ChucnysTranslator LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dynarmic's bundled fmt 10.1.0 rejects its own compile-time format-string checks under
# clang 21 (NDK r29): "call to consteval function ... is not a constant expression".
# An empty FMT_CONSTEVAL turns those checks into plain constexpr; formatting is unchanged.
add_compile_definitions(FMT_CONSTEVAL=)

if (ANDROID)
    # libzbridge.so exports only its C and JNI entry points; everything else stays internal.
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_C_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
    add_compile_options(-ffunction-sections -fdata-sections)
endif()

set(DYNARMIC_FRONTENDS "A32" CACHE STRING "" FORCE)
set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)
set(DYNARMIC_USE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/dynarmic EXCLUDE_FROM_ALL)

add_subdirectory(core)
add_subdirectory(cli/zbfix)
add_subdirectory(cli/zbrun)

option(ZB_BUILD_TESTS "Build host tests" ON)
if (ZB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/host)
endif()
