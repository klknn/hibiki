# Hibiki DAW

A modern DAW with a Java Swing GUI and a high-performance C++ backend.

## Prerequisites

- **Bazel**: version 9.0 or later. Download [bazelisk](https://github.com/bazelbuild/bazelisk/releases) and copy it to `/usr/bin/bazel`.
- **ALSA**: Standard Linux audio development headers/libraries.
- **X11**: Required for VST3 plugin editors.

## For developers

### Main Application (Java GUI)

To build and run the complete Hibiki DAW:

```bash
bazel run -c opt //:hibiki-gui-java
```

## Testing

Verifies the high-performance C++ engine and Java GUI frontend:

```bash
bazel test //:all -c opt --test_output=all
```

## Project Structure

Common
- `hibiki_*.fbs`: Flatbuffer schema for IPC and project files.- `testdata/`: Sample MIDI files and test plugins.

Audio engine backend
- `main.cpp`: C++ audio engine entry point and IPC handler.
- `vst3_host.cpp`: VST3 hosting implementation.
- `midi.cpp`: MIDI event library.
- `alsa_out.cpp`: ALSA audio playback.

GUI frontend
- `src/main/java/hibiki`: Java Swing GUI frontend.
