# C++ Error Handling & Logging Conventions

## Hierarchy

```
absl::Status / absl::StatusOr<T>   ← primary error return type
    ↓ consumed by
CHECK_OK(status) / LOG(ERROR)      ← at call sites
    ↓ backed by
LOG(INFO|WARNING|ERROR)            ← structured logging
```

## 1. `absl::Status` / `absl::StatusOr<T>` — Preferred for Fallible Operations

Use typed Status returns for any function that can fail:

```cpp
// Good — caller gets typed error info
absl::Status LoadWav(const std::string& path, std::vector<float>& out, ...);
absl::StatusOr<Project> LoadProject(const std::string& path);

// Usage at call site
auto status = LoadWav(path, data, channels, duration);
if (!status.ok()) {
  LOG(ERROR) << "Load failed: " << status.message();
  return;
}
// Or, if failure is fatal:
CHECK_OK(LoadProject(path));
```

**Error codes:** Use descriptive status codes from `absl/status/status.h`:
- `absl::NotFoundError` — file/resource not found
- `absl::InvalidArgumentError` — bad inputs
- `absl::InternalError` — unexpected internal failure
- `absl::DataLossError` — corruption / IO failure

**Macros** (from `engine/core/status.hpp`):
- `RETURN_IF_ERROR(expr)` — early return on error
- `ASSIGN_OR_RETURN(var, expr)` — unwrap StatusOr or return error

## 2. `CHECK` / `CHECK_*` — Invariant Assertions

Use CHECK macros for conditions that **must** hold. They crash with a stacktrace
on failure, which is the desired behavior for programming errors:

```cpp
#include "absl/log/check.h"

CHECK(ptr != nullptr) << "Unexpected null pointer";
CHECK_GT(sample_rate, 0.0f) << "Invalid sample rate";
CHECK_GE(buffer.size(), block_size * channels);
CHECK(!std::isnan(bpm)) << "BPM must be initialized";
CHECK_OK(SaveProject(state, path));  // fatal if save fails
```

**When to use CHECK:**
- Null pointer dereferences that indicate bugs
- Array/buffer size invariants before unsafe access
- Post-condition sanity (e.g., sample_rate > 0 after device init)
- State initialization (e.g., BPM, sample_rate set before playback)
- At call sites where Status failure is truly unrecoverable

**When NOT to use CHECK:**
- User-facing errors (file not found, bad input) → use `absl::Status`
- Plugin failures that should be recoverable → use `LOG(ERROR)` + graceful handling

## 3. `LOG()` — Structured Logging

Use `absl::LOG` for all diagnostic output. Never use `std::cerr`, `fprintf(stderr)`,
or `printf` for logging.

```cpp
#include "absl/log/log.h"

LOG(INFO) << "ALSA Audio Device: " << device_name;
LOG(WARNING) << "No controller available for '" << plugin_name << "'";
LOG(ERROR) << "Failed to load VST3 module: " << error;
LOG_EVERY_N_SEC(INFO, 1) << "MIDI capture: " << event_count << " events";
```

**Levels:**
- `INFO` — normal operation, device discovery, plugin loading
- `WARNING` — recoverable issues, fallback paths taken
- `ERROR` — failures that degrade functionality

**Initialization:** Call `absl::InitializeLog()` early in `main()` to suppress
the "all log messages before InitializeLog" warning.

## 4. BUILD Dependencies

```python
deps = [
    "@abseil-cpp//absl/log",           # LOG()
    "@abseil-cpp//absl/log:check",     # CHECK()
    "@abseil-cpp//absl/log:initialize",# InitializeLog()
    "@abseil-cpp//absl/status",        # absl::Status
    "@abseil-cpp//absl/strings",       # absl::StrCat (for status messages)
]
```

## 5. VST3 Plugin Safety

The VST3 SDK compiles with `-fno-exceptions`, so `try/catch` cannot be used in
code that links against it. Instead:

- **CHECK guards** before plugin API calls: `CHECK(processor != nullptr)`
- **LOG(ERROR)** for API call failures (non-zero `tresult`)
- **Null checks** before every COM interface dereference
- Plugin crashes in out-of-process workers are isolated by design

## 6. Testing with `absl::Status`

Use `absl/status/status_matchers.h` with gmock matchers for clean status assertions:

```cpp
#include "absl/status/status_matchers.h"

using ::absl_testing::IsOk;

TEST(MyTest, FunctionReturnsOk) {
  EXPECT_THAT(LoadWav(path, data, channels, dur), IsOk());
  ASSERT_THAT(SaveProject(state, file), IsOk());
}
```

**BUILD dep:**
```python
"@abseil-cpp//absl/status:status_matchers",
```
