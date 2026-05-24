---
name: reviewer-klknn
description: Guidelines and check procedures to review code edits and verify workspace changes against the preferences and standards of reviewer klknn.
version: 1.0.0
tags:
  - reviewer
  - code-review
  - standards
  - constraints
---

# Reviewer Klknn Guidelines

Use this skill when you are asked to review code edits, write implementations, or verify workspace changes. This skill acts as a proxy for the guidelines and standards set by the reviewer `klknn`.

## Checklist and Key Principles

### 1. Test-Driven Bug Fixing ("Test First")
- **Rule**: You MUST write a failing regression test to reproduce a bug before writing any source code changes or fixes.
- **Verification**: Ensure the test compiles and fails on the buggy codebase before checking that it passes on the fixed codebase.

### 2. Bazel Configurations and Compiler Flags
- **Rule**: Editing `.bazelrc` or introducing ad-hoc compiler flags is strictly a **last resort**.
- **Verification**: Always exhaust standard build commands, clean rebuilds (`bazel clean`), and IDE synchronization first.

### 3. Formatting
- **Rule**: You MUST always run `./tools/format.sh` after editing, modifying, or creating any C++, Java, BUILD, or proto files.
- **Verification**: Verify that the formatter completes successfully before submitting the work or ending your turn.

### 4. Optimized Build Mode
- **Rule**: Always build and run tests with `-c opt` (optimized mode) to prevent linker/compilation errors in fastbuild mode (e.g. `.sframe` relocations in the VST3 SDK).

### 5. Hide C++ implementations from header as possible

Do not expose any implementations except for getter/setters. Prefer PIMPL idiom whenever possible.

### 6. Prefer simpler design

Instead of putting many functions and fields in large classes, prefer pulling functions and POD struct outside of the class (e.g. module level scope in C++).

### 7. Always write doc comments in public functions/structs

Because we always hide its implementation details, documenting is better way to make the API usable.
In tests, we also prefer writing friendly detailed error messages on assertions.

### 8. Prefer absl::Status over bool for Return Values
- **Rule**: Prefer returning `absl::Status` with descriptive error messages instead of simple `bool` values for operations that can fail (e.g. loading plugins, processing commands).
- **Benefit**: Provides readable, friendly error messages on failure, facilitating easier debugging.
