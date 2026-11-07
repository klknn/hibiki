# Hibiki DAW — D Language Migration Architecture & Plan

## 1. Executive Summary & Guiding Principles

### 1.1 Background & Motivation
Hibiki DAW is an open-source digital audio workstation developed to combine real-time modular synthesis, high-precision audio editing, plugin hosting (VST3), and flexible sequencing. Historically, the project evolved as a polyglot system: the audio DSP engine, plugin host, and playback server were implemented in modern C++ (C++23), while the desktop graphical user interface, timeline editor, and piano roll were authored in Java Swing/Java2D and Clojure, with an embedded JavaScript/TypeScript scripting engine.

While this architecture delivered rich functionality, maintaining a cross-language bridge between C++ and the JVM introduces non-trivial friction:
1. **Runtime Overhead & Non-Deterministic Latency**: The Java Virtual Machine imposes substantial memory overhead (300–400 MB baseline heap) and garbage collection pauses that necessitate strict multi-process isolation to prevent audio dropouts.
2. **Build and Interop Complexity**: Coordinating Bazel rules across C++, Java (`rules_jvm`), Clojure, and embedded Native JNI/C shims complicates continuous integration, packaging, and debugging.
3. **Language Friction**: Modern C++ offers deterministic execution and zero-cost abstractions, but lacks built-in module ergonomics, compile-time reflection, memory safety guardrails, and expressive slice mechanics without heavy third-party dependencies (such as Boost or Abseil).

The **D programming language** (specifically utilizing the latest **LLVM D Compiler, LDC 1.41.0**) provides a unified, modern systems programming paradigm that bridges the gap between low-level hardware control and high-level productivity. This document establishes the definitive, comprehensive architectural specification and implementation roadmap for migrating the Hibiki DAW codebase to D.

### 1.2 Core Principles: Conciseness, Safety, Speed
The migration to D is governed by three non-negotiable engineering tenets:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Hibiki Core Philosophy                          │
├────────────────────────────────────────────────────────────────────────┤
│  1. Conciseness (簡潔性)                                               │
│     - Native dynamic and static slices (T[]) eliminate boilerplate.    │
│     - Compile-time introspection (__traits, CTFE) removes manual glue. │
│     - Scope guards (scope(exit), scope(failure)) streamline RAII.      │
│     - Zero unnecessary external third-party D libraries.               │
├────────────────────────────────────────────────────────────────────────┤
│  2. Safety (安全性)                                                    │
│     - Compile-time memory safety enforcement (@safe).                  │
│     - Total ban of garbage collection on audio thread (@nogc).         │
│     - Formal exception exclusion (nothrow) on real-time paths.         │
│     - Pure DSP functions guaranteeing referential transparency.        │
├────────────────────────────────────────────────────────────────────────┤
│  3. Speed (高速性)                                                     │
│     - Direct native machine code generation via LLVM 20.1 backend.     │
│     - Identical or superior DSP execution speed relative to C++23.     │
│     - Direct cache-friendly SIMD auto-vectorization.                   │
│     - Deterministic 512-frame block processing without thread pauses.  │
└────────────────────────────────────────────────────────────────────────┘
```

1. **Conciseness**:
   D's expressive type system, Uniform Function Call Syntax (UFCS), native slices, and compile-time function execution (CTFE) drastically reduce lines of code compared to C++23. Repetitive boilerplate—such as Protobuf serialization, plugin parameter registration, and undo state cloning—is automated at compile time without runtime penalties.
2. **Safety**:
   Audio processing demands strict memory and execution safety. By dividing the codebase into `@safe` high-level domain logic and `@nogc nothrow` real-time audio code, we mathematically eliminate memory safety vulnerabilities, race conditions, and priority inversions.
3. **Speed**:
   LDC leverages the LLVM compiler infrastructure, emitting highly optimized SIMD instructions (SSE4.2, AVX2, NEON) directly on target hardware. D code achieves parity with optimized C++ while offering cleaner memory layouts and reduced pointer chasing.
4. **Zero External Dependencies**:
   To ensure maximum long-term stability, rapid hermetic builds, and complete platform sovereignty, the core migration avoids external DUB packages. Build toolchains, Protobuf wire serialization, DSP primitives, and audio engines are implemented self-containedly within the repository.

### 1.3 High-Level 4-Step Roadmap Overview
The migration is divided into four sequential, milestone-driven steps:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        4-Step Migration Roadmap                        │
├────────────────────────────────────────────────────────────────────────┤
│  Step 1: Hermetic Bazel Rules for LDC & Android Target Isolation       │
│  - Hermetic LDC 1.41.0 toolchain fetch via Starlark (Bzlmod).          │
│  - Custom rules: d_library, d_binary, d_test with CcInfo interop.      │
│  - Darwin target triple resolution & -betterC flag support.            │
│  - Android target isolation (target_compatible_with & manual tags).    │
│  - Formatter integration (dfmt via tools/format.sh).                   │
├────────────────────────────────────────────────────────────────────────┤
│  Step 2: Zero-Dependency Protobuf in D                                 │
│  - Self-contained wire format codec in dproto/ (Wire Types 0, 1, 2, 5).│
│  - Protobuf 3 wire quirks: 10-byte sign-extended int32, packed floats. │
│  - Oneof tagged unions, proto3 optional presence, default omission.   │
│  - @nogc fixed-buffer real-time audio serialization for IPC.           │
│  - Bidirectional wire compatibility test suite against C++ and Java.   │
├────────────────────────────────────────────────────────────────────────┤
│  Step 3: Core Engine Migration to D (Excluding External C++ Glue)      │
│  - 3-tier architecture: Tier 1 (@nogc RT), Tier 2 (@safe non-RT),      │
│    Tier 3 (extern(C++) boundary glue for VST3, sound, pocketfft).      │
│  - 7-phase bottom-up module migration order.                           │
│  - 15 builtin audio effects and 15 builtin synths in D.                │
│  - State management, WAV/MIDI parsers, 9 command handlers.             │
│  - Entry points: hbk-play, hbk-plugin-worker, hbk-worker-daemon.       │
├────────────────────────────────────────────────────────────────────────┤
│  Step 4: Future GUI Migration from Java/Clojure to D                   │
│  - Evaluation of D GUI frameworks (DlangUI, Raylib, Skia, Dear ImGui). │
│  - Recommended architecture: Dear ImGui (Docking) + cimgui + GLFW.     │
│  - Subprocess out-of-process isolation for absolute audio safety.      │
│  - 7-phase GUI migration roadmap (Windowing -> IPC -> Canvas -> REPL). │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Global Architecture & Boundary Taxonomy

### 2.1 The Three-Tier Architecture Model
To preserve real-time safety, strict modularity, and smooth interoperability, the Hibiki D engine is structured into three discrete operational tiers:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Hibiki 3-Tier Architecture                      │
├────────────────────────────────────────────────────────────────────────┤
│  Tier 2: Non-Real-Time Control, State & IPC                            │
│  - Language Paradigm: Idiomatic @safe D (Phobos runtime enabled)       │
│  - Memory: Dynamic GC heap, native slices (T[]), RAII scope guards     │
│  - Modules: ProjectState (.hbk), Track, TimelineClip, HistoryManager,  │
│             AudioFile (WAV), Midi SMF parser, AudioEditor, Registries, │
│             IPC stdout/stdin framing, 9 Command Handlers               │
├────────────────────────────────────────────────────────────────────────┤
│  Tier 1: Real-Time Audio & DSP Processing                              │
│  - Language Paradigm: @nogc nothrow pure, -betterC compatible          │
│  - Memory: Pre-allocated buffers, stack arrays, zero dynamic alloc     │
│  - Modules: playback_thread (512-frame blocks), BiquadFilter, Adsr,    │
│             MultiPointEnvelope, Modulator LFO, Resampler kernel,       │
│             15 Builtin Effects, 15 Builtin Synths, BuiltinDrumMachine, │
│             Track Mixer summing, Panning, Sidechain routing            │
├────────────────────────────────────────────────────────────────────────┤
│  Tier 3: External C++ Boundary Glue                                    │
│  - Language Paradigm: extern(C++) classes & extern(C) function shims   │
│  - Memory: Native OS handles, raw pointers, zero C++ STL leaks         │
│  - Modules: Steinberg VST3 SDK wrapper (engine/vst3/vst3_host.*),      │
│             OS Audio/MIDI hardware (engine/audio/sound.*, midi_input), │
│             PocketFFT C shim (pocketfft_c_shim.cpp)                    │
└────────────────────────────────────────────────────────────────────────┘
```

#### Tier 1: Real-Time Audio & DSP Processing
- **Execution Context**: Real-time audio rendering thread (`playback_thread`), running at elevated OS priority (e.g., `THREAD_PRIORITY_TIME_CRITICAL` or macOS Audio Workgroup).
- **Invariants**: Strict enforcement of `@nogc` and `nothrow`. Absolutely no calls to garbage-collected allocations (`new`, array appends `~=`, associative arrays), dynamic type casts, or blocking OS system calls (file I/O, mutex locks, network sockets).
- **Processing Unit**: Synchronous block processing (typically 512 frames, corresponding to ~11.6 ms latency at 44.1 kHz or ~10.7 ms at 48 kHz).
- **Compatibility**: Written to be fully functional under `-betterC` mode if desired, relying solely on C standard library primitives or raw memory buffers.

#### Tier 2: Non-Real-Time Control, State & IPC
- **Execution Context**: Main command thread, background worker threads, disk I/O threads, and GUI synchronization loops.
- **Invariants**: Idiomatic, expressive `@safe` D utilizing Phobos standard library, garbage collection where appropriate for non-real-time state, associative arrays for string lookup tables, dynamic slices, and strong exceptions (`Exception`).
- **Responsibilities**:
  - Managing project graph hierarchy (`ProjectState`, `TrackState`, `ClipState`).
  - Loading and saving binary project files (`.hbk`) and configuration (`.hibikirc.textproto`).
  - Maintaining 50-step undo/redo transaction history (`HistoryManager`).
  - Parsing and encoding uncompressed audio (`AudioFile` WAV reader/writer) and Standard MIDI Files (`Midi`).
  - Decoding incoming GUI commands from `stdin` and emitting binary notification packets to `stdout`.

#### Tier 3: External C++ Boundary Glue
- **Execution Context**: Operating system audio drivers, native desktop windowing subsystems, and third-party plugin SDKs.
- **Invariants**: Stable, clean binary interface contracts (`extern(C++)` and `extern(C)`). C++ Standard Template Library (`std::vector`, `std::string`, `std::function`) is strictly prohibited from crossing the ABI boundary to prevent symbol mangling and allocator incompatibilities.
- **Components**:
  - Steinberg VST3 SDK host binding (`engine/vst3/`): Manages VST3 COM interfaces (`IComponent`, `IAudioProcessor`, `IEditController`) and attaches native window handles (`NSView*` on macOS, `HWND` on Windows, `Window` on Linux X11).
  - Native Audio Hardware abstraction (`engine/audio/sound.*`): Interfaces with CoreAudio (macOS/iOS), ALSA (Linux), AAudio (Android), and WASAPI (Windows).
  - Header-Only C++ Libraries (`pocketfft`): Wrapped behind a trivial 30-line `extern "C"` shim to avoid exposing complex C++ template metaprogramming to the D compiler.

### 2.2 Memory Safety & Audio Thread GC Isolation
In modern DAWs, garbage collection pauses (Stop-the-World events) cause catastrophic audio dropouts ("pops" or "glitches"). Hibiki eliminates this hazard through a multi-tiered defense:

1. **Static Analysis Enforcement via `@nogc`**:
   The D compiler verifies at compile time that no AST expression within a `@nogc` function can invoke the D runtime memory allocator (`_d_newitemT`, `_d_newarrayT`, etc.). If a developer accidentally writes `auto arr = new float[512];` inside a DSP callback, compilation immediately fails with an error.
2. **Deterministic Pre-Allocation**:
   All audio thread data structures (delay line buffers, reverb comb filters, voice note pools, convolution partitions) are pre-allocated during plugin initialization (`init()` / `reset()`). During real-time playback, DSP components operate strictly within fixed-size stack buffers (`float[512]`) or pre-allocated heap slices allocated during project load.
3. **Lock-Free Cross-Thread Communication**:
   State transitions requested by Tier 2 (e.g., parameter tweaks, note triggers, clip scheduling) are communicated to Tier 1 via wait-free, lock-free Single-Producer Single-Consumer (SPSC) ring buffers or atomic variables (`core.atomic`). Tier 1 never locks a mutex or waits on a condition variable.
4. **Audio Thread Detachment from GC**:
   When launching the audio thread via platform APIs or D's `core.thread`, the thread is intentionally detached from the D runtime GC world-tracking registry (`thread_detachThis()`). Consequently, even if Tier 2 triggers a background GC collection cycle, the audio thread is never paused.

### 2.3 C++ Interoperability & ABI Contracts
D was designed with native C++ interop as a core language feature. Hibiki leverages this to interact with existing C++ components without intermediate glue wrappers:

#### 1. Calling C++ Member Functions and Classes (`extern(C++)`)
D directly shares the Itanium C++ ABI (on macOS and Linux) and the MSVC C++ ABI (on Windows). A C++ abstract base class maps 1:1 to a D `extern(C++) interface`:

```d
// D Definition matching C++ IPlugin
extern(C++) interface IPlugin {
    void reset() @nogc nothrow;
    void process(
        float** inputs,
        float** outputs,
        int numSamples,
        const ref HostProcessContext ctx,
        const(MidiNoteEvent)* events,
        size_t numEvents,
        float** sidechain
    ) @nogc nothrow;
    void setParameter(uint paramId, float value) @nogc nothrow;
    float getParameter(uint paramId) const @nogc nothrow;
}
```

#### 2. Cross-Language Struct Layout (`extern(C)`)
Structs shared between D and C++ must use standard C memory layout alignment (`extern(C)` or `extern(C++) struct`). D slices (`T[]`) are 16-byte pairs consisting of `{ size_t length; T* ptr; }`, which do not match C++ pointer conventions. Therefore, all ABI boundary functions use explicit pointers and size arguments:
- Pass buffers as: `const(float)* buffer, size_t numSamples` (or `float** channels` for deinterleaved audio).
- Pass strings as: `const(char)* str, size_t len` (or null-terminated `const(char)*`).

#### 3. Runtime Lifecycle Management
When the application entry point is in C++ (`engine/main.cpp`) and invokes standard D libraries with Phobos support, C++ must explicitly initialize and terminate `druntime`:

```cpp
// C++ startup wrapper
#include <iostream>

extern "C" int rt_init();
extern "C" int rt_term();

int main(int argc, char** argv) {
    if (!rt_init()) {
        std::cerr << "Failed to initialize D runtime" << std::endl;
        return 1;
    }
    // Execute D engine logic...
    rt_term();
    return 0;
}
```
If the entire binary is built under `-betterC`, `rt_init()` is completely unnecessary and omitted.

---

## 3. Step 1 Specification: Hermetic Bazel Rules for LDC

### 3.1 Android Target Isolation & Build Unblocking
Hibiki utilizes Bazel 9 with Bzlmod (`MODULE.bazel`). Recently, experimental Android toolchain registrations and incomplete glob definitions were introduced to the workspace, which caused global builds (`bazel test //... -c opt`) to fail on developer machines and CI servers where the Android SDK and NDK are not installed:

1. **Root Cause Analysis**:
   - In `MODULE.bazel` lines 33 and 41, unconditional toolchain registrations:
     ```bzl
     register_toolchains("@androidsdk//:sdk-toolchain", "@androidsdk//:all")
     register_toolchains("@androidndk//:all")
     ```
     force Bazel to evaluate the `@androidndk` repository rule during toolchain resolution for *every* build target. When `ANDROID_NDK_HOME` is absent, `@rules_android_ndk//:rules.bzl` invokes `fail("Either the ANDROID_NDK_HOME environment variable or the path attribute of android_ndk_repository must be set.")`.
   - In `android/app/BUILD` line 11:
     ```bzl
     srcs = glob(["src/main/jniLibs/**/*.so"]),
     ```
     under Bazel 9's default `--incompatible_disallow_empty_glob`, an empty directory results in an immediate build evaluation failure.
2. **Remediation Specification**:
   - **Scope Toolchain Registration to Android Builds**: Remove unconditional `register_toolchains` from `MODULE.bazel`. Toolchain registration is delegated to `.bazelrc` under an explicit `--config=android` flag:
     ```bzl
     build:android --extra_toolchains=@androidndk//:all
     ```
   - **Permit Empty Globs**: In `android/app/BUILD`, configure `allow_empty = True`:
     ```bzl
     cc_library(
         name = "hibiki_jni_so",
         srcs = glob(["src/main/jniLibs/**/*.so"], allow_empty = True),
     )
     ```
   - **Apply Platform Constraints and Manual Tags**:
     Tag all Android-specific targets in `android/app/BUILD` and `engine/android/BUILD`:
     ```bzl
     target_compatible_with = ["@platforms//os:android"],
     tags = ["manual"],
     ```
     This instructs Bazel's target pattern evaluator to completely skip Android targets when executing host commands such as `bazel build //...` or `bazel test //... -c opt`.

### 3.2 Hermetic LDC Toolchain Design (`tools/d_rules/`)
To eliminate host dependency friction, Bazel must download and configure a hermetic LDC compiler distribution matching the host operating system and CPU architecture.

#### Platform Tarballs & Integrity Hashes (LDC 1.41.0)
The repository rule fetches the official LDC 1.41.0 archives (based on DMD v2.111.0 and LLVM 20.1.5):

| Host Platform | Archive Filename | SHA-256 Digest |
| :--- | :--- | :--- |
| **macOS ARM64** | `ldc2-1.41.0-osx-arm64.tar.xz` | `157267042f10b047210619314aa719b4f0bf887601e93b1c634aa1ecb3c546e4` |
| **macOS x86_64** | `ldc2-1.41.0-osx-x86_64.tar.xz` | `5bcff48b63c56a45dbaacdb0c5bddc8ea6be86d4a0c7b2c7c8318e047f721181` |
| **Linux x86_64** | `ldc2-1.41.0-linux-x86_64.tar.xz` | `4a439457f0fe59e69d02fd6b57549fc3c87ad0f55ad9fb9e42507b6f8e327c8f` |
| **Linux AArch64** | `ldc2-1.41.0-linux-aarch64.tar.xz` | `1c4b950a13d53379ed4f564366c27ec56d6261e21686880d70c7486b3e8c7ba8` |
| **Windows x64** | `ldc2-1.41.0-windows-x64.7z` | `1db11e5eeed10236729d45cde861fb5197d1cc716b4186600fca7fc6bb48c810` |

If an offline environment is detected, the repository rule falls back to the host compiler specified by the `LDC_PATH` environment variable or `/Users/karita/dlang/ldc-1.41.0`.

### 3.3 Target Triple Resolution (macOS Darwin 27 & OS)
A critical defect occurs when invoking `ldc2` directly on modern macOS (Darwin 27.x.x / macOS 15+ Sequoia):
- By default, LDC calculates the macOS version by subtracting 9 from the Darwin major version: `27 - 9 = 18`. It subsequently passes `-target arm64-apple-macos18` to the underlying LLVM code generator.
- Apple Clang 21.0.0 and modern Apple linkers reject `macos18` as an invalid, unreleased operating system version, failing with:
  `clang: error: invalid version number in '-target arm64-apple-macos18'`
- **Fix**: The D Bazel rules systematically detect macOS execution and explicitly inject the flag:
  `-mtriple=arm64-apple-darwin27.0.0` (or `-mtriple=arm64-apple-macos15.0`).
  This overrides LLVM's version calculation and ensures 100% clean compilation and linking.

### 3.4 Starlark Rules: `d_library`, `d_binary`, `d_test`
The core build logic is encapsulated in `tools/d_rules/defs.bzl`:

```bzl
# tools/d_rules/defs.bzl
load("@rules_cc//cc:defs.bzl", "cc_common")

DInfo = provider(
    doc = "Propagates transitive D sources, module import paths, and string imports.",
    fields = {
        "srcs": "depset of D source files (.d)",
        "import_dirs": "depset of import directories (-I)",
    },
)

def _d_library_impl(ctx):
    out_lib = ctx.actions.declare_file("lib" + ctx.label.name + ".a")
    
    # Aggregate transitive D imports and C++ dependencies
    transitive_srcs = [ctx.files.srcs]
    transitive_imports = [ctx.label.package]
    cc_dep_contexts = []
    
    for dep in ctx.attr.deps:
        if DInfo in dep:
            transitive_srcs.append(dep[DInfo].srcs)
            transitive_imports.append(dep[DInfo].import_dirs)
        if CcInfo in dep:
            cc_dep_contexts.append(dep[CcInfo])

    # Platform-specific compilation flags
    flags = []
    if ctx.target_platform_has_darwin:
        flags.append("-mtriple=arm64-apple-darwin27.0.0")
    if ctx.attr.betterc:
        flags.append("-betterC")
    
    # Import paths
    for imp in depset(transitive = transitive_imports).to_list():
        flags.append("-I=" + imp)

    args = ctx.actions.args()
    args.add("--lib")
    args.add("-of=" + out_lib.path)
    args.add_all(flags)
    args.add_all(ctx.attr.copts)
    args.add_all(ctx.files.srcs)

    ctx.actions.run(
        inputs = ctx.files.srcs,
        outputs = [out_lib],
        executable = ctx.executable._ldc2,
        arguments = [args],
        mnemonic = "DCompileLib",
        progress_message = "Compiling D library %{label}",
    )

    # Wrap the output archive into CcInfo for seamless C++ interop
    cc_toolchain = ctx.attr._cc_toolchain[cc_common.CcToolchainInfo]
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )
    
    linker_input = cc_common.create_linker_input(
        owner = ctx.label,
        libraries = depset([
            cc_common.create_library_to_link(
                actions = ctx.actions,
                feature_configuration = feature_configuration,
                cc_toolchain = cc_toolchain,
                static_library = out_lib,
            )
        ]),
    )
    linking_context = cc_common.create_linking_context(
        linker_inputs = depset([linker_input]),
    )
    
    merged_cc_info = cc_common.merge_cc_infos(
        direct_cc_infos = [CcInfo(linking_context = linking_context)] + cc_dep_contexts,
    )

    return [
        DInfo(
            srcs = depset(ctx.files.srcs, transitive = transitive_srcs),
            import_dirs = depset(transitive_imports),
        ),
        merged_cc_info,
    ]

d_library = rule(
    implementation = _d_library_impl,
    attrs = {
        "srcs": attr.label_list(allow_files = [".d"], mandatory = True),
        "deps": attr.label_list(providers = [[DInfo], [CcInfo]]),
        "copts": attr.string_list(default = ["-O3", "-release"]),
        "betterc": attr.bool(default = False),
        "_ldc2": attr.label(default = "@ldc_toolchain//:ldc2", executable = True, cfg = "exec"),
        "_cc_toolchain": attr.label(default = "@bazel_tools//tools/cpp:current_cc_toolchain"),
    },
    fragments = ["cpp"],
    toolchains = ["@bazel_tools//tools/cpp:toolchain_type"],
)
```

#### Rule Capabilities
- **`d_library`**: Compiles D source files into a static library (`.a`). Returns `DInfo` for D consumers and `CcInfo` for C++ consumers. Existing C++ targets (`cc_binary`, `cc_library`, `cc_test`) can depend directly on `d_library` without additional glue rules.
- **`d_binary`**: Compiles and links an executable. If `betterc = False`, automatically links `libphobos2-ldc.a` and `libdruntime-ldc.a`. Resolves C++ dependencies from `CcInfo` and links system runtime libraries (`-L=-lc++` on macOS, `-L=-lstdc++` on Linux).
- **`d_test`**: Compiles with `-unittest -main` to produce a standalone test executable fully integrated with `bazel test`.

### 3.5 Bzlmod Integration (`MODULE.bazel`)
In `MODULE.bazel`, the D rules extension is registered:

```bzl
# MODULE.bazel
bazel_dep(name = "platforms", version = "0.0.10")
bazel_dep(name = "rules_cc", version = "0.0.17")

ldc_extension = use_extension("//tools/d_rules:extensions.bzl", "ldc_repositories")
use_repo(ldc_extension, "ldc_toolchain")
```

### 3.6 Formatter Integration (`dfmt` & `tools/format.sh`)
The D community formatting standard is `dfmt` (`dlang-community/dfmt`). `tools/format.sh` is extended with an `ensure_dfmt` routine matching `google-java-format` and `buildifier`:

```bash
# In tools/format.sh:
DFMT_VERSION="v0.15.1"
DFMT_BIN="$TOOLS_DIR/dfmt"

ensure_dfmt() {
  if command -v dfmt &>/dev/null; then return 0; fi
  if [[ ! -x "$DFMT_BIN" ]]; then
    local url="https://github.com/dlang-community/dfmt/releases/download/${DFMT_VERSION}/dfmt-${PLATFORM}"
    curl -fsSL -o "$DFMT_BIN" "$url"
    chmod +x "$DFMT_BIN"
  fi
}

format_d() {
  ensure_dfmt
  local files
  files=$(find "$REPO_ROOT" -name '*.d' -not -path '*/.*' -not -path '*/bazel-*')
  if [[ -n "$files" ]]; then
    echo "$files" | xargs "$DFMT_BIN" -i
  fi
}
```

---

## 4. Step 2 Specification: Zero-Dependency D Protobuf

### 4.1 Schema Inventory & Scope (`pb/*.proto`)
Hibiki relies on four canonical Protobuf schema files under `syntax = "proto3";`:

| Schema File | Message Count | Enum Count | Key Message Types | Primary Role |
| :--- | :--- | :--- | :--- | :--- |
| `pb/core.proto` | 15 | 3 | `Project`, `Track`, `Clip`, `AutomationLane` | Domain model persistence (`.hbk`) |
| `pb/commands.proto` | 19 | 10 | `Request`, `ProjectCmd`, `TrackCmd`, `TransportCmd` | GUI $\rightarrow$ Engine stdin commands |
| `pb/notifications.proto` | 37 | 1 | `Notification`, `PlaybackState`, `TrackMeter` | Engine $\rightarrow$ GUI stdout sync |
| `pb/plugin_worker.proto` | 30 | 1 | `WorkerRequest`, `WorkerResponse`, `ProcessAudio` | Audio $\leftrightarrow$ VST3 worker IPC |

**Structural Characteristics**:
Across all 101 messages:
- Only Wire Types **0** (Varint), **1** (64-bit Fixed), **2** (Length-delimited), and **5** (32-bit Fixed) are utilized.
- No Protobuf Maps (`map<k, v>`) or zigzag signed integers (`sint32`, `sint64`) are used.
- Contains 11 packed repeated float fields (audio waveform envelopes, spectrum analysis bars).
- Contains 5 explicit `oneof` unions and 4 proto3 `optional` fields.

### 4.2 Wire Format Codec Architecture
The package `dproto/` implements a lightweight, zero-external-dependency wire encoder and decoder:
- `dproto/wire.d`: Pure, `@nogc`, `nothrow`, `@safe` low-level wire primitives.
- `dproto/message.d`: Compile-time reflection traits and field tag generation.
- `pb/*.d`: Strongly-typed D message structs.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        dproto Codec Architecture                       │
├────────────────────────────────────────────────────────────────────────┤
│  Wire Type 0: Varint (1-10 bytes)                                      │
│  - 7 bits payload per byte, bit 7 = continuation bit.                  │
│  - Negative int32 quirk: Cast to int64 and encode as 10-byte varint.   │
├────────────────────────────────────────────────────────────────────────┤
│  Wire Type 1: Fixed 64-bit (8 bytes LE)                                │
│  - double, fixed64, sfixed64. Direct IEEE 754 byte copy.               │
├────────────────────────────────────────────────────────────────────────┤
│  Wire Type 2: Length-Delimited (Varint length + raw payload)          │
│  - Strings (UTF-8 byte count, not character count).                    │
│  - Raw bytes slices (const(ubyte)[]).                                  │
│  - Embedded submessages (nested serialization).                        │
│  - Packed repeated floats (N * 4 contiguous bytes).                   │
├────────────────────────────────────────────────────────────────────────┤
│  Wire Type 5: Fixed 32-bit (4 bytes LE)                                │
│  - float, fixed32, sfixed32. Direct IEEE 754 byte copy.                │
└────────────────────────────────────────────────────────────────────────┘
```

#### Low-Level Wire Primitives (`dproto/wire.d`)

```d
module dproto.wire;

/// Canonical Protobuf wire type identifiers.
enum WireType : ubyte {
    varint = 0,
    fixed64 = 1,
    lengthDelimited = 2,
    fixed32 = 5,
}

/// Constructs a field tag byte from field number and wire type.
pragma(inline, true)
pure nothrow @nogc @safe ubyte makeTag(uint fieldNumber, WireType wireType) {
    return cast(ubyte)((fieldNumber << 3) | cast(ubyte)wireType);
}

/// Encodes an unsigned 64-bit integer into a varint byte slice.
pure nothrow @nogc @safe size_t writeVarint(ref ubyte[] buf, ulong val) {
    size_t written = 0;
    while (val >= 0x80) {
        if (written >= buf.length) return 0;
        buf[written++] = cast(ubyte)((val & 0x7F) | 0x80);
        val >>= 7;
    }
    if (written >= buf.length) return 0;
    buf[written++] = cast(ubyte)val;
    return written;
}

/// Encodes signed 32-bit int with Protobuf 10-byte sign-extension quirk.
pure nothrow @nogc @safe size_t writeInt32(ref ubyte[] buf, int val) {
    ulong uval = (val < 0) ? cast(ulong)cast(long)val : cast(ulong)val;
    return writeVarint(buf, uval);
}

/// Decodes a varint from a byte slice. Updates slice in-place.
pure nothrow @nogc @safe bool readVarint(ref const(ubyte)[] buf, out ulong val) {
    val = 0;
    int shift = 0;
    while (buf.length > 0) {
        ubyte b = buf[0];
        buf = buf[1 .. $];
        val |= cast(ulong)(b & 0x7F) << shift;
        if (!(b & 0x80)) return true;
        shift += 7;
        if (shift >= 64) return false; // Overflow protection
    }
    return false;
}

/// Skips unknown field according to wire type.
pure nothrow @nogc @safe bool skipField(ref const(ubyte)[] buf, WireType wireType) {
    final switch (wireType) {
        case WireType.varint:
            ulong dummy;
            return readVarint(buf, dummy);
        case WireType.fixed64:
            if (buf.length < 8) return false;
            buf = buf[8 .. $];
            return true;
        case WireType.lengthDelimited:
            ulong len;
            if (!readVarint(buf, len) || buf.length < len) return false;
            buf = buf[cast(size_t)len .. $];
            return true;
        case WireType.fixed32:
            if (buf.length < 4) return false;
            buf = buf[4 .. $];
            return true;
    }
}
```

### 4.3 Proto3 Semantics: Presence, Defaults, Oneofs
1. **Proto3 Default Value Omission**:
   In proto3, scalar fields with default values (`0`, `0.0f`, `false`, `""`) are omitted during serialization to reduce payload size.
2. **Proto3 Optional & Presence Tracking**:
   Fields marked `optional` generate an explicit boolean tracking flag (`bool has_fieldName`). If `has_fieldName` is true, the field is written to the wire even if its value equals zero.
3. **Oneof Tagged Unions**:
   Oneofs are represented in D as tagged unions containing an enum tag indicating the active variant. When writing to the wire, only the active variant is emitted.
4. **Packed Repeated Fields**:
   Proto3 packs repeated floating-point fields into a single Length-Delimited tag (`WireType.lengthDelimited`). Decoders must support both packed wire format and unpacked repeated tags (`WireType.fixed32`).

### 4.4 Real-Time Audio Serialization (`@nogc` Zero-Heap)
When transmitting audio frames across the IPC socket to worker plugins (`ProcessAudio`), dynamic heap allocations are prohibited. `dproto` provides a fixed-buffer zero-heap serialization interface:

```d
module dproto.realtime;

struct RealtimeAudioPacket {
    int sampleRate;
    int numSamples;
    int numChannels;
    float[2][512] audioData;
}

/// Serializes audio packet directly into a pre-allocated byte buffer without heap allocation.
size_t serializeRealtimeAudio(const ref RealtimeAudioPacket packet, ubyte[] dest) @nogc nothrow @safe {
    ubyte[] outBuf = dest;
    size_t totalBytes = 0;

    // Field 1: sampleRate (varint)
    dest[totalBytes++] = makeTag(1, WireType.varint);
    totalBytes += writeVarint(outBuf[totalBytes .. $], cast(ulong)packet.sampleRate);

    // Field 2: numSamples (varint)
    dest[totalBytes++] = makeTag(2, WireType.varint);
    totalBytes += writeVarint(outBuf[totalBytes .. $], cast(ulong)packet.numSamples);

    // Field 3: audioData (packed repeated floats)
    dest[totalBytes++] = makeTag(3, WireType.lengthDelimited);
    const size_t floatBytes = packet.numChannels * packet.numSamples * 4;
    totalBytes += writeVarint(outBuf[totalBytes .. $], floatBytes);

    for (int ch = 0; ch < packet.numChannels; ++ch) {
        for (int s = 0; s < packet.numSamples; ++s) {
            float f = packet.audioData[ch][s];
            const(ubyte)* bytes = cast(const(ubyte)*)&f;
            dest[totalBytes .. totalBytes + 4] = bytes[0 .. 4];
            totalBytes += 4;
        }
    }
    return totalBytes;
}
```

### 4.5 IPC Framing Protocols (Engine IO & Sockets)
Hibiki defines three distinct length-prefixed IPC framing contracts:

```
1. Engine Stdout to GUI (Notifications):
   ┌──────────────────────┬──────────────────────┬──────────────────────────────────┐
   │ Magic (4B LE)        │ Length (4B LE)       │ Serialized Notification Payload │
   │ 0x48424B49 ("HBKI")  │ uint32 size          │ bytes[size]                      │
   └──────────────────────┴──────────────────────┴──────────────────────────────────┘

2. GUI Stdin to Engine (Commands):
   ┌──────────────────────┬────────────────────────────────────────────────────────┐
   │ Length (4B LE)       │ Serialized Request Payload                             │
   │ uint32 size          │ bytes[size]                                            │
   └──────────────────────┴────────────────────────────────────────────────────────┘

3. Plugin Worker Socket (WorkerRequest / WorkerResponse):
   ┌──────────────────────┬────────────────────────────────────────────────────────┐
   │ Length (4B LE)       │ Serialized WorkerRequest / WorkerResponse Payload      │
   │ uint32 size          │ bytes[size]                                            │
   └──────────────────────┴────────────────────────────────────────────────────────┘
```

- **Resynchronization Scanner**: `IpcClient.java` and `gui.ipc` scan up to 10,000 bytes for `0x48424B49` if byte corruption occurs on stdout.
- **Payload Size Ceiling**: Enforces a strict 1 MB (`1024 * 1024` bytes) sanity limit on incoming commands.

### 4.6 Verification & Wire-Compatibility Test Suite
Binary compatibility is guaranteed through automated cross-validation tests:
1. **Golden Vector Generation**: C++ and Java test binaries serialize representative project states, command requests, and notification payloads into `.bin` golden files.
2. **D Deserialization & Field Verification**: D unit tests deserialize each golden vector and assert 100% field value equality.
3. **D Re-Serialization & Byte Equality**: D re-serializes the restored structs and verifies byte-for-byte identity against the C++ golden output.

---

## 5. Step 3 Specification: Core Engine Migration to D

### 5.1 7-Phase Bottom-Up Migration Strategy
To ensure that all existing C++ and Java tests pass continuously throughout migration, the engine components are migrated in a strict bottom-up dependency order:

```
[Phase 3.1: DSP Primitives & Math Kernel]
  core/math, core/biquad_filter, instruments/adsr, instruments/multi_point_envelope,
  core/modulator, core/resample, ipc/tcp
        │
        ▼
[Phase 3.2: 15 Builtin Audio Effects]
  effects/builtin_* (15 effects: Compressor, Eq, Delay, Reverb, Limiter, Maxim,
  Hott, Bitcrusher, Chorus, Phaser, StereoWidth, Convolver, EnvelopeShaper, Vocodey, Aux),
  effects/registry
        │
        ▼
[Phase 3.3: 15 Builtin Instruments & Synths]
  instruments/builtin_* (15 synths: 3xOsc, AcidBass, 9 DR8 drum synths,
  EPiano, Film, Organ, Sampler), instruments/registry
        │
        ▼
[Phase 3.4: Core Audio, Project State, History & Parsers]
  core/audio_file (WAV), core/midi (SMF), core/clip, core/history (undo/redo),
  core/audio_editor (destructive editing), core/track, core/project (.hbk)
        │
        ▼
[Phase 3.5: IPC Infrastructure & 9 Command Handlers]
  instruments/builtin_drum_machine, builtin_registry, ipc/ipc,
  commands/* (handleProjectCmd, handleTrackCmd, handleTransportCmd, etc.)
        │
        ▼
[Phase 3.6: Entry Points & Subprocess Daemons]
  hbk-play, hbk-plugin-worker, hbk-worker-daemon, plugin/plugin_proxy, plugin/plugin_scanner
        │
        ▼
[Phase 3.7: C++ Boundary Glue]
  engine/vst3/vst3_host.*, engine/audio/sound.*, engine/audio/midi_input.*,
  third_party/pocketfft/pocketfft_c_shim.cpp
```

#### Detailed Phase Descriptions

##### Phase 3.1: DSP Primitives & Math Kernel (`@nogc nothrow pure`)
- `engine/core/math.d`: High-precision mathematical constants (`pi = 3.14159265358979323846`), dB-to-linear conversions (`dbToLinear(float db) = 10.0f ^^ (db / 20.0f)`), linear-to-dB conversions, and constant-power panning laws.
- `engine/core/biquad_filter.d`: RBJ audio cookbook biquad filter supporting LowPass, HighPass, BandPass, Notch, Peaking, LowShelf, and HighShelf with smooth parameter updates.
- `engine/instruments/adsr.d` & `multi_point_envelope.d`: Linear and exponential envelope generators with multi-segment breakpoint curves.
- `engine/core/modulator.d`: Phase-accumulating LFO supporting Sine, Triangle, Saw, Square, and Sample-and-Hold random waveforms.
- `engine/core/resample.d`: Polyphase Kaiser-windowed sinc interpolation kernel for arbitrary sample rate conversion.

##### Phase 3.2: 15 Builtin Audio Effects
Migrate all 15 builtin effects into clean D structs implementing `IPlugin`:
1. `BuiltinCompressor`: Peak/RMS detector, attack/release ballistics, variable ratio, soft knee, makeup gain.
2. `BuiltinEq`: 8-band parametric EQ with complex frequency response computation.
3. `BuiltinDelay`: Stereo ping-pong feedback delay with cross-feed damping filters.
4. `BuiltinReverb`: Algorithmic reverberator combining Schroeder/Moorer comb and allpass lattices.
5. `BuiltinLimiter`: Lookahead peak limiter with transparent auto-release.
6. `BuiltinMaxim`: Loudness maximizer and soft-knee saturator.
7. `BuiltinHott`: Tube amplifier saturation model with harmonic overtones.
8. `BuiltinBitcrusher`: Sample-rate reduction and bit-depth quantization.
9. `BuiltinChorus`: Multi-voice modulated delay lines with quadrature LFOs.
10. `BuiltinPhaser`: 6-stage allpass filter ladder with resonance feedback.
11. `BuiltinStereoWidth`: Mid/side decomposition and stereo balance adjustment.
12. `BuiltinConvolver`: Real-time partitioned impulse response convolution.
13. `BuiltinEnvelopeShaper`: Transient shaper with independent attack/sustain detection.
14. `BuiltinVocodey`: Channel vocoder with carrier/modulator filterbanks.
15. `BuiltinAux`: Auxiliary bus send/return routing and summing.

##### Phase 3.3: 15 Builtin Instruments & Synths
1. `Builtin3xOsc`: 3-oscillator subtractive synthesizer with detune, mix, and hard sync.
2. `BuiltinAcidBass`: Roland TB-303 emulation with diode ladder filter simulation, accent, and slide.
3. 9 DR8 drum synths (`dr8_kick`, `dr8_snare`, `dr8_hat`, `dr8_tom`, `dr8_clap`, `dr8_cowbell`, `dr8_crash`, `dr8_rim`, `dr8_conga`).
4. `BuiltinEPiano`: FM synthesis electric piano model.
5. `BuiltinFilm`: Dual-oscillator cinematic ambient pad synthesizer.
6. `BuiltinOrgan`: 9-drawbar tonewheel organ model with key click and percussion.
7. `BuiltinSampler`: Multi-zone sample playback engine with loop points and pitch interpolation.

##### Phase 3.4: Core Audio, Project State, History & Parsers
- `engine/core/audio_file.d`: WAV file parser and writer supporting 16-bit, 24-bit, and 32-bit float PCM.
- `engine/core/midi.d`: Standard MIDI File (SMF format 0/1) chunk parser, tempo map builder, and note event extractor.
- `engine/core/history.d`: Circular 50-state undo/redo stack serializing project snapshots.
- `engine/core/audio_editor.d`: Non-real-time destructive editing: Normalize, Reverse, FadeIn, FadeOut, Trim, Gain, Convolve, Blur, and Spectrogram calculation.
- `engine/core/track.d`: Track hierarchy, volume faders, pan curves, automation lanes, and clip scheduling.
- `engine/core/project.d`: Root DAW project graph, project serialization to `.hbk`, and offline audio bouncing.

##### Phase 3.5: IPC Infrastructure & 9 Command Handlers
- `engine/ipc/ipc.d`: Stdout notification framing (`0x48424B49` + LE uint32 length + Protobuf).
- `engine/commands/*.d`: 9 modular command handlers: `handleProjectCmd`, `handleTransportCmd`, `handleTrackCmd`, `handlePluginCmd`, `handleAutomationCmd`, `handleMidiCmd`, `handleModulationCmd`, `handleAudioEditorCmd`, `handleDrumPadCmd`.

##### Phase 3.6: Entry Points & Subprocess Daemons
- `hbk-play`: Main engine executable containing the playback loop and IPC server.
- `hbk-plugin-worker`: Subprocess container executing isolated VST3 plugins.
- `hbk-worker-daemon`: Background daemon coordinating worker socket allocations.

##### Phase 3.7: C++ Boundary Glue
- `engine/vst3/vst3_host.*`: Retained in C++ to encapsulate Steinberg COM interfaces and OS window handles.
- `engine/audio/sound.*`: Retained in C++ to encapsulate OS audio hardware drivers.
- `pocketfft_c_shim.cpp`: Lightweight C wrapper around header-only FFT routines.

### 5.2 Real-Time Interface Contracts (`IPlugin` & Audio)
The real-time plugin interface governs all builtin and external audio processing:

```d
module engine.plugin.iplugin;

/// Host process context passed to real-time DSP plugins.
extern(C++) struct HostProcessContext {
    double sampleRate;
    double tempo;
    long playheadSamples;
    bool isPlaying;
}

/// MIDI note event structure matching C++ binary layout.
extern(C++) struct MidiNoteEvent {
    int noteNumber;
    float velocity;
    int sampleOffset;
    bool isNoteOn;
}

/// Real-time audio plugin interface.
extern(C++) interface IPlugin {
    void reset() @nogc nothrow;
    
    void process(
        float** inputs,
        float** outputs,
        int numSamples,
        const ref HostProcessContext ctx,
        const(MidiNoteEvent)* events,
        size_t numEvents,
        float** sidechain
    ) @nogc nothrow;
    
    void setParameter(uint paramId, float value) @nogc nothrow;
    float getParameter(uint paramId) const @nogc nothrow;
}
```

### 5.3 Regression Testing Strategy & 50 `cc_test` Parity
The Hibiki engine currently includes 50 `cc_test` targets covering all core modules, effects, instruments, and state managers. During migration:
1. Each C++ test target is paired with a corresponding D test target (or adapted via `CcInfo` linking).
2. All 50 tests must pass with zero failures under `bazel test //engine/... -c opt`.
3. Java end-to-end integration tests (`//:backend_manager_test`, `//:plugin_worker_test`, `//:js_repl_test`, `//:acid_house_sdk_e2e_test`) must pass without modification against the D `hbk-play` binary.

---

## 6. Step 4 Specification: Future GUI Migration Plan

### 6.1 Current Java/Clojure GUI Architecture Analysis
The current Hibiki desktop frontend comprises a rich, full-featured graphical user interface implemented in Java Swing, FlatLaf, and Java2D (`src/main/java/hibiki/`), with Clojure scripting and glue (`src/main/clojure/hibiki/`) and a TypeScript/JavaScript REPL powered by Mozilla Rhino:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                               hibiki-gui-java                               │
│                                                                             │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │ TopBar: Transport (Play, Record, Loop), BPM, TimeSig, View Switch     │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│  ┌───────────────┬───────────────────────────────────────────────────────┐  │
│  │ BrowserPane   │ CardLayout: SessionView <──> TimelineView             │  │
│  │ (Samples,     ├───────────────────────────────────────────────────────┤  │
│  │  Presets,     │ PluginPane: Device Chain & ArcKnob Parameter Controls │  │
│  │  Plugins)     ├───────────────────────────────────────────────────────┤  │
│  │               │ Right: Clojure REPL / JavaScript (Rhino) REPL         │  │
│  └───────────────┴───────────────────────────────────────────────────────┘  │
│                                                                             │
│  IPC Layer (IpcClient / BackendManager)                                     │
└─────────────────────────────────────────────────────────────────────────────┘
           │                                                       ▲
           │ stdin: [size LE (4B)][pb.commands.Request]            │ stdout: [0x48424B49 (4B)]
           ▼                                                       │         [size LE (4B)]
┌──────────────────────────────────────────────────────────────────┴─────┐   │         [Notification]
│                     hbk-play (D Engine Backend)                        │   │
│                                                                        │   │
│  ┌──────────────────────────────────────────────────────────────────┐  │   │
│  │ Real-Time Audio Thread (playback_thread, 512 frames, @nogc)      │  │   │
│  └──────────────────────────────────────────────────────────────────┘  │   │
│  ┌──────────────────────────────────────────────────────────────────┐  │   │
│  │ Notification Thread (30Hz: meters, playhead, spectrum, dynamics) │──┴───┘
│  └──────────────────────────────────────────────────────────────────┘
└────────────────────────────────────────────────────────────────────────┘
```

#### Key Frontend Components
1. **`TimelineView` & `TimelineRenderer`** (2,556 lines):
   - Multi-track horizontal timeline, time ruler, loop brackets, and marker lane.
   - Java2D painting for track lanes, audio waveform envelopes, MIDI note thumbnails, cubic Bezier automation curves, playhead cursor, and drag ghosts.
2. **`PianoRoll` & `PianoRollRenderer`** (1,200 lines):
   - 128 vertical piano keys, scrolling note canvas, velocity bars.
   - Note insertion, pitch/tick dragging, edge duration resizing, scale quantization (Major, Minor, Pentatonic, Dorian), and grid snapping.
3. **`SessionView` & `SessionViewIpc`** (852 lines):
   - Non-linear clip matrix launcher (tracks $\times$ scene slots).
   - Per-track channel strips: volume faders, pan knobs, mute/solo/arm toggles, stereo peak meters (`LevelMeter`).
4. **`PluginPane` & Device Panels** (30 panel classes in `ui/panels/devices/`):
   - Parameter control panels for all 15 builtin effects and 15 builtin instruments.
   - Custom rotary arc knobs (`KnobPanel`, `paintArcKnob`).
5. **Theme System (`Theme.java`)**:
   - 6 color palettes (`AbletonDark`, `AbletonLight`, `SolarizedDark`, `SolarizedLight`, `Win95`, `WinXP`).
   - Dynamic HiDPI scaling applied across all widgets.

### 6.2 D GUI Framework Evaluation & Trade-off Matrix
To select the optimal technology for Hibiki's native GUI, the primary options in the D and native ecosystem were evaluated:

| Dimension | DlangUI | Raylib-d | BindBC-Skia (Custom UI) | Dear ImGui (`cimgui` in D) | Recommendation |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Paradigm** | Retained Mode (OOP) | Immediate Mode (Game) | Retained / Immediate Canvas | Immediate Mode (GUI) | **Immediate Mode (`cimgui`)** |
| **Rendering Backend** | CPU blitter / OpenGL | OpenGL (1.1–4.3) | GPU (Metal, Vulkan, GL) | GPU (Metal, GL 3.3, DX11/12) | **GPU-accelerated** |
| **Framerate & Performance** | 30–60 FPS (CPU bound) | 60+ FPS | 60–144 FPS | 60–144 FPS | **60–144 FPS** |
| **GC Pressure on Render Loop** | Heavy (widget tree allocations) | Low | Low | **Zero (completely @nogc)** | **Zero GC steady-state** |
| **Docking / Desktop Layouts** | Supported | Not supported | Must write from scratch | **Built-in (Docking Branch)** | **Docking + Splitters** |
| **Hermetic Bazel Build** | Complex (SDL2, FreeType) | Simple (`cc_library`) | Complex (GN/Ninja / prebuilt) | **Trivial (Clean C++ `cc_library`)** | **Trivial `cc_library`** |
| **Binary Footprint** | ~10 MB | ~5 MB | ~35–45 MB | **~6–8 MB** | **Minimal (<10 MB)** |
| **Audio Industry Track Record** | None | Limited | Avalonia / Flutter | **Standard (Reaper, JUCE, VCV)**| **Proven in DAW/DSP tools**|

### 6.3 Recommended Architecture: Dear ImGui (Docking) + `cimgui` + GLFW
The recommended foundation for the Hibiki native GUI is **Dear ImGui (Docking branch) via the clean `cimgui` C wrapper**, paired with **GLFW** for cross-platform windowing and native GPU contexts:
- **macOS**: GLFW + Apple Metal (`imgui_impl_metal.mm`)
- **Linux**: GLFW + OpenGL 3.3 / Vulkan (`imgui_impl_opengl3.cpp`)
- **Windows**: GLFW + DirectX 11 / DirectX 12 (`imgui_impl_dx11.cpp`)

#### Rationale
1. **Zero Garbage Collection Overhead**:
   Immediate-mode GUI creates zero objects on the D garbage collector during steady-state frame rendering. The entire render loop executes under `@nogc`.
2. **Direct 1:1 Mapping of Java2D Drawing**:
   `TimelineRenderer.java` and `PianoRollRenderer.java` use Java2D operations that map directly to ImGui's `ImDrawList`:
   - `g2.fillRect(...)` $\rightarrow$ `drawList.AddRectFilled(...)`
   - `g2.draw(GeneralPath)` $\rightarrow$ `drawList.AddPolyline(...)`
   - `g2.drawBezier(...)` $\rightarrow$ `drawList.AddBezierCubic(...)`
   - `g2.setClip(...)` $\rightarrow$ `drawList.PushClipRect(...)`
3. **GPU Batching Acceleration**:
   Java2D struggles with complex timelines containing thousands of MIDI notes and audio waveform slices, often consuming 15–30 ms per frame. ImGui batches thousands of draw calls into a single textured vertex/index buffer, rendering full DAW timelines in **< 0.5 milliseconds** on modern GPUs.
4. **Docking, Multi-window, and Menus Built-in**:
   The docking branch of ImGui provides tabbed dockspaces, detachable secondary windows (e.g., detached Piano Roll or Plugin Editor on a secondary display), and resizable splitters out of the box.

#### Out-of-Process Isolation
`hbk-gui` executes as an independent OS process spawning `hbk-play`. Communication occurs via standard pipes using the zero-dependency Protobuf wire serialization (`dproto/`). Any UI hang, modal dialog, file browser freeze, or window resizing glitch cannot pause, jitter, or crash the audio playback engine.

### 6.4 Phased Step 4 Migration Roadmap
The GUI migration is structured into 7 sequential phases:

```
[ Phase 4.1: Windowing & Theme Shell ]
                 │
                 ▼
[ Phase 4.2: IPC Client & State Sync ]
                 │
                 ▼
[ Phase 4.3: Timeline & Waveform View ]
                 │
                 ▼
[ Phase 4.4: Piano Roll & MIDI Editor ]
                 │
                 ▼
[ Phase 4.5: Session View & Clip Matrix ]
                 │
                 ▼
[ Phase 4.6: Device Chain & ArcKnobs ]
                 │
                 ▼
[ Phase 4.7: Scripting REPL & Parity Hardening ]
```

- **Phase 4.1: Windowing, Theme Shell & Bazel Setup**: Build `cimgui` and GLFW hermetically in Bazel. Bootstrap native window with docking layout and 6 theme presets (`gui.theme`).
- **Phase 4.2: D IPC Client & Engine Synchronization**: Port `IpcClient.java` to D. Spawn `hbk-play`, decode notifications, and control transport at 60 Hz.
- **Phase 4.3: Timeline & Waveform Canvas**: Implement `TimelineCanvas` with `ImDrawList`. Render track lanes, waveform envelopes, and cubic Bezier automation curves.
- **Phase 4.4: Piano Roll & MIDI Editor**: Implement 128-key piano roll, note dragging/resizing, scale quantization, and velocity lanes.
- **Phase 4.5: Session View & Clip Launcher**: Implement non-linear clip matrix, channel strips, stereo peak meters, and drag-and-drop file import.
- **Phase 4.6: Device Chain & Parametric Panels**: Implement reusable `drawArcKnob` widget. Generate parameter panels for all 30 devices using compile-time reflection. Embed native VST3 child windows.
- **Phase 4.7: Scripting REPL & Acceptance Hardening**: Embed QuickJS (<1 MB) to execute existing TypeScript SDK automation. Validate parity against `hibiki-gui-java`. Deprecate JVM dependencies.

---

## 7. Development, Formatting, and Git Workflow

### 7.1 Git Branch `d` Management & Push Protocol
In strict adherence to repository guidelines:
1. All D migration development occurs on the dedicated branch `d`.
2. The active branch is verified prior to making changes:
   ```bash
   git branch --show-current  # Must output 'd'
   ```
3. Pushes to remote must establish upstream tracking:
   ```bash
   git push -u origin d
   ```

### 7.2 Pre-commit Formatting Standards (`tools/format.sh`)
Prior to staging or committing any code changes:
1. Execute the repository formatting suite:
   ```bash
   ./tools/format.sh
   ```
2. Verify format compliance (must return exit code 0):
   ```bash
   ./tools/format.sh --check
   ```
3. Markdown documentation files (`docs/*.md`) must adhere to strict formatting: 4-space indentation for D code snippets, proper table alignment, and clean syntax tags.

### 7.3 Milestone-Based Granular Commit Convention
Commits must be structured logically by milestone using clear Conventional Commits headers. Orchestration metadata under `.agents/teamwork/` must NEVER be committed.

| Milestone | Scope | Commit Message Template |
| :--- | :--- | :--- |
| **M1** | Documentation | `docs: add comprehensive D language migration plan (English & Japanese)` |
| **M2** | Step 1 (Bazel + LDC) | `build: configure hermetic LDC toolchain rules and skip Android targets` |
| **M3** | Step 2 (D Protobuf) | `feat(dproto): implement zero-dependency Protobuf wire codec and tests in D` |
| **M4** | Step 3 (Core Engine) | `feat(engine): migrate core audio DSP and control modules to D` |
| **M5** | Final Verification | `test: verify 100% full test suite pass and format compliance for D migration` |

### 7.4 Final Verification Checklist
Before concluding Milestone M1:
- [x] `docs/d.md` (English) exists and covers all 4 steps in exhaustive detail.
- [x] `docs/d_ja.md` (Japanese) exists with exact 1:1 structural parity and glossary compliance.
- [x] Format check passes cleanly via `./tools/format.sh --check`.
- [x] Only documentation deliverables are staged (`git status --short` shows no untracked `.agents/teamwork/` files).
- [x] Commit created on branch `d` and successfully pushed to `origin d`.
