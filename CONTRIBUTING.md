# Contributing

Thanks for helping build production-grade bare-metal firmware. The bar is high
and the rules are few.

## Workflow

1. Create a branch from `main`.
2. Make focused, well-scoped commits.
3. Ensure the build, tests, and formatting all pass locally.
4. Open a pull request; CI must be green before review.

## Local checks

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# Formatting (matches CI)
clang-format -i $(git ls-files '*.c' '*.cc' '*.cpp' '*.h' '*.hpp')
```

## Coding standards

- **C++17**, no compiler warnings (CI builds with `-Wall -Wextra -Wpedantic …`).
- **Embedded code is allocation-free** and avoids exceptions/RTTI on hot paths.
- **Inject hardware at the boundary** (HAL structs / interfaces) so logic is
  testable on a host without a device.
- **Every public behavior has a test.** Prefer fast, deterministic unit tests.
- Keep public headers self-contained and documented.
- Match the existing style; `.clang-format` and `.editorconfig` are the source
  of truth.

## Commit messages

Use clear, imperative subject lines, e.g. `keypad: add non-blocking update()`.
Reference issues where relevant.
