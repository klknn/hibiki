# Cross-Platform Development Guide

This guide documents the patterns and conventions used in Hibiki's
C++ codebase to support Linux, macOS, and Windows from a single source tree.

## Platform Abstraction Strategy

Hibiki follows a strict rule: **no `#ifdef` in headers**. All
platform-specific code lives in dedicated `.cpp` files, selected
at build time by Bazel's `select()`.

### File Naming Conventions

| Suffix | Platform | Example |
|--------|----------|---------|
| `_posix.cpp` | Linux + macOS (POSIX) | `worker_channel_posix.cpp` |
| `_alsa.cpp` | Linux only | `sound_alsa.cpp` |
| `_coreaudio.cpp` | macOS only | `sound_coreaudio.cpp` |
| `_win32.cpp` | Windows only | `sound_win32.cpp`, `tcp_win32.cpp` |
| `_x11.cpp` | Linux (X11 GUI) | `vst3_host_x11.cpp` |
| `_mac.cpp` | macOS (Cocoa GUI) | `vst3_host_mac.cpp` |

### Pimpl Pattern

Platform-specific types (e.g., `snd_pcm_t*`, `HANDLE`, `AudioUnit`)
are hidden behind a forward-declared `struct Impl` in the `.cpp` file.
The header exposes only `std::unique_ptr<Impl> impl_`.

```cpp
// sound.hpp — platform-agnostic header
class SoundDevice {
 public:
  virtual ~SoundDevice() = default;
  virtual void write(const std::vector<float>& data, int frames) = 0;
  static std::unique_ptr<SoundDevice> create(int rate, int ch);
};
```

```cpp
// sound_alsa.cpp — Linux implementation
#include "sound.hpp"
#include <alsa/asoundlib.h>

class AlsaSoundDevice : public SoundDevice {
  snd_pcm_t* pcm_ = nullptr;  // ALSA-only type, hidden from header
  // ...
};

std::unique_ptr<SoundDevice> SoundDevice::create(int rate, int ch) {
  return std::make_unique<AlsaSoundDevice>(rate, ch);
}
```

### TCP Shim Pattern

For code that is *mostly* shared but differs in a few API calls
(e.g., BSD sockets vs Winsock2), use a thin shim header:

```
tcp.hpp            — declares platform-neutral functions
tcp_posix.cpp      — implements with POSIX APIs
tcp_win32.cpp      — implements with Winsock2 APIs
worker_channel_tcp.cpp — shared logic, calls tcp.hpp functions
```

---

## BUILD Patterns

### Per-Platform Source Selection with `select()`

```python
cc_library(
    name = "sound",
    hdrs = ["sound.hpp"],
    srcs = select({
        "@platforms//os:linux": ["sound_alsa.cpp"],
        "@platforms//os:macos": ["sound_coreaudio.cpp"],
        "@platforms//os:windows": ["sound_win32.cpp"],
    }),
    linkopts = select({
        "@platforms//os:linux": ["-lasound"],
        "@platforms//os:macos": [
            "-framework CoreAudio",
            "-framework AudioUnit",
        ],
        "@platforms//os:windows": ["-lole32"],
    }),
)
```

### `target_compatible_with` for Platform-Exclusive Targets

Use when a target should only exist on one platform:

```python
cc_library(
    name = "vst3_host_x11",
    srcs = ["vst3_host_x11.cpp"],
    target_compatible_with = ["@platforms//os:linux"],
)
```

### Linkopts Differences

| Library | Linux | macOS | Windows |
|---------|-------|-------|---------|
| POSIX threads | `-lpthread` | built-in | N/A |
| Shared memory | `-lrt` | built-in | N/A |
| Dynamic loading | `-ldl` | built-in | N/A |
| ALSA audio | `-lasound` | N/A | N/A |
| CoreAudio | N/A | `-framework CoreAudio` | N/A |
| WASAPI | N/A | N/A | `-lole32` |
| Winsock2 | N/A | N/A | `-lws2_32` |

---

## POSIX → Windows API Mapping

### IPC (Unix Socket → Named Pipe)

| Operation | POSIX | Windows |
|-----------|-------|---------|
| Create server | `socket(AF_UNIX)` | `CreateNamedPipeA` |
| Accept | `accept()` | `ConnectNamedPipe` |
| Connect | `connect()` | `CreateFileA` |
| Send/Recv | `write()`/`read()` | `WriteFile()`/`ReadFile()` |
| Close | `close(fd)` | `CloseHandle(hPipe)` |

### Shared Memory

| Operation | POSIX | Windows |
|-----------|-------|---------|
| Create | `shm_open()` + `ftruncate()` | `CreateFileMappingA` |
| Open | `shm_open()` | `OpenFileMappingA` |
| Map | `mmap()` | `MapViewOfFile` |
| Unmap | `munmap()` | `UnmapViewOfFile` |
| Remove | `shm_unlink()` | automatic |

### Process Management

| Operation | POSIX | Windows |
|-----------|-------|---------|
| Spawn | `fork()` + `execl()` | `CreateProcessA` |
| Kill | `kill(pid, SIGTERM)` | `TerminateProcess` |
| Wait | `waitpid()` | `WaitForSingleObject` |
| Parent death | `prctl(PR_SET_PDEATHSIG)` | Job Objects |

---

## Adding a New Platform-Specific Component

1. **Create the header** with abstract interface or pimpl — no `#ifdef`
2. **Create per-platform `.cpp` files** using the naming conventions above
3. **Add a `select()`-based `cc_library`** in BUILD
4. **Add platform-specific linkopts** using `select()`
5. **Write tests** that run on all platforms (use the abstract interface)

---

## Common Pitfalls

- **Don't include platform headers in `.hpp` files** — use forward
  declarations or pimpl instead of `#include <windows.h>`
- **Don't use `pid_t` in headers** — it's POSIX-only. Use an opaque
  type or `int` + platform-specific cast in the `.cpp`
- **Winsock2 initialization** — call `WSAStartup()` before any socket
  operation on Windows. The TCP shim handles this.
- **Path separators** — use `/` in code; Windows APIs accept both
- **`ssize_t`** — doesn't exist on MSVC. Define it as `intptr_t` or
  use `int` in cross-platform headers
