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
