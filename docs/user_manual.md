# Hibiki DAW User Manual

A lightweight Digital Audio Workstation with VST3 plugin support.

## Table of Contents
- [Getting Started](#getting-started)
- [Keyboard Shortcuts](#keyboard-shortcuts)
- [Session View](#session-view)
- [Timeline View](#timeline-view)
- [Automation](#automation)
- [Piano Roll Editor](#piano-roll-editor)
- [Loading Plugins](#loading-plugins)
- [Playing MIDI](#playing-midi)
- [Recording Audio](#recording-audio)
- [Exporting Audio](#exporting-audio)
- [Project Management](#project-management)

---

## Getting Started

### Building
```bash
bazel build -c opt //:hibiki-gui-java
```

### Running
```bash
./bazel-bin/hibiki-gui-java
```

#### Log Verbosity Flags

Both the GUI and `hbk-play` support [abseil logging flags](https://github.com/abseil/abseil-cpp/blob/master/absl/log/flags.h):

| Flag | Values | Description |
|------|--------|-------------|
| `--stderrthreshold=N` | 0=info, 1=warning, 2=error | Log messages at or above this level go to stderr |
| `--minloglevel=N` | 0=info, 1=warning, 2=error | Discard messages below this level |
| `--v=N` | 0, 1, 2, ... | Verbose logging level (higher = more detail) |

```bash
# GUI with verbose engine logging
./bazel-bin/hibiki-gui-java --stderrthreshold=0

# CLI with only errors
./bazel-bin/engine/hbk-play --stderrthreshold=2 project.hbk
```

> **Note:** The GUI defaults to `--stderrthreshold=0` (info). Pass `--stderrthreshold=1` to show only warnings and errors.

---

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| **Space** | Play / Stop toggle |
| **Enter** | Reset playhead to start |
| **Tab** | Toggle Session ↔ Timeline view |
| **1-4** | Select Track 1-4 |
| **Ctrl+Z** | Undo |
| **Ctrl+Shift+Z** / **Ctrl+Y** | Redo |

---

## Session View

The Session View is inspired by Ableton Live's session mode. It shows tracks with clip slots and per-track level meters.

### Track Layout
- **Track Headers**: Click to select a track. **Double-click** to rename.
- **Clip Slots**: Click to play loaded clips, right-click for context menu
- **Scene Launch**: Click to trigger all clips in a row
- **Level Meters**: Real-time stereo peak meters beside each track

### Loading Clips
1. **Drag & Drop**: Drag audio (.wav) or MIDI (.mid) files from your file manager or the Browser panel onto a slot
2. **Context Menu**: Right-click a slot → "Load Clip..." to browse for files

### Playing Clips
- **Left-click** a clip slot to play it
- Clip button turns green when playing

---

## Timeline View

The Timeline View shows a linear arrangement of clips on tracks (like a traditional DAW timeline).

### Navigation
- **Click** on a track label to select it
- **Double-click** a track label to rename it
- Press **Tab** to switch between Session and Timeline views
- **Click** on the time ruler to seek the playhead

### Creating Clips

| Method | Description |
|--------|-------------|
| **Click + drag** in empty area | Draw a new clip (snaps to grid) |
| **Shift + click + drag** | Draw a new clip without snapping |
| **Right-click** empty area → "Create New Clip" | Create a 1-bar clip at the clicked position |
| **Drag & Drop** from Browser | Drop audio/MIDI files onto the timeline |

### Clip Manipulation

| Action | Description |
|--------|-------------|
| **Drag clip** | Move clip to new time/track (snaps to grid) |
| **Shift + Drag** | Move clip without snap (free positioning) |
| **Alt + Drag** | Copy clip to new location (snaps to grid) |
| **Alt + Shift + Drag** | Copy clip without snapping |
| **Right-click clip** → "Edit Clip..." | Open MIDI clip in Piano Roll |
| **Right-click clip** → "Delete Clip" | Remove clip from timeline |

### Zoom and Grid Controls

The control bar at the bottom provides:

- **Grid**: Dropdown to select grid resolution — Auto, 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, and triplet subdivisions (1/3, 1/6, 1/12, 1/24)
- **H** slider: Horizontal zoom (5%–400%)
- **V** slider: Vertical zoom / track height (5%–200%)
- **Auto-scroll**: Checkbox to follow the playhead during playback

---

## Automation

Automation lets you record and edit parameter changes over time. Each track can have multiple automation lanes controlling different plugin parameters.

### Adding Automation Lanes

| Method | Description |
|--------|-------------|
| **Right-click** track header → "Add Automation: \<param>" | Add a lane for the last parameter you adjusted |
| **Right-click** track header → "Add Automation Lane..." | Enter plugin_index,param_id manually |
| **Right-click** empty timeline area → "Add Automation Lane..." | Same as above, from timeline context menu |

> **Tip**: Adjust a plugin slider first, then right-click the track header — the last-touched parameter appears as a quick option.

### Expanding / Collapsing Lanes
- Click the **▶ Auto** indicator at the bottom of a track header to expand automation lanes
- Click **▼ Auto** to collapse them

### Editing Automation Points

| Action | Description |
|--------|-------------|
| **Left-click** empty lane area | Add a new automation point (snaps to grid) |
| **Drag** existing point | Move in time and value (snaps to grid) |
| **Shift + click/drag** | Disable grid snapping for precise positioning |
| **Right-click** a point → "Delete Point" | Remove the point |
| **Right-click** a point → "Tension" | Choose a curve shape preset |

### Tension Handles (Non-Linear Curves)
- Between every two consecutive points, a **cyan circle** appears at the curve midpoint
- **Drag** the handle **up** for ease-out (fast start, slow arrival)
- **Drag** the handle **down** for ease-in (slow start, fast arrival)
- The handle position updates the curve shape in real-time

### Dedicated Automation Editor
- **Right-click** empty area in an automation lane → "Edit Automation..."
- Opens a full-size editor window with:
  - **Beat grid** with bar/beat numbers
  - **Value ruler** (0%–100%)
  - **Zoom controls** (+ / -)
  - All the same click/drag/right-click interactions as the inline editor

### Removing Automation Lanes
- **Right-click** track header → "Remove Automation Lane" → select the lane to remove

### Slider Sync
- Plugin sliders in the **Plugin Panel** automatically follow parameter value changes from automation playback and native plugin GUI edits

---

## Piano Roll Editor

Open the Piano Roll by right-clicking a MIDI clip → "Edit Clip...". The editor provides a grid-based interface for editing MIDI notes.

### Layout
- **Piano keys** on the left side (scrolls vertically with the grid)
- **Note grid** in the center showing notes as colored rectangles
- **Time ruler** at the top showing bar/beat numbers
- **Velocity lane** at the bottom displaying velocity bars per note

### Playhead
- A red vertical line indicates the current playback position
- **Click** or **drag** on the time ruler to seek to a position
- **Middle-click** or **Ctrl+click** on the note grid to seek playhead

### Note Editing

| Action | Description |
|--------|-------------|
| **Left-click** empty area | Create a new note (drag to set duration) |
| **Right-click** a note | Delete the note |
| **Drag** a note | Move the note (pitch + time) |
| **Alt + Drag** a note | Copy the note to a new position |
| **Drag** the right edge of a note | Resize the note duration |

A ghost silhouette shows the original position while dragging.

### Velocity Editing
- The **velocity lane** at the bottom shows a colored bar for each note
- **Click** or **drag** on a velocity bar to adjust (top = 127, bottom = 1)
- Bar color encodes velocity: blue (soft) → red (loud)

### Zoom Controls
- **Grid**: Dropdown to select snap/grid resolution (Auto, 1/4, 1/8, etc.)
- **H** slider: Horizontal zoom (100%–max)
- **V** slider: Vertical zoom / key height (2–30 px)
- On open, the editor auto-fits zoom to show all notes

### Auto-sync
Edits are synced to the backend in real-time via IPC — no manual save step required.

---

## Loading Plugins

### VST3 Plugins
1. Open the **Browser** panel on the left
2. Expand the **Plugins** folder
3. **Double-click** a plugin to load it on the selected track
4. The plugin's VST3 editor window opens automatically

### Plugin Locations
- Linux: `~/.vst3/` and `/usr/lib/vst3/`
- macOS: `~/Library/Audio/Plug-Ins/VST3/`
- Windows: `C:\Program Files\Common Files\VST3\`

### Supported Plugin Types
- **Instruments** (synthesizers): Generate sound from MIDI
- **Effects**: Process audio (EQ, compressor, reverb, etc.)

---

## FilM — FM Synthesizer

FilM is a built-in 6-operator FM synthesizer inspired by Sytrus/FM8. Load it from the Browser under **Builtin → FilM**.

### Architecture
- **6 Operators**: Each with selectable waveform (Sin/Saw/Sq/Tri/Noise), ADSR envelope, LFO, ratio/fine tuning, feedback, and pan
- **3 Filters**: Independent filter modules (LP/HP/BP/LS/HS/Bell) with ADSR envelope, LFO, and dry/wet mix
- **Modulation Matrix**: 6×12 grid controlling FM depth (op→op), filter routing (op→filter), pan, FX send, and output levels
- **8-voice polyphony** with oldest-voice stealing

### UI Layout

The panel is split into two areas:

| Area | Description |
|------|-------------|
| **Left: Tabs** | MAIN, OP1–OP6, F1–F3, FX — click to edit each module |
| **Right: Matrix** | Always-visible 6×12 modulation matrix with rotary knobs |

### Tabs

| Tab | Content |
|-----|---------|
| **MAIN** | Master volume, algorithm selector, unison (voices/detune/spread), portamento, gain envelope |
| **OP 1–6** | Waveform buttons, sub-tabs: **VOL** (level/pan/feedback/ADSR), **PITCH** (ratio/fine), **PHASE** (offset), **LFO** (rate/depth/waveform) |
| **F1–F3** | Filter type buttons, sub-tabs: **CTRL** (cutoff/resonance/depth/mix), **ENV** (filter ADSR), **LFO** (cutoff modulation) |
| **FX** | Reserved for future effects |

### Modulation Matrix

The matrix is a 6-row × 12-column grid of knobs:
- **Columns 1–6**: FM modulation depth between operators (diagonal = self-feedback)
- **Columns F1–F3**: Operator → filter send levels
- **Columns P/FX/O**: Pan, effects send, and direct output

Knobs default to center (neutral). Turn right to increase, left to invert.

### Quick Start
1. Load FilM from Browser → Builtin → FilM
2. Play MIDI notes → hear a pure sine tone (Op1 only by default)
3. Switch waveforms on OP1 tab for different timbres
4. Enable Op2 (set level > 0) and turn matrix knob row 2→col 1 to hear FM modulation
5. Route operators to filters via matrix columns F1–F3

---

## Built-in Audio Effects

### Bitcrusher (`builtin://bitcrusher`)
A lo-fi distortion effect that reduces audio quality through fractional bit-depth quantization and sample-rate reduction.
- **Parameters**:
  - **Drive**: Pre-drive input gain (0 to 24 dB) to color/drive the signal.
  - **Bit Depth**: Resolution reduction (1 to 24 bits), allowing continuous automation.
  - **Sample Rate**: Downsampling range (20 Hz to host sample rate) using a phase accumulator and sample-and-hold.
  - **Wet/Dry**: Mix control between clean and processed audio.
  - **Enable**: Bypass switch.

### Chorus (`builtin://chorus`)
A stereo chorus effect that modulates dual delay lines (5ms to 30ms) using multi-phase LFOs to create thickness, pitch variation, detuning, and stereo spread.
- **Parameters**:
  - **Rate**: LFO modulation frequency (0.1 Hz to 10 Hz).
  - **Depth**: Delay modulation depth (0 ms to 5 ms).
  - **Delay**: Base delay time offset (5 ms to 30 ms).
  - **Feedback**: Feeds delayed signal back into delay lines (0.0 to 0.95).
  - **Wet/Dry**: Mix ratio (default 50% for classic chorus summation).
  - **Enable**: Bypass switch.

### Stereo Width (`builtin://stereo_width`)
A spatial stereo enhancer that delays one channel relative to the other (0 to 40ms) exploiting the Haas effect, coupled with a crossover to keep low frequencies in mono.
- **Parameters**:
  - **Delay**: Haas delay time offset (0 ms to 40 ms).
  - **Channel**: Selects which channel to delay (Left or Right).
  - **Mono Crossover**: Frequency below which audio is forced to mono (50 Hz to 500 Hz) to keep low frequencies focused and phase-aligned.
  - **Width**: Mid/side scaling factor (0.0 to 2.0) applied to high frequencies.
  - **Enable**: Bypass switch.

---

## Built-in Instruments

### Acid Bass (`builtin://acid_bass`)
A monophonic bass synthesizer inspired by the TB-303, featuring band-limited Saw/Square oscillators, a resonant diode-ladder lowpass filter, accent/glide behaviors, and built-in overdrive.
- **Parameters**:
  - **Waveform**: Selects between band-limited Sawtooth (0.0) and Square (1.0) oscillators.
  - **Cutoff**: Base filter cutoff frequency (100 Hz to 3000 Hz).
  - **Resonance**: Resonant peak level (zero-delay feedback loop up to self-oscillation).
  - **Env Mod**: Envelope depth applied to filter cutoff.
  - **Decay**: Filter envelope decay time (0.05s to 3.0s).
  - **Accent**: Boosts volume and drives filter cutoff decay based on MIDI velocity threshold.
  - **Overdrive**: Tanh saturation stage post-filter.
  - **Volume**: Master level.

### Drawbar Organ (`builtin://organ`)
A polyphonic additive synthesizer simulating drawbar organs by summing fundamental and harmonic sine waves. Includes Leslie-style rotary speaker emulation and percussion key click envelope.
- **Parameters**:
  - **Drawbars 1–9**: Relative levels for standard Hammond harmonics (16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1').
  - **Percussion Enable**: Triggers a fast key-click decay at note onset.
  - **Percussion Decay**: Decay time of percussion transient.
  - **Rotary Speed**: Emulates Leslie rotary speaker speed, interpolating between Slow (1.2 Hz) and Fast (6.8 Hz) Doppler vibrato and tremolo.
  - **Volume**: Master level.

### DR8 Drum Synthesizers

Dedicated, minimal synthesis modules inspired by classic analog drum machines like the TR-808.

#### DR8 Kick (`builtin://dr8_kick`)
A punchy kick drum synthesizer utilizing a pitch-swept sine wave oscillator, an exponential amplitude decay envelope, a short high-frequency noise transient click, and a soft-clipping distortion stage.
- **Parameters**:
  - **Pitch**: Base oscillator frequency (40 Hz to 80 Hz).
  - **Decay**: Amplitude envelope decay time (0.05s to 1.0s).
  - **Pitch Env Decay**: Pitch sweep decay time (0.01s to 0.15s).
  - **Pitch Env Depth**: Pitch sweep depth added to the base pitch (0 Hz to 300 Hz).
  - **Click Level**: Level of the short noise-click transient (0.0 to 1.0).
  - **Distortion**: Soft-clipping overdrive drive depth (0.0 to 1.0).
  - **Volume**: Master level.

#### DR8 Snare (`builtin://dr8_snare`)
A snare drum synthesizer that splits the sound generator into two components: a resonant skin body (two detuned sine wave oscillators tuned to a fundamental and a 1.6x harmonic) and snare wires (a white noise generator passed through a resonant high-pass filter).
- **Parameters**:
  - **Pitch**: Fundamental skin pitch (100 Hz to 250 Hz).
  - **Decay**: Skin body tone decay time (0.05s to 0.5s).
  - **Noise Level**: Snare wire noise volume (0.0 to 1.0).
  - **Noise Decay**: Snare wire noise decay time (0.05s to 1.0s).
  - **Noise HPF**: High-pass filter cutoff frequency for the noise (800 Hz to 8000 Hz).
  - **Tone/Noise Mix**: Mix balance between skin body tone (0.0) and wire noise (1.0).
  - **Volume**: Master level.

#### DR8 Hat (`builtin://dr8_hat`)
A hihat synthesizer emulating the classic TR-808 metallic sound source by summing 6 detuned square wave oscillators, then processing the sum through a resonant bandpass filter and high-pass filter cascade.
- **Parameters**:
  - **Decay**: Amplitude decay time (0.02s to 0.8s).
  - **HPF Freq**: High-pass filter cutoff frequency (3 kHz to 12 kHz).
  - **BPF Freq**: Bandpass filter center frequency (6 kHz to 15 kHz).
  - **Tension**: Detuning tension factor of the 6 oscillators (0.0 to 1.0).
  - **Volume**: Master level.

#### DR8 Tom (`builtin://dr8_tom`)
A tom synthesizer featuring a pitch-swept sine wave oscillator passed through a low-pass filter for a clean, woody tone, combined with an initial noise attack click.
- **Parameters**:
  - **Pitch**: Base oscillator frequency (70 Hz to 200 Hz).
  - **Decay**: Amplitude envelope decay time (0.1s to 1.5s).
  - **Pitch Env Decay**: Pitch sweep decay time (0.02s to 0.3s).
  - **Pitch Env Depth**: Pitch sweep depth added to base pitch (0 Hz to 100 Hz).
  - **Noise Attack**: Initial attack noise click level (0.0 to 1.0).
  - **Volume**: Master level.

#### DR8 Clap (`builtin://dr8_clap`)
A hand clap synthesizer utilizing a white noise sound source modulated by a multi-trigger amplitude envelope (3 rapid pre-claps followed by a main decay tail), processed through a resonant bandpass filter.
- **Parameters**:
  - **Decay**: Main tail decay time (0.05s to 1.0s).
  - **Filter Cutoff**: Bandpass filter center frequency (500 Hz to 3000 Hz).
  - **Spread**: Micro-trigger timing spread (5ms to 20ms).
  - **Volume**: Master level.

#### DR8 Cowbell (`builtin://dr8_cowbell`)
A cowbell synthesizer utilizing two detuned square wave oscillators passed through a resonant bandpass filter, modulated by an exponential amplitude decay envelope.
- **Parameters**:
  - **Pitch**: Base tuning frequency (400 Hz to 700 Hz).
  - **Decay**: Amplitude envelope decay time (0.05s to 0.5s).
  - **Detune**: Detuning ratio between oscillators (0.0 to 1.0).
  - **Volume**: Master level.

---

## Plugin Hosting Modes

Hibiki supports three modes for running VST3 plugins, selectable in
**Settings → Plugins**:

### In-Process (Default)

Plugins run directly in the audio engine process.

| Pro | Con |
|-----|-----|
| Lowest latency | Plugin crash kills Hibiki |
| Zero overhead | No isolation |
| Simplest setup | — |

Best for: trusted, stable plugins during production work.

### Local Sandbox

Each plugin runs in its own child process (`hbk-plugin-worker`),
communicating via Unix socket + shared memory.

| Pro | Con |
|-----|-----|
| Crash isolation | ~1ms overhead per block |
| Parallel execution | Slightly higher memory usage |
| Debuggable per-plugin | Linux/macOS only |

**Setup**: Select **Local Sandbox** in Settings → Plugins. No further
configuration needed — worker processes are spawned automatically.

Best for: development, testing untrusted plugins.

### Remote (TCP)

Plugins run on remote machines via `hbk-worker-daemon`, enabling
cross-platform plugin hosting and CPU offloading across multiple servers.

| Pro | Con |
|-----|-----|
| Cross-machine | ~1-5ms LAN latency |
| Cross-platform | Requires network |
| CPU offloading | ~180 KB/s per plugin |
| Multi-server | Initial scan delay |

**Setup on each remote machine:**
```bash
bazel build -c opt //:hbk-worker-daemon
./bazel-bin/hbk-worker-daemon --port 9100
```

**Setup in Hibiki (multi-server):**
1. Open **Settings → Plugins**
2. Set **Hosting Mode** to `Remote (TCP)`
3. Use **+** / **−** to add/remove hosts (e.g., `mac-studio:9100`, `win-pc:9100`)
4. Click **Apply** to save, then **Scan Remote** to discover plugins
5. Remote plugins appear in the Browser under **📡 host:port** tree nodes

Plugins are distributed across servers round-robin as they are loaded.

**Network requirements:**

| Parameter | Value |
|-----------|-------|
| Protocol | TCP |
| Default port | 9100 |
| Bandwidth | ~180 KB/s per plugin (44.1kHz/512) |
| Latency | ~1-5ms LAN |
| Security | None (LAN-only) |

### Finding Your Machine's IP Address

| OS | Command | Example Output |
|----|---------|----------------|
| **Linux** | `hostname -I` | `192.168.1.50` |
| **macOS** | `ipconfig getifaddr en0` | `192.168.1.51` |
| **Windows** | `ipconfig` (look for IPv4 Address) | `192.168.1.52` |

> **Tip:** On macOS, use `en0` for Wi-Fi and `en1` for Ethernet.
> On Linux, `ip addr show` gives more detail if `hostname -I` lists multiple addresses.

Best for: running macOS/Windows-only plugins from Linux, distributing
CPU load across machines, or using specialized hardware on remote hosts.

### Latency Tips

Choose the right mode + buffer size for your scenario:

| Scenario | Recommended Mode | Buffer Size | Expected Latency |
|----------|-----------------|-------------|------------------|
| Live monitoring | In-Process | 128–256 | 3–6 ms |
| Tracking (recording) | In-Process | 256–512 | 6–12 ms |
| Mixing (untrusted plugins) | Local Sandbox | 512 | ~12 ms |
| Bouncing/export | Any | 1024–2048 | N/A (offline) |
| Cross-OS plugins | Remote TCP | 512+ | 13–17 ms |
| Multi-machine render | Remote TCP | 1024+ | N/A (offline) |

> **Tip:** Latency only matters for **live monitoring**.
> When bouncing/exporting, larger buffers reduce CPU load without
> affecting the rendered output quality.

> **Tip:** Wired Ethernet adds ~0.5ms. WiFi can add 3–15ms of
> jitter — use wired connections for remote plugin hosting.

## Playing MIDI

### With a Software Synth
1. Load a synth plugin (e.g., Dexed) on Track 1
2. Load a MIDI file (.mid) into a clip slot on Track 1
3. Click the clip slot to play → MIDI events trigger the synth

### Important
- **MIDI clips require an instrument plugin on the same track** to produce sound
- Audio clips play directly without needing a plugin

---

## Recording Audio

> 🚧 **Coming Soon** - Recording functionality is under development

### Planned Features
- Arm track for recording
- Input monitoring
- Punch-in/out recording

---

## Exporting Audio

### GUI Export
> 🚧 **Coming Soon** - Export dialog is under development

### Command-Line Export
Use the `hbk-play` CLI tool for offline rendering:

```bash
# Basic playback
./bazel-bin/hbk-play project.hbk

# Export to WAV (specify output duration)
./bazel-bin/hbk-play project.hbk -o output.wav --max-duration 30
```

### Options
| Flag | Description |
|------|-------------|
| `-o <file>` | Output WAV file path |
| `--max-duration <sec>` | Maximum render duration in seconds |

---

## Project Management

### Saving Projects
1. Click the **Save** button in the top bar
2. Enter a filename (`.hbk` extension is added automatically)
3. Projects save: tracks, plugins, clips, BPM, track names, and plugin parameters

### Loading Projects
1. Click the **Load** button in the top bar
2. Browse to a `.hbk` file
3. The project state is restored (including track names and clip positions)

### Project File Format
- `.hbk` files use Protobuf binary serialization
- Compact and efficient for quick load times

---

## Settings

Click the **⚙** (gear) button in the top bar to open settings:

- **Theme**: Choose from Ableton Dark/Light, Solarized, Win95, WinXP
- **Scaling**: Adjust UI scale (auto-detected on Linux via gsettings)
- **Font**: Choose font family and size
- **BPM**: Set project tempo (also editable directly in top bar)

---

## Troubleshooting

### No Sound from MIDI Clips
- Ensure you have a **synth plugin loaded** on the same track as the MIDI clip
- Check that the synth plugin initialized correctly (look at console output)

### Plugins Not Showing
- Make sure VST3 plugins are in the correct directory
- Check console for plugin loading errors

### HiDPI Scaling Issues (Linux)
- Set `GDK_SCALE=2` environment variable, or
- The app auto-detects scaling via `gsettings`

---

## Support

- GitHub Issues: [klknn/hibiki](https://github.com/klknn/hibiki)
- Source code in `src/main/java/hibiki/` (GUI) and `*.cpp` (engine)
