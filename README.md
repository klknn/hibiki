# Hibiki DAW

A modern DAW with a Java Swing GUI and a high-performance C++ backend.

## Prerequisites

- **Bazel**: version 7.x or later.
- **Java**: The build uses `remotejdk_21` (Java 21), which Bazel will download automatically.
- **ALSA**: Standard Linux audio development headers/libraries.
- **X11**: Required for VST3 plugin editors.

## Building and Running

### Main Application (Java GUI)

To build and run the complete Hibiki DAW:

```bash
# Build
bazel build -c opt //:hibiki-gui-java --java_runtime_version=remotejdk_21

# Run
bazel run -c opt //:hibiki-gui-java --java_runtime_version=remotejdk_21
```

### Backend Binary (C++)

To build the standalone audio engine backend:

```bash
bazel build -c opt //:hbk-play
```

## Testing

### MIDI Unit Tests (C++)

Verifies the high-performance MIDI parsing engine:

```bash
bazel test //:midi_test -c opt --test_output=all
```

### Backend Integration Tests (Java)

Verifies the binary IPC protocol and real-time plugin loading:

```bash
bazel test //:backend_manager_test -c opt --test_output=all --java_runtime_version=remotejdk_21
```

## Project Structure

- `src/main/java/hibiki`: Java Swing GUI frontend.
- `main.cpp`: C++ audio engine entry point and IPC handler.
- `vst3_host.cpp`: VST3 hosting implementation.
- `midi.cpp`: Custom MIDI parsing engine.
- `testdata/`: Sample MIDI files and test plugins.
