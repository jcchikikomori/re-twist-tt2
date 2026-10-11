// Minimal test harness — header-only, no external dependencies (no
// Catch2, no doctest). The engine's tests are short enough to keep
// this in-tree.
//
// Usage:
//
//   #include "test_harness.hpp"
//   #include "tt2/something.hpp"
//
//   TEST(my_test) {
//       ASSERT_TRUE(1 + 1 == 2);
//       ASSERT_EQ(2, 2);
//   }
//
//   // main() is provided by test_main.cpp.

#pragma once

#include <cstdio>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace tt2_test {

struct Test {
    const char* name;
    void (*fn)();
};

inline std::vector<Test>& registry() {
    static std::vector<Test> r;
    return r;
}

struct Register {
    Register(const char* n, void (*f)()) {
        registry().push_back({n, f});
    }
};

struct AssertionFailure : public std::exception {
    std::string msg;
    explicit AssertionFailure(std::string m) : msg(std::move(m)) {}
    const char* what() const noexcept override { return msg.c_str(); }
};

}  // namespace tt2_test

#define TEST(name)                                                              \
    static void name();                                                          \
    static ::tt2_test::Register _reg_##name(#name, &name);                       \
    static void name()

#define ASSERT_TRUE(expr)                                                        \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::ostringstream os;                                               \
            os << __FILE__ << ":" << __LINE__ << " ASSERT_TRUE failed: " << #expr;\
            throw ::tt2_test::AssertionFailure(os.str());                        \
        }                                                                        \
    } while (0)

#define ASSERT_EQ(a, b)                                                          \
    do {                                                                         \
        auto _a = (a); auto _b = (b);                                             \
        if (!(_a == _b)) {                                                       \
            std::ostringstream os;                                               \
            os << __FILE__ << ":" << __LINE__                                    \
               << " ASSERT_EQ failed: " << #a << " (" << _a                      \
               << ") != " << #b << " (" << _b << ")";                            \
            throw ::tt2_test::AssertionFailure(os.str());                        \
        }                                                                        \
    } while (0)

#define ASSERT_NE(a, b)                                                          \
    do {                                                                         \
        auto _a = (a); auto _b = (b);                                             \
        if (!(_a != _b)) {                                                       \
            std::ostringstream os;                                               \
            os << __FILE__ << ":" << __LINE__                                    \
               << " ASSERT_NE failed: " << #a << " (" << _a                      \
               << ") == " << #b << " (" << _b << ")";                            \
            throw ::tt2_test::AssertionFailure(os.str());                        \
        }                                                                        \
    } while (0)

#define ASSERT_THROWS(expr)                                                      \
    do {                                                                         \
        bool _threw = false;                                                      \
        try { (void)(expr); } catch (...) { _threw = true; }                     \
        if (!_threw) {                                                           \
            std::ostringstream os;                                               \
            os << __FILE__ << ":" << __LINE__                                    \
               << " ASSERT_THROWS failed: " << #expr;                            \
            throw ::tt2_test::AssertionFailure(os.str());                        \
        }                                                                        \
    } while (0)

#define ASSERT_NO_THROW(expr)                                                    \
    do {                                                                         \
        try { (void)(expr); }                                                     \
        catch (const std::exception& e) {                                        \
            std::ostringstream os;                                               \
            os << __FILE__ << ":" << __LINE__                                    \
               << " ASSERT_NO_THROW failed: " << #expr                           \
               << " threw \"" << e.what() << "\"";                               \
            throw ::tt2_test::AssertionFailure(os.str());                        \
        }                                                                        \
    } while (0)
