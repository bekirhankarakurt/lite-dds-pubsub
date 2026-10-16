# Contributing

## Build and test

```bash
cmake -S . -B build -DLDDS_WERROR=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Before opening a pull request also run the sanitizer build:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DLDDS_SANITIZE=ON -DLDDS_WERROR=ON
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
```

Static analysis runs in CI and can be reproduced locally:

```bash
cmake -S . -B build-tidy -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build-tidy $(git ls-files 'src/*.cpp' 'apps/*.cpp' 'bench/*.cpp' 'tests/*.cpp')
cppcheck --enable=warning,style,performance,portability --error-exitcode=1 --inline-suppr \
  --suppressions-list=.cppcheck-suppressions -I include --std=c++17 src apps bench tests
```

## Style

- C++17, RAII, no raw `new`/`delete`, const-correct, `[[nodiscard]]` on results that matter, `enum class`.
- Zero warnings with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`; `.clang-tidy` must stay clean.
- No TODO markers, stubs or dead code. Public headers carry short `///` documentation.
- Hardware- or network-dependent code needs a simulation path (`MemoryBus`, `ManualClock`, replay) so it can be tested.
- Every behavior change comes with a test in `tests/`; the harness is `tests/check.hpp` (no external dependencies).

## Commits and pull requests

- Small, logical commits with a short imperative subject line ("Add NACK rate limit", not "Added" or "Adds").
- Describe why in the body when it is not obvious. Update `CHANGELOG.md` under `[Unreleased]`.
- Keep pull requests focused; CI (GCC, Clang, sanitizers, static analysis, CodeQL, coverage) must be green.
