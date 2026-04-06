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

---

## TCP Shim Deep Dive

The `tcp.hpp` / `tcp_posix.cpp` / `tcp_win32.cpp` pattern isolates
all socket API differences behind five functions:

```cpp
// tcp.hpp — platform-neutral interface
namespace hibiki {
  void tcp_init();         // WSAStartup on Windows, no-op on POSIX
  int tcp_close(socket_t); // close() vs closesocket()
  int tcp_send(socket_t, const void*, size_t);  // write() vs send()
  int tcp_recv(socket_t, void*, size_t);        // read() vs recv()
  void tcp_setsockopt(socket_t, int, int, int);
  const char* tcp_strerror(); // strerror(errno) vs WSA error string
}
```

### Platform Differences Abstracted

| Concern | POSIX (`tcp_posix.cpp`) | Windows (`tcp_win32.cpp`) |
|---------|------------------------|--------------------------|
| Socket type | `int` | `SOCKET` (unsigned) |
| Invalid value | `-1` | `INVALID_SOCKET` (~0) |
| Init | no-op | `WSAStartup(MAKEWORD(2,2), &wsaData)` |
| Close | `close(fd)` | `closesocket(fd)` |
| Send | `write(fd, buf, len)` | `send(fd, buf, len, 0)` |
| Recv | `read(fd, buf, len)` | `recv(fd, buf, len, 0)` |
| Error | `strerror(errno)` | `FormatMessageA(WSAGetLastError())` |
| Headers | `<sys/socket.h>`, `<arpa/inet.h>` | `<winsock2.h>`, `<ws2tcpip.h>` |

### Why Not `#ifdef` Inline?

Using `#ifdef _WIN32` inside shared `.cpp` files creates spaghetti
that's hard to test and review. The shim pattern:

1. Keeps all platform code in one dedicated file per platform
2. Makes `worker_channel_tcp.cpp` 100% platform-neutral
3. Allows each platform file to be reviewed in isolation
4. Works naturally with Bazel `select()` — no preprocessor needed
   at build time

---

## Extended Pitfalls & Codebase Examples

### Process Executable Path

```cpp
// ❌ Linux-only: /proc/self/exe doesn't exist elsewhere
ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf));

// ✅ Cross-platform approach:
// Linux:   readlink("/proc/self/exe")
// macOS:   _NSGetExecutablePath(buf, &size)
// Windows: GetModuleFileNameA(NULL, buf, MAX_PATH)
```

In practice, Hibiki uses `readlink` in `plugin_proxy.cpp` because
the sandbox mode only targets Linux currently. When adding macOS
support, extract this into a platform shim.

### Parent Death Signaling

```cpp
// ❌ Linux-only
prctl(PR_SET_PDEATHSIG, SIGTERM);

// macOS alternative:
// Use kqueue with EVFILT_PROC + NOTE_EXIT on getppid()

// Windows alternative:
// AssignProcessToJobObject + JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
```

### Socket Type Safety

```cpp
// ❌ Common mistake: comparing SOCKET to -1 on Windows
if (fd == -1) { /* WRONG on Windows */ }

// ✅ Use the shim constant
if (fd == INVALID_SOCK) { /* Works everywhere */ }
```

### `ssize_t` on MSVC

```cpp
// ❌ Won't compile on MSVC
ssize_t bytes_read = read(fd, buf, len);

// ✅ Use int or intptr_t in cross-platform code
int bytes_read = tcp_recv(fd, buf, len);
```

### Header Include Order

```cpp
// ❌ Including <windows.h> pulls in <winsock.h> which conflicts
//    with <winsock2.h>
#include <windows.h>
#include <winsock2.h>  // ERROR: redefinition

// ✅ Always include <winsock2.h> BEFORE <windows.h>, or use
//    WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
```

Hibiki avoids this entirely by never including Windows headers in
`.hpp` files — they only appear in `_win32.cpp` files.

---

## Debugging Cross-Platform Builds

### Running Windows Targets

Windows builds are cross-compiled using MinGW or built natively
with MSVC. To test locally on Linux:

```bash
# Cross-compile (if toolchain configured)
bazel build --platforms=@platforms//os:windows //:hbk-play

# Run under Wine (basic smoke test)
wine bazel-bin/hbk-play.exe
```

### Checking for Platform Leaks

Use `grep` to find accidental platform-specific includes in headers:

```bash
# Should find NO results in .hpp files
grep -rn '#include <windows.h>\|#include <unistd.h>\|#include <sys/' *.hpp
grep -rn 'pid_t\|HANDLE\|SOCKET' *.hpp
```

### Testing All Platforms in CI

```yaml
# .github/workflows/build.yml (example)
strategy:
  matrix:
    os: [ubuntu-latest, macos-latest, windows-latest]
```

Each platform runs `bazel test //...` with the correct `select()`
branches automatically chosen.

---

## Code Style Rules for Cross-Platform C++

1. **No `#ifdef` in `.hpp` files** — use pimpl or virtual interface
2. **No platform types in headers** — no `pid_t`, `HANDLE`, `SOCKET`,
   `snd_pcm_t*` etc. Use opaque wrappers or forward-declared `Impl`
3. **Use `select()` in BUILD** — never rely on preprocessor for
   source selection
4. **One `.cpp` per platform** — suffix with `_posix`, `_alsa`,
   `_win32`, `_coreaudio`, `_x11`, `_mac`
5. **Shared logic in shared `.cpp`** — TCP channel logic is in
   `worker_channel_tcp.cpp`, calling platform-neutral `tcp.hpp`
6. **Platform linkopts in `select()`** — `-lasound`, `-framework
   CoreAudio`, `-lws2_32` etc.
7. **Prefer standard C++17** — `<string>`, `<vector>`, `<thread>`,
   `<mutex>`, `<filesystem>` work everywhere
8. **Avoid glibc-isms** — `prctl`, `epoll`, `inotify` are Linux-only;
   provide alternatives via the platform shim pattern
