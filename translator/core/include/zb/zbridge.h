#pragma once

// C entry point for embedding the translator in an app process (libzbridge.so).

#ifdef __cplusplus
extern "C" {
#endif

#if defined(ZB_EXPORT_C_API)
#define ZB_API __attribute__((visibility("default")))
#else
#define ZB_API
#endif

// Runs an arm32 Android executable in this process through the translator and returns its
// exit status, or 128 + signal for a fatal guest fault.
//   sysroot  directory holding system/bin/linker and system/lib (arm32 bionic)
//   argv     argv[0] is the path of the executable; argc must be at least 1
//   envp     NULL-terminated guest environment, or NULL to pass the host environment
// A guest exit_group while other guest threads are alive ends the host process, as it would
// end a real Android process; run guests in a dedicated process.
ZB_API int zb_run_executable(const char* sysroot, int argc, const char* const* argv, const char* const* envp);

#ifdef __cplusplus
}
#endif
