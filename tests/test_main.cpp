#include "check.hpp"

int main() {
    int skipped = 0;
    int ran = 0;
    for (const auto& c : ldds_test::registry()) {
        const int before = ldds_test::failures();
        bool aborted = false;
        try {
            c.fn();
            ++ran;
        } catch (const ldds_test::Skip& s) {
            ++skipped;
            std::cout << "[ SKIP ] " << c.name << ": " << s.reason << '\n';
            continue;
        } catch (const ldds_test::Fatal&) {
            aborted = true;
        } catch (const std::exception& e) {
            ldds_test::report(c.name, 0, std::string("unexpected exception: ") + e.what());
        }
        std::cout << (ldds_test::failures() == before && !aborted ? "[  OK  ] " : "[ FAIL ] ") << c.name << '\n';
    }
    if (ldds_test::failures() != 0) {
        std::cout << ldds_test::failures() << " check(s) failed\n";
        return 1;
    }
    if (ran == 0 && skipped > 0) return 77;  // CTest SKIP_RETURN_CODE
    return 0;
}
