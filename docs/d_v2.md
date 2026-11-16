# Hibiki `gui_d` Live End-to-End Integration Architecture (`d_v2`)

> **Goal**: Turn the `dlangui`-based Y2K/FL-inspired `gui_d` frontend into a fully functional, end-to-end live DAW connected to the `hbk-play` C++/D real-time audio engine over Protobuf/HBKI IPC.

---

## 1. Current Audit & Architectural Gaps

The existing `gui_d` prototype renders FL-style windows (Toolbar, Browser, Channel Rack + Graph Editor, Playlist, Piano Roll, Mixer + FX Rack + 3-Band EQ, Plugin Wrapper) and holds local UI state in `TransportModel`, while the `hbk-play` backend (`engine/main.d` + `engine/playback_thread.cpp` + `engine/commands/` + `engine/core/`) already implements real-time 512-sample audio mixing, built-in instruments/effects, VST3/CLAP hosting, MIDI/Timeline/Session clip playback, automation, recording, and WAV bouncing.

Key disconnects discovered during the audit:

1. **Executable Discovery & Lifecycle (`gui_d/main.d`)**
   - `resolveEnginePath` checks workspace-relative paths only, missing Bazel runfiles / sibling binary paths when invoked via `bazel run //gui_d:hibiki_gui`.
2. **Startup Desync & Track Topology**
   - `gui_d` initializes `model.initDefaultTemplate()`, while `hbk-play` boots with `tracks = {}`, `bpm = 140`. Pressing Play or triggering `Z..M` produces silence.
   - `gui_d` routes Channel Generators (`Kick`, `Snare`, `Hi-Hat`, `Acid Bass`, `E-Piano`) to Mixer Inserts (`1..5`), whereas `Track::LoadPlugin` forces `isInstrument()` plugins into slot `0` and effects after it on the *same* `track_index`. Using the same index for both causes slot `0` collisions.
   - `playback_thread.cpp` mixes normal tracks -> group/aux tracks -> `d_process_master_output`, without treating Track `0` as a true Master FX/Gain/Pitch bus or providing per-track 3-band EQ, stereo separation, and wet/dry FX mix.
3. **Step Sequencer / Channel Rack / Pattern / Song Mode**
   - Channel Rack step pads, Graph Editor parameters (`vel`/`pitch`/`pan`/`shift`), Swing, Patterns, and `PAT` vs `SONG` transport mode exist only in `gui_d/transport_model.d`, with no backend execution or sync.
4. **Incomplete `SyncProjectToGui` & Telemetry**
   - `SyncProjectToGui` omits project `bpm`, transport state, track volume/pan/mute/solo/routing/EQ/stereo-sep, plugin bypass & current parameter values, and MIDI clip note payloads.
   - `Toolbar` oscilloscope & CPU meter use mock `sinf()` and hardcoded `cpuPercent = 3`.
5. **Missing UI <-> IPC Bindings**
   - Playlist lacks IPC sync for clip move, resize, slice, mute, clone/copy, track mute/solo, automation point edits, and double-click clip -> `MIDI_ACTION_GET` -> Piano Roll.
   - Plugin Wrapper lacks live parameter list binding, preset selector, bypass sync, VST3 remote framebuffer rendering/input, EQ spectrum, compressor metering, phaser scope, sampler WAV load, and drum machine pad triggers.
   - Browser sample list uses non-existent sample filenames; `sendPluginList` omits `PluginDescription.path`; File Open/Save/Bounce/Settings/Undo/Redo lack real interactive modals or full backend wiring.

---

## 2. Target End-to-End Architecture

### 2.1 Track Topology & Signal Routing

```
  [Channel Generator Tracks (100 + channelIdx)]
       │  e.g. Ch 0 Kick    (track 100) ──> builtin://dr8_kick  ──┐
       │       Ch 1 Snare   (track 101) ──> builtin://dr8_snare ──┼── output_track_index = ch.mixerInsert (1..12)
       │       Ch 2 Hi-Hat  (track 102) ──> builtin://dr8_hat   ──┤
       │       Ch 3 Acid    (track 103) ──> builtin://acid_bass ──┤
       │       Ch 4 E-Piano (track 104) ──> builtin://epiano    ──┘
       ▼
  [Mixer Insert Bus Tracks (1..12, TrackType = GROUP)]
       │  Per-Insert FX chain slots 0..9 (wet/dry mix + enable/bypass)
       │  Built-in 3-Band EQ (Low/Mid/High) + Stereo Separation + Pan + Fader + Mute/Solo
       │  Route ──> output_track_index = 0 (Master) or another Insert/Aux (1..12)
       ▼
  [Master Bus (Track 0, TrackType = MASTER / special post-mix pass)]
       │  Master FX slots 0..9 + Master 3-Band EQ + Master StereoSep + Master Vol/Pitch
       ▼
  [d_process_master_output] ──> miniaudio hardware output + Telemetry ring
```

Why this works cleanly:
- **Zero slot collisions**: Generator tracks (`100 + chIdx`) hold the instrument plugin exclusively at slot `0` and route audio into their assigned Mixer Insert Group bus (`1..12`), while Mixer Inserts (`1..12`) and Master (`0`) hold pure FX chains at slots `0..9`.
- **Reuses existing Pass 1 -> Pass 2 engine scheduling**: `playback_thread.cpp` already processes `NORMAL` tracks (generators/audio/MIDI) in Pass 1 and sums their outputs into `GROUP`/`AUX` tracks (`1..12`) processed in Pass 2! We add Pass 3 for Master Track `0` before `d_process_master_output`.
- **Single-track Solo/Mute**: Channel Solo/Mute gates its generator track; Mixer Insert Solo/Mute gates the insert bus cleanly without corrupting channel mute state.

### 2.2 Playback Modes: `PAT` vs `SONG`

- **`SONG` mode**: Plays timeline clips (`TimelineClip`) and automation lanes across tracks `0..12` and generator tracks, just as `playback_thread.cpp` currently does, plus any `PLAYLIST_CLIP_PATTERN` references or synced compiled pattern clips.
- **`PAT` mode**: Loops the active Pattern (steps `0..stepCount-1`, 16th-note resolution with FL-style **Swing** applied to odd 16th steps and per-step Graph Editor velocity/pitch/pan/shift) across generator tracks (`100 + chIdx`), plus any Piano Roll notes on that pattern/channel.
- Both modes run sample-accurately in the real-time audio callback (`512` frames/block), plus **Metronome click** on quarter beats and optional **Count-in** before recording.

### 2.3 Protocol & DSP Extensions (`pb/*.proto`)

- `pb/core.proto`:
  - `Track`: add `eq_low`, `eq_mid`, `eq_high`, `stereo_sep`, `plugin_mix` (`map<int32, float>`), and `TrackType::TRACK_TYPE_MASTER` (or explicit `track_index == 0` master role).
  - `Project`: add `playback_mode`, `metronome_enabled`, `swing`, `master_pitch`, plus Channel Rack / Pattern state (`patterns`, `channels`, `active_pattern_index`) so `.hbk` project saves/loads preserve the complete FL/Hibiki session!
- `pb/commands.proto`:
  - `TransportCmd`: add `ACTION_SET_PLAYBACK_MODE` (`PAT`/`SONG`), `ACTION_SET_METRONOME`, `ACTION_SET_SWING`, `ACTION_SET_MASTER_PITCH`, `ACTION_PANIC`.
  - `TrackCmd`: add `ACTION_SET_EQ` (low/mid/high), `ACTION_SET_STEREO_SEP`.
  - `PluginCmd`: add `ACTION_SET_MIX` (wet/dry `0..1`).
  - `PatternCmd` / `ChannelCmd` (or dedicated `SequencerCmd`): create/select/clone pattern, set active pattern length, toggle/edit step + graph values (velocity/pitch/pan/shift), add/update channel (uri, mixer_insert, mute, solo, vol, pan).
- `pb/notifications.proto`:
  - `TrackLevels`: extend with `stereo_sep`, `eq_low/mid/high`, `volume`, `pan`, `muted`, `soloed` OR include in `TrackInfo` + `ProjectSync` (`PlayheadInfo` extended with `cpu_percent` and downsampled `master_waveform` oscilloscope samples!).
  - `PluginDescription`: ensure `path` is populated in `sendPluginList`.

---

## 3. Phased Implementation & Rollback Plan

Each phase ends with `./tools/format.sh`, full Bazel `-c opt` tests, and a dedicated clean git commit on branch `d`.

### Phase 0: Architecture Documentation (`docs/d_v2.md`)
- Document design, track routing topology, protocol extensions, and phased plan.
- Commit Phase 0.

### Phase 1: Engine Discovery, Master Bus, Sync Completeness, Metronome & Live Telemetry
- **UI**:
  - Fix `resolveEnginePath` (`gui_d/main.d`) to find `hbk-play` next to the binary, in `../engine/hbk-play`, and Bazel runfiles, plus surface stderr/connection errors gracefully.
  - Replace mock `sinf()` oscilloscope & `cpuPercent = 3` in `Toolbar` with real telemetry streamed from `hbk-play`.
  - Wire `SendVirtualMidi` on `Z..M` and Piano Roll keybed with clean timed note-off without blocking/leaking, plus `ACTION_PANIC` on Stop.
- **Engine**:
  - Fix `sendPluginList` in `engine/ipc/ipc.cpp` to populate `PluginDescription.path`.
  - Add Track `0` Master Bus pass (`Master` FX + Volume + Pitch) in `playback_thread.cpp` and `d_process_master_output` telemetry (`cpu_percent`, downsampled master waveform in `PlayheadInfo`).
  - Add backend Metronome synthesis (accented downbeat + beat click) toggled via `TransportCmd`.
  - Complete `SyncProjectToGui` so BPM, playhead, track levels/routing/mute/solo, plugin bypass & param values, and clip MIDI events sync immediately on load/connect.
- **Tests**: D (`gui_d_test`, `commands_d_test`) + C++ (`commands_test`, `playback_test`).
- Commit Phase 1.

### Phase 2: Channel Rack, Step Sequencer, Patterns, & Generator-to-Mixer Routing
- **Engine & Proto**:
  - Implement generator track topology (`100 + channelIdx` -> `output_track_index = mixerInsert`), Channel Rack + Pattern sequencer (`PatternState`, `ChannelState`, 16/32/64 steps, Swing, Graph Editor velocity/pitch/pan/shift), and `PAT` vs `SONG` real-time rendering in `playback_thread.cpp` + `.hbk` save/load.
  - Add `SequencerCmd` (sync/add channel, set step, set pattern, set active pattern, swing, channel volume/pan/mute/solo/route).
- **UI**:
  - Sync `initDefaultTemplate()` to `hbk-play` as soon as the IPC connection opens (and on `File -> New`), so pressing Play / clicking pads / typing `Z..M` makes live audio immediately.
  - Wire all `ChannelRackWidget` actions (`onStepToggled`, `onGraphValueChanged`, `onSwingChanged`, `onChannelChanged`, `onAddChannel`, `onOpenPianoRoll`, `onOpenPlugin`) and `Toolbar` `PAT`/`SONG` + Pattern `< > +` to `hbk-play`.
- **Tests**: Unit tests for step timing, swing math, PAT vs SONG event emission, generator->insert routing, and D model/IPC round-trips.
- Commit Phase 2.

### Phase 3: Mixer DSP (3-Band EQ, Stereo Sep, Wet/Dry FX Mix) & Playlist/Piano Roll Sync
- **Engine & D DSP**:
  - Add `@nogc` 3-band Biquad EQ (leveraging `engine/core/biquad_filter.d` / D kernel), Stereo Separation M/S processor, and per-FX-slot wet/dry Mix blending in `playback_mixer.d` / `playback_thread.cpp`.
  - Add `ACTION_SET_EQ`, `ACTION_SET_STEREO_SEP`, `ACTION_SET_MIX`, and consistent FL-curve gain mapping (`0..1` UI <-> linear amplitude).
- **UI**:
  - Wire Mixer 3-Band EQ sliders, Stereo Separation knob, FX slot Mix knobs, Routing selector, and Track `0` Master FX chain.
  - Complete Playlist (`TimelineCanvasWidget`) callbacks (`MOVE_CLIP`, `RESIZE_CLIP`, `SET_CLIP_MUTED`, `SLICE`, `COPY`, `TRACK_MUTE`/`TRACK_SOLO`, `AutomationCmd`, double-click clip -> `MIDI_ACTION_GET`) and Piano Roll `onNotesChanged` / active clip synchronization.
- **Tests**: DSP unit tests (EQ boost/cut frequency response, stereo width mono/wide bounds, wet/dry mix), IPC command/notification tests, and `gui_d` widget regression tests.
- Commit Phase 3.

### Phase 4: Plugin Wrapper, Real Metering/Spectrum/Scope, Remote VST3 Editor, & Sampler/Drum Machine
- **UI & Engine**:
  - Populate `PluginWrapperWidget` parameters directly from `ParamList`, sync live `ParamValueChange`, wire **Preset `< >`**, **Bypass**, and `EDITOR` toggle (`ACTION_SHOW_GUI` / `ACTION_GET_EDITOR_FRAME` -> `EditorFrameData` rendered into `ColorDrawBuf`, mouse/key events via `ACTION_SEND_EDITOR_INPUT`, `ACTION_STOP_GUI`).
  - Render real telemetry inside `PluginWrapperWidget` when telemetry notifications arrive (`PluginSpectrumData` for EQ, `PluginMeteringData` for Comp/Limiter/HOTT/Maxim/Reverb/Delay, `PluginScopeData` for Phaser).
  - Support `BuiltinSampler` sample load (`ACTION_LOAD_SAMPLE` -> `PluginSampleData`) and `BuiltinDrumMachine` pad preview (`DrumPadCmd`).
- **Tests**: Plugin param sync, preset switching, telemetry apply/render, sampler & drum pad IPC tests.
- Commit Phase 4.

### Phase 5: Real Browser Scanning/Audition, Project Dialogs, Settings, Undo/Redo, & Bounce
- **UI**:
  - Replace hardcoded non-existent WAV paths in `browser_panel.d` with real filesystem directory scanning (including `testdata/` + configurable sample folder), WAV preview loading, and live preview audition in engine.
  - Add interactive modal dialogs in `gui_d` for **Open / Save As / Bounce WAV / Collect Files**, **Browse VST3 / Scan Paths**, and **Audio/Plugin Settings** (`SetPluginHostMode`, `SetAudioBufferSize`, `SetProcessingPrecision`, `ListAudioInputs`, `ListMidiInputs`).
  - Wire **Edit -> Undo / Redo** with full project re-sync, plus **Toolbar Record / Overdub / Countdown** recording flow.
- **Tests**: Browser scan/filter tests, Undo/Redo round-trip, project save->clear->load round-trip including patterns/channels/EQ/routing, WAV bounce verification.
- Commit Phase 5.

### Phase 6: Full Verification & Final Status Update
- Run `./tools/format.sh`, `bazel test //... -c opt`, and `bazel build //gui_d:hibiki_gui //engine:hbk-play //engine:hbk-plugin-worker //engine:hbk-worker-daemon -c opt`.
- Update `docs/d_v2.md` with verification results and commit.
