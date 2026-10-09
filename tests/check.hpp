#pragma once

// Minimal dependency-free unit test harness.

#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace ldds_test {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back(Case{name, fn}); }
};

struct Skip {
    std::string reason;
};

struct Fatal {};

inline int& failures() {
    static int count = 0;
    return count;
}

inline void report(const char* file, int line, const std::string& message) {
    ++failures();
    std::cerr << file << ':' << line << ": " << message << '\n';
}

template <class A, class B>
bool equal(const A& a, const B& b) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B> && std::is_signed_v<A> != std::is_signed_v<B>) {
        if constexpr (std::is_signed_v<A>) {
            return a >= 0 && static_cast<unsigned long long>(a) == static_cast<unsigned long long>(b);
        } else {
            return b >= 0 && static_cast<unsigned long long>(a) == static_cast<unsigned long long>(b);
        }
    } else {
        return a == b;
    }
}

template <class A, class B>
void check_eq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line, bool fatal) {
    if (equal(a, b)) return;
    std::ostringstream os;
    os << "CHECK_EQ failed: " << ea << " == " << eb << " (" << a << " vs " << b << ")";
    report(file, line, os.str());
    if (fatal) throw Fatal{};
}

}  // namespace ldds_test

#define TEST(name)                                                  \
    static void name();                                             \
    static ldds_test::Registrar registrar_##name(#name, &name);     \
    static void name()

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) ldds_test::report(__FILE__, __LINE__, "CHECK failed: " #expr); \
    } while (false)

#define REQUIRE(expr)                                                                \
    do {                                                                             \
        if (!(expr)) {                                                               \
            ldds_test::report(__FILE__, __LINE__, "REQUIRE failed: " #expr);         \
            throw ldds_test::Fatal{};                                                \
        }                                                                            \
    } while (false)

#define CHECK_EQ(a, b) ldds_test::check_eq((a), (b), #a, #b, __FILE__, __LINE__, false)
#define REQUIRE_EQ(a, b) ldds_test::check_eq((a), (b), #a, #b, __FILE__, __LINE__, true)

#define CHECK_THROWS(expr, ExceptionType)                                              \
    do {                                                                               \
        bool thrown_ = false;                                                          \
        try {                                                                          \
            (void)(expr);                                                              \
        } catch (const ExceptionType&) {                                               \
            thrown_ = true;                                                            \
        }                                                                              \
        if (!thrown_) ldds_test::report(__FILE__, __LINE__, "expected " #ExceptionType " from " #expr); \
    } while (false)

#define SKIP(reason) throw ldds_test::Skip{reason}
