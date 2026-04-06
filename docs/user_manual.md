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
- `.hbk` files use FlatBuffers binary serialization
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
