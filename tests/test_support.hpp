#pragma once

#include <cstdlib>
#include <iostream>

namespace test_support {

[[noreturn]] inline void fail(
    const char* expression,
    const char* file,
    int line) {
    std::cerr << file << ':' << line
              << ": requirement failed: " << expression << '\n';
    std::abort();
}

}  // namespace test_support

#define TEST_REQUIRE(expression)                                      \
    do {                                                              \
        if (!(expression)) {                                          \
            ::test_support::fail(#expression, __FILE__, __LINE__);    \
        }                                                             \
    } while (false)
