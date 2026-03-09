# Hibiki DAW User Manual

A lightweight Digital Audio Workstation with VST3 plugin support.

## Table of Contents
- [Getting Started](#getting-started)
- [Keyboard Shortcuts](#keyboard-shortcuts)
- [Session View](#session-view)
- [Timeline View](#timeline-view)
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
| **Ctrl+Shift+Z** | Redo |

---

## Session View

The Session View is inspired by Ableton Live's session mode. It shows 4 tracks with 5 clip slots each.

### Track Layout
- **Track Headers**: Click to select a track
- **Clip Slots**: Click to play loaded clips, right-click for context menu
- **Scene Launch**: Click to trigger all clips in a row

### Loading Clips
1. **Drag & Drop**: Drag audio (.wav) or MIDI (.mid) files from your file manager onto a slot
2. **Context Menu**: Right-click a slot → "Load Clip..." to browse for files

### Playing Clips
- **Left-click** a clip slot to play it
- Clip button turns green when playing

---

## Timeline View

The Timeline View shows a linear arrangement of clips on tracks (like traditional DAW timeline).

### Navigation
- **Click** on a track to select it
- Press **Tab** to switch between Session and Timeline views

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

## Playing MIDI

### With a Software Synth
1. Load a synth plugin (e.g., Dexed) on Track 1
2. Load a MIDI file (.mid) into a clip slot on Track 1
3. Click the clip slot to play → MIDI events trigger the synth

### Important
- **MIDI clips require an instrument plugin on the same track** to produce sound
- Audio clips play directly without needing a plugin

### Piano Roll Editor
- Right-click a MIDI clip → "Edit Clip..." to open the Piano Roll
- Edit MIDI notes directly in the piano roll interface

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
3. Projects save: tracks, plugins, clips, BPM, and plugin parameters

### Loading Projects
1. Click the **Load** button in the top bar
2. Browse to a `.hbk` file
3. The project state is restored

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
