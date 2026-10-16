#pragma once

#include <cstdio>
#include <cstdlib>

// _Exit, not exit: a failed check may run on a worker thread while guest threads still run,
// and exit() would run static destructors under them or hang.
#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::fflush(stderr);                                                      \
            std::_Exit(1);                                                            \
        }                                                                             \
    } while (0)
