# Antigravity Assistant

This document outlines the capabilities and workflow of Antigravity, your AI pairing partner for the Hibiki DAW project.

## Capabilities

### 1. Codebase Exploration & Feature Implementation
- **C++ Backend Development:** Extending VST3 host capabilities, ALSA playback engine (`alsa_out.cpp`), MIDI processing (`midi.cpp`), and Protobuf-based IPC & project files.
- **Java Frontend Development:** Refining and building UI components in the Java Swing frontend (`src/main/java/hibiki`).
- **Build System Configuration:** Managing and updating Bazel builds in `BUILD`, `MODULE.bazel`, or Protobuf schemas.

### 2. Testing and Validation
- Running builds and tests using Bazel (e.g., `bazel test //:all` or `bazel run -c opt //:hibiki-gui-java`).
- Writing and executing unit tests across components.

### 3. Workflow Utilities
- `/goal`: Command to run thorough, long-running tasks.
- `/schedule`: Command to schedule recurring tasks or set timers.
- `/grill-me`: Command to align on implementation design through interactive questions.
