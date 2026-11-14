# Hibiki Native GUI Migration Plan (`dlangui` + SDL2)

## 1. Overview & Motivation

Hibiki's desktop GUI currently lives in `src/main/java/hibiki/ui/` (114 Java files, ~15,800 LOC in Swing), communicating with the native engine (`hbk-play`) over length-prefixed Protobuf (`HBKI` framing on `stdin`/`stdout`).

Migrating the GUI to D using **[`dlangui`](https://github.com/buggins/dlangui)** eliminates the JVM runtime dependency while preserving a clean retained-mode OOP widget architecture that maps closely to Swing (`MainView`, `TimelineView`, `SessionView`, `PianoRoll`, `PluginPane`, `BrowserPane`, `AutomationEditor`).

---

## 2. Aesthetic Direction: Y2K DAWs vs. Modern Vector (FL Studio 2026)

| Dimension | Y2K Workstation (FL Studio 8 / Logic 5 / Pro Tools 5) | Modern Vector (FL Studio 2026) |
| :--- | :--- | :--- |
| **Visual Language** | 1px raised/sunken bevels, brushed/matte panels, segmented LED meters, LCD transport readouts, 9-patch skins, dense toolbars | Anti-aliased vector knobs, smooth Bezier curves, soft drop shadows, scalable SVG icons, rounded translucent overlays |
| **Fit with `dlangui`** | **Excellent (Native)** — `dlangui`'s `DrawBuf`, 9-patch PNG skinning (`res/`), and integer-aligned `Rect` primitives render crisp 1px bevels and hardware-inspired controls directly | **Moderate / Extra Work** — `dlangui`'s `DrawBuf` lacks built-in anti-aliased vector paths (no Skia/NanoVG), requiring custom software AA rasterization in `ColorDrawBuf` or OpenGL shaders |
| **Recommended Hybrid** | **Y2K Tactile Geometry + HiDPI Crispness**: Use `dlangui`-native 1px bevels, segmented LEDs, and dense FL8/Logic5/PT5 panel hierarchy paired with clean FreeType fonts and cohesive dark/slate themes. | Optionally add custom AA `ColorDrawBuf` helpers later only where curves matter most (automation tension splines, rotary arc knobs). |

**Conclusion:** The **Y2K DAW aesthetic (FL Studio 8 / Logic 5 / Pro Tools 5)** is significantly more feasible and natural in `dlangui`, while still benefiting from sharp HiDPI FreeType text and modern dark/light theme palettes.

---

## 3. Bazel Integration Architecture

`dlangui` (SDL backend) has a minimal dependency graph:
- **D dependencies**: `dlangui` (`-d-version=USE_SDL -d-version=USE_FREETYPE -d-version=USE_OPENGL`), `bindbc-sdl`, `bindbc-freetype`, `bindbc-loader`.
- **C/System libraries**: `SDL2`, `FreeType2` (via Bazel `http_archive` / BCR or system dynamic loading via `bindbc`).

```
//gui_d:hibiki_gui (d_binary)
  ├── //pb:commands_d_proto, //pb:notifications_d_proto, //pb:core_d_proto
  ├── //dproto
  ├── //engine/ipc:ipc_d
  └── @dlangui//:dlangui (d_library)
        ├── @bindbc_sdl//:bindbc_sdl
        ├── @bindbc_freetype//:bindbc_freetype
        └── @bindbc_loader//:bindbc_loader
```

---

## 4. Phased Migration Roadmap

### Phase 4.0 — Bazel + `dlangui` Proof of Concept
- Add `http_archive` rules in `MODULE.bazel` for `dlangui`, `bindbc-sdl`, `bindbc-freetype`, and `bindbc-loader` with `third_party/dlangui.BUILD`.
- Verify compilation under LDC 1.41.0 (`-d-version=USE_SDL -d-version=USE_FREETYPE -d-version=USE_OPENGL`).
- Create `//gui_d:hibiki_gui` opening a themed SDL2 window on macOS/Linux.

### Phase 4.1 — Engine Process Host & IPC Client (`gui_d/ipc_client.d`)
- Port `BackendManager.java` / `IpcClient.java` using `std.process.pipeProcess` to spawn `hbk-play`.
- Read `HBKI`-framed Protobuf `Notification`s on a background thread using [`dproto`](../pb/dproto.d) and marshal UI updates onto `dlangui`'s main thread via `window.executeInUiThread(...)`.

### Phase 4.2 — Main Shell, Theme & Session View (`gui_d/theme.d`, `gui_d/top_bar.d`, `gui_d/session_view.d`)
- Implement the Y2K-inspired theme system (1px bevel helpers, segmented LED meter palette, LCD transport font/panel).
- Port `TopBar` (Play/Stop/Record, BPM, Loop, CPU/Peak meters), `MenuBarFactory`, and `SessionView` (clip launcher grid + mixer strips).

### Phase 4.3 — Custom Canvas Editors (`gui_d/timeline_view.d`, `gui_d/piano_roll.d`, `gui_d/automation_editor.d`)
- Subclass `dlangui.widgets.widget.Widget` overriding `onDraw(DrawBuf buf, Rect rc)` and `onMouseEvent` / `onKeyEvent`.
- Port `TimelineView` (waveform peaks, clip move/trim/fade), `PianoRoll` (MIDI note grid, velocity bars, scale snap), and `AutomationEditor` (power-curve tension interpolation).

### Phase 4.4 — Device Rack, Browser & VST3 Framebuffer (`gui_d/plugin_pane.d`, `gui_d/browser_pane.d`)
- Implement tactile Y2K rack-style device panels, rotary knobs, and `BrowserPane` file tree.
- Render remote VST3 `EditorFrameData` RGBA buffers directly into `dlangui`'s `ColorDrawBuf`.

### Phase 4.5 — Settings, Scripting & Cutover
- Port `SettingsDialog` and scripting/CLI integration.
- Verify feature parity against `hibiki-gui-java` and deprecate JVM GUI targets.
