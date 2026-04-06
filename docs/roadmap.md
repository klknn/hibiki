# Hibiki DAW — Roadmap

> Living document tracking short-term and long-term action items.
> Last updated: 2026-03-12

---

## Frontend UI (Java Swing)

### Short-term
- [ ] Mixer view — per-track volume faders, pan knobs, mute/solo buttons
- [ ] Waveform rendering for audio clips in Timeline and Session views
- [ ] Clip resize handles (drag left/right edges to trim)
- [ ] Undo/redo visual feedback (toast or status bar message)
- [ ] Velocity editing in Piano Roll (bar overlay per note)
- [ ] Snap-to-grid toggle and grid resolution selector (1/4, 1/8, 1/16, …)
- [ ] Zoom-to-fit button for Timeline view
- [ ] Multi-select clips and notes (Shift/Ctrl+click, rubber-band selection)

### Long-term
- [ ] Automations lanes — draw and edit parameter automation curves
- [ ] Arrangement markers and loop regions
- [ ] Plugin chain view (drag-to-reorder, bypass toggle)
- [ ] Theme engine — user-selectable color palettes, dark/light modes
- [ ] Accessibility — keyboard-only navigation, screen reader hints
- [ ] Localization / i18n support

---

## Backend Engine (C++)

### Short-term
- [ ] Backend clip deletion — persist delete via IPC (currently GUI-only)
- [ ] Audio clip recording (capture from ALSA/system input to WAV)
- [ ] Real-time audio metering — send peak/RMS levels via IPC for mixer UI
- [ ] MIDI input capture — live record from connected MIDI controller
- [ ] Tempo automation (BPM changes within a project)
- [ ] Click track / metronome output

### Long-term
- [ ] Multi-output bus routing (submixes, sends, returns)
- [ ] Audio effects plugin chain processing (currently instrument-only VST3)
- [ ] Offline bounce/export with realtime-bypass for faster rendering
- [ ] Time-stretching and pitch-shifting for audio clips
- [ ] Audio-to-MIDI conversion
- [ ] MIDI CC / pitch-bend event support in clips and Piano Roll
- [ ] Sample-accurate latency compensation across plugin chains

---

## Multi-Platform

### Short-term (Linux first)
- [x] Linux (ALSA + X11) — primary supported platform
- [ ] Windows (Win32 audio + MSVC) — builds exist, needs CI/testing parity
- [ ] macOS (CoreAudio) — driver stub exists (`coreaudio_out.cpp`), needs end-to-end testing
- [ ] Automated cross-platform CI (GitHub Actions matrix: Linux, Windows, macOS)

### Long-term
- [ ] ChromeOS — investigate Crostini (Linux container) compatibility; may work out-of-the-box with ALSA/PulseAudio bridge
- [ ] Android — evaluate feasibility: Oboe for audio output, Java GUI can reuse Swing-to-Android bridge or rewrite as Jetpack Compose UI
- [ ] Web/WASM — explore compiling the C++ engine to WebAssembly with Web Audio API output
- [ ] Headless / CLI mode for server-side rendering or batch processing (`hbk-play` already exists as a starting point)

---

## Plugin Sandbox Workers

> **Status: Implemented** — core sandbox and remote worker infrastructure is complete.

### Motivation
VST3 plugins run in-process and a misbehaving plugin can crash the entire DAW.
Sandboxing each plugin in a separate worker process improves stability, security, and enables per-plugin resource limits.

### Completed
- [x] IPC mechanism for real-time audio streaming (Unix socket + shared memory)
- [x] Single-plugin-per-process architecture (`hbk-plugin-worker`)
- [x] Crash isolation — worker death doesn't crash the DAW
- [x] `IPlugin` interface (`Vst3Plugin` + `PluginProxy`)
- [x] Cross-platform channel abstraction (`WorkerChannel`)
- [x] Remote TCP daemon (`hbk-worker-daemon`) for cross-OS hosting
- [x] Multi-server round-robin distribution
- [x] UI integration (Settings → Plugins, Browser per-host tree)

### Remaining (long-term)
- [ ] Automatic crash recovery — respawn worker + restore cached state
- [ ] Resource limits — CPU time and memory caps per plugin worker
- [ ] GPU/UI isolation — plugin editor windows hosted in sandbox with XEmbed
- [ ] Security policy — restrict file-system/network access for untrusted plugins
- [ ] Plugin scanning in sandbox — avoid crashes during plugin enumeration
