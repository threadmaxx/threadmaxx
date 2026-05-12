#pragma once

#include <cstdio>
#include <cstdlib>

// Tiny no-dependency test harness. Each test executable defines its own
// `main()` that runs a series of CHECKs, then returns gTestFailures.

inline int gTestFailures = 0;

#define CHECK(expr)                                                           \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "FAIL %s:%d  CHECK(%s)\n",                    \
                         __FILE__, __LINE__, #expr);                           \
            ++gTestFailures;                                                   \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                        \
    do {                                                                       \
        const auto _av = (a);                                                  \
        const auto _bv = (b);                                                  \
        if (!(_av == _bv)) {                                                   \
            std::fprintf(stderr, "FAIL %s:%d  CHECK_EQ(%s, %s)\n",             \
                         __FILE__, __LINE__, #a, #b);                          \
            ++gTestFailures;                                                   \
        }                                                                      \
    } while (0)

#define EXIT_WITH_RESULT()                                                    \
    do {                                                                       \
        if (gTestFailures == 0) {                                              \
            std::printf("OK\n");                                               \
            return 0;                                                          \
        }                                                                      \
        std::fprintf(stderr, "%d failure(s)\n", gTestFailures);                \
        return 1;                                                              \
    } while (0)
