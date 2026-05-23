# Built-in Devices

Hibiki includes 4 built-in audio devices: 2 effects (EQ, Compressor) and 2 instruments (3xOsc, Sampler). This document describes the registration flow, engine processing, proto notifications, and Java UI panels for each.

## Registration

Plugins are loaded in `track.cpp` `LoadPlugin()` by matching `builtin://` paths:

```cpp
if (path == BuiltinEq::kPath)     plugin = std::make_unique<BuiltinEq>();
if (path == BuiltinCompressor::kPath) plugin = std::make_unique<BuiltinCompressor>();
if (path == Builtin3xOsc::kPath)  plugin = std::make_unique<Builtin3xOsc>();
if (path == BuiltinSampler::kPath) plugin = std::make_unique<BuiltinSampler>();
```

All implement `IPlugin` (`engine/plugin/iplugin.hpp`). Parameters use normalized 0-1 range.

---

## EQ Eight (`builtin://eq`)

**Engine**: `engine/effects/builtin_eq.hpp/cpp`
- 8-band parametric EQ using biquad filters (Audio EQ Cookbook formulas).
- Filter types: Off, LPF, HPF, Low Shelf, High Shelf, Bell.
- FFT spectrum analyzer via `SpectrumAnalyzer` (`engine/core/fft.hpp/cpp`).
- 33 parameters: 8×(type, freq, gain, Q) + 1 enable.

**Proto notifications**:
- `PluginParamChange` → per-band parameter updates.
- `PluginSpectrumMagnitudes` → input/output FFT spectrum (64 bins).
  - Sent from `main.cpp`: `dynamic_cast<BuiltinEq*>(plugin.get())` → `getSpectrumData()`.

**Java UI**: `src/main/java/hibiki/ui/EqDevicePanel.java` (678 LOC)
- `CurvePanel`: Draggable band handles on a frequency response graph.
  - Mouse: drag to move freq/gain, shift-drag for Q, double-click to toggle band.
  - `drawSpectrumOverlay()`: Renders FFT input/output spectrum behind the curve.
- `setSpectrumData()`: Called by `PluginPane` when spectrum notification arrives.
- `updateParam()`: Called by `PluginPane` on `PluginParamChange`.

---

## Compressor (`builtin://compressor`)

**Engine**: `engine/effects/builtin_compressor.hpp/cpp`
- Stereo compressor with adjustable threshold, ratio, attack, release, knee, makeup.
- Provides gain reduction, input, and output level metering via `std::atomic<float>`.
- 7 parameters.

**Proto notifications**:
- `PluginParamChange` → parameter updates.
- `PluginMeteringData` → gain reduction dB, input dB, output dB.
  - Sent from `main.cpp`: `dynamic_cast<BuiltinCompressor*>()` → `getGainReductionDb()`, `getInputDb()`, `getOutputDb()`.

**Java UI**: `src/main/java/hibiki/ui/CompressorDevicePanel.java` (454 LOC)
- `TransferCurvePanel`: Input-vs-output dB transfer curve with threshold line, knee region, and live input/output dot.
- `GrMeterPanel`: Vertical gain reduction meter.
- `KnobPanel`: Slider-based knob with value label formatting per parameter.
- `setGainReduction()`, `setInputOutputLevel()`: Called by `PluginPane`.

---

## 3xOsc (`builtin://3xosc`)

**Engine**: `engine/instruments/builtin_3xosc.hpp/cpp`
- 3-oscillator synthesizer (FL Studio 3xOSC-style).
- Waveforms: sine, saw, square, triangle.
- Per-osc: waveform, coarse/fine tune, volume, pan.
- Global: gain ADSR, filter (LP/HP/BP via `BiquadFilter`) with ADSR modulation.
- 8-voice polyphony with oldest-note stealing.
- 29 parameters.

**Proto notifications**:
- `PluginParamChange` → parameter updates.

**Java UI**: `src/main/java/hibiki/ui/ThreeOscDevicePanel.java` (331 LOC)
- 3 oscillator rows: waveform selector, coarse/fine/volume/pan knobs.
- ADSR sections (gain + filter) with knob groups.
- Filter section: type dropdown, cutoff, resonance, depth.
- `KnobPanel`: Arc-knob with mouse drag interaction.
- `handleParamChange()`: Called by `PluginPane`.

---

## Sampler (`builtin://sampler`)

**Engine**: `engine/instruments/builtin_sampler.hpp/cpp`
- Single-waveform sampler (Ableton Simpler-style).
- Loads WAV via `LoadWav()`, pitched playback based on root note.
- 8-voice polyphony, gain ADSR, filter with ADSR modulation.
- 128-point waveform summary for UI display.
- 17 parameters.

**Proto notifications**:
- `PluginParamChange` → parameter updates.
- `PluginSampleData` → waveform summary + sample name.
  - Sent from `commands.cpp`: `dynamic_cast<BuiltinSampler*>()` → `loadSample()` → `getWaveformSummary()`.

**Java UI**: `src/main/java/hibiki/ui/SamplerDevicePanel.java` (488 LOC)
- `WaveformPanel`: Waveform display with draggable start/end markers, DnD file drop support.
- Root note knob, gain ADSR, filter section (type/cutoff/resonance/depth).
- `updateWaveform()`: Called by `PluginPane` when `PluginSampleData` notification arrives.
- `sendLoadSample()`: Sends `ACTION_LOAD_SAMPLE` command to backend.

---

## Architecture Diagram

```
┌──────────────┐     IPC (protobuf)     ┌──────────────────┐
│  Engine      │ ───────────────────────▸│  Java UI          │
│              │                        │                    │
│  BuiltinEq   │◀─ setParameterValue ── │  EqDevicePanel     │
│  → process() │── SpectrumMagnitudes ─▸│  → setSpectrumData │
│              │── ParamChange ────────▸│  → updateParam     │
│              │                        │                    │
│  BuiltinComp │◀─ setParameterValue ── │  CompressorPanel   │
│  → process() │── MeteringData ──────▸│  → setGainReduction │
│              │── ParamChange ────────▸│  → updateParam     │
│              │                        │                    │
│  Builtin3xO  │◀─ setParameterValue ── │  ThreeOscPanel     │
│  → process() │── ParamChange ────────▸│  → handleParam     │
│              │                        │                    │
│  BuiltinSamp │◀─ setParameterValue ── │  SamplerPanel      │
│  → process() │── SampleData ────────▸│  → updateWaveform  │
│              │── ParamChange ────────▸│  → handleParam     │
└──────────────┘                        └──────────────────┘
```

---

## Creating a New Built-in Device

Follow these step-by-step instructions to create, register, and wire a new built-in effect or instrument:

### 1. Engine Backend (C++)

1. **Define the Device**:
   - Create a header and source file in `engine/effects/` (for effects) or `engine/instruments/` (for instruments).
   - Inherit your class from `IPlugin` (`engine/plugin/iplugin.hpp`).
   - Define a static unique path and name:
     ```cpp
     static constexpr const char* kPath = "builtin://my_device";
     static constexpr const char* kName = "My Device";
     ```
   - Implement the DSP processing in `process(const float** inputs, float** outputs, int num_samples)`.

2. **Register the Plugin**:
   - Include your new header in `engine/core/track.cpp`.
   - In `LoadPlugin(const std::string& path)`, map the path to instantiate your class:
     ```cpp
     if (path == BuiltinMyDevice::kPath) plugin = std::make_unique<BuiltinMyDevice>();
     ```

3. **Update Build Configuration**:
   - Open `engine/effects/BUILD` (or `engine/instruments/BUILD`) and add your `.hpp`/`.cpp` files to the target's `srcs`/`hdrs` lists.
   - Add any dependency libraries or target definitions if needed.

---

### 2. IPC & Real-time Notifications (Protobuf)

If your device needs to send custom visualization data (e.g. metering, spectrum analysis) to the Java front-end:

1. **Update Proto definition**:
   - Open `pb/notifications.proto`.
   - Add fields to the relevant message (e.g., `PluginMeteringData` or define a new message).

2. **Update IPC Helper**:
   - Update `engine/ipc/ipc.hpp`/`ipc.cpp` to add or modify serialization functions that accept your device's metering data and publish notifications.

3. **Wire Event loop**:
   - In `engine/main.cpp` (within the IPC notification thread loops), query your plugin class type and send data:
     ```cpp
     if (auto* dev = dynamic_cast<BuiltinMyDevice*>(plugin.get())) {
       hibiki::pb::notifications::PluginMeteringData meter;
       // ... populate and call sendPluginMeteringData(meter) ...
     }
     ```

---

### 3. Front-end UI (Java)

1. **Implement UI Panel**:
   - Create a new class extending `AbstractDevicePanel` in `src/main/java/hibiki/ui/panels/devices/`.
   - Implement the layout, knobs (using `KnobPanel`), and paint custom components for visualizers/meters if needed.
   - Assign final visual fields at the top of the constructor to avoid definite-assignment analysis issues in callbacks.

2. **Register UI Class**:
   - In `src/main/java/hibiki/ui/PluginPane.java`, add your device to the `BUILTIN_DEVICE_PANELS` map:
     ```java
     Map.entry("My Device", MyDevicePanel.class)
     ```
   - If handling custom metering notifications, update `handlePluginMetering` or the notification parser in `PluginPane` to pass the payload to your panel.

3. **Add to Browser List**:
   - Open `src/main/java/hibiki/ui/BrowserPane.java` and find `populateTree()`.
   - Instantiation and register a `FileItem` under the "Built-in" node:
     ```java
     FileItem myDeviceItem = new FileItem(new File("builtin"), "builtin", "My Device", "Hibiki", 0);
     myDeviceItem.rawPath = "builtin://my_device";
     builtinNode.add(new DefaultMutableTreeNode(myDeviceItem));
     ```

---

### 4. Testing & Verification

1. **Unit Tests**:
   - Create a test file (e.g., `engine/effects/builtin_my_device_test.cpp`) using `gtest`.
   - Add the test to the `BUILD` file.
   - In `src/test/java/hibiki/ui/PluginPaneExtendedTest.java`, add `"My Device"` to the `builtinNames` array to verify mapping completeness.
   - Run tests:
     ```bash
     bazel test //...
     ```

---

## Advanced Tips & Best Practices (Lessons from Drum Machine)

If you are building a complex or container-style built-in device (like the Drum Machine), keep the following architectural lessons in mind:

### 1. Host-Child Plugin Serialization
- If a built-in device hosts child plugins, ensure you recursively serialize and deserialize their states. 
- In your C++ state capture (e.g., `BuildProjectProto` in `engine/core/project.cpp`), call the child plugin's state serialization methods inside the parent's serialization routines.

### 2. Thread-Safety & Deadlock Prevention
- **Backend Mutex Locks**: Avoid calling IPC notification-dispatching functions (e.g., `sendNotification` or `sendPadState`) from within a mutex-locked scope in C++ if those notifications trigger callbacks that require engine locks. This prevents cross-thread deadlock between the audio process/IPC worker threads and the main UI thread.

### 3. Java Swing UI Initialization & Lambdas
- **Initialization Order**: In your Swing panel constructors, always initialize labels, lookups, and dependent visual components *before* adding event listeners to controls (such as `JSpinner` or sliders). Adding listeners that reference local controls before they are fully declared can cause "variable might not have been initialized" errors in lambda contexts.

### 4. Drag & Drop for Built-ins
- Ensure drag handlers preserve the `rawPath` schema (`builtin://...`) instead of trying to resolve them as filesystem files. Check `BrowserPane.java` and your drop-target panels to allow drop gestures to distinguish between builtin instruments and raw audio files.

### 5. Preserving In-memory MIDI State (Undo/Redo)
- Timeline clips and session slots containing empty paths (`""`) are treated as in-memory clips (e.g., manually composed MIDI notes).
- Ensure that the project serialization helper (`engine/core/project.cpp`) serializes the note array (`midi_events`), display name, and waveform summaries into the project protobuf so they survive undo/redo captures and apply routines.
