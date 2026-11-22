#pragma once
// Shared by every test file: the failure counter, CHECK and the runner with the KE_TEST filter.
// Exit code of ke_tests = number of failures. The list of tests is in main.cpp, their declarations
// in Tests.h.
#include "math/Math.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

using namespace ke;

inline int g_failures = 0;
#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        if (!(cond)) {                                                            \
            ++g_failures;                                                         \
            std::printf("  FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond);         \
            std::printf(__VA_ARGS__);                                             \
            std::printf("\n");                                                    \
        }                                                                         \
    } while (0)

inline void run(const char* name, const std::function<void()>& fn) {
    if (const char* only = std::getenv("KE_TEST"); only && *only && !std::strstr(name, only)) return;
    auto t0 = std::chrono::steady_clock::now();
    int before = g_failures;
    fn();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("[%s] %s (%.0f ms)\n", g_failures == before ? " OK " : "FAIL", name, ms);
    std::fflush(stdout); // piped output is block-buffered: a crash would lose the last tests' lines
}
