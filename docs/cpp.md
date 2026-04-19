# C++ Coding Preferences

## Error Handling
- Prefer `absl::StatusOr<T>` over `std::unique_ptr<T>` / raw pointer returns for functions that can fail.
- Use `RETURN_IF_ERROR` and `ASSIGN_OR_RETURN` from `engine/core/status.hpp` when the caller returns `absl::Status`.
- Never silently swallow errors with logging unless the error is explicitly ignorable.

## Enums
- Use `enum class` (scoped enums) instead of unscoped `enum`.
  Ref: *Effective Modern C++* Item 10.

## Unreachable Code
- Use `ABSL_UNREACHABLE()` (`absl/base/optimization.h`) after exhaustive return-style switches.
- Do **not** place it after break-style switches (the code after the switch IS reachable).

## Containers & Utilities
- Prefer Abseil equivalents over `std::` when available:
  - `absl::Status` / `absl::StatusOr` over ad-hoc error returns
  - `LOG()` (abseil logging) over `std::cerr` / `std::cout`
- Keep `std::map` where key ordering matters (e.g. track iteration).

## Avoid
- `std::ranges` — do not use.
- `std::expected` — use `absl::StatusOr` instead.

## Indexing
- Use 0-based indexing throughout the codebase.
