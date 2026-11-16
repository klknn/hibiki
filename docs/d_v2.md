# Hibiki `gui_d` Live End-to-End Integration Architecture (`d_v2`)

> **Goal**: Turn the `dlangui`-based Y2K/FL-inspired `gui_d` frontend into a fully functional, real-time, end-to-end live DAW connected to the `hbk-play` C++/D audio engine over Protobuf/HBKI IPC.

---

## 1. Initial Audit & Architectural Gaps Solved

The original `gui_d` prototype rendered FL-style windows (Toolbar, Browser, Channel Rack + Graph Editor, Playlist, Piano Roll, Mixer + FX Rack + 3-Band EQ, Plugin Wrapper) backed only by local state in `TransportModel`, while `hbk-play` (`engine/main.d` + `engine/playback_thread.cpp` + `engine/commands/` + `engine/core/`) hosted the real-time 512-sample audio mixer, built-in instruments/effects, VST3/CLAP hosting, MIDI/Timeline/Session playback, automation, recording, and WAV bouncing.

Key disconnects resolved across Phases 1–11:

1. **Executable Discovery, Lifecycle & IPC Safety (`gui_d/main.d`, `gui_d/ipc_client.d`)**
   - `resolveEnginePath` locates `hbk-play` next to the binary, in sibling `../engine/hbk-play`, and inside Bazel runfiles (`RUNFILES_DIR` / `TEST_SRCDIR`).
   - `decodeProtoOwned` allocates per-message GC `Arena` buffers so queued notifications never alias a shared static scratch buffer.
   - `extractNextFrame` scans ahead with a **1-byte stride** on corrupt/oversized headers (`len > MAX_IPC_FRAME_BYTES`) so valid `HBKI` headers at unaligned offsets are never skipped.
   - `EngineProcessClient` captures `hbk-play` `stderr` on a background thread (`stderrLoop`) and surfaces process/pipe diagnostics via `onError` directly to the UI hint bar.
2. **Startup Sync & Track Topology**
   - `gui_d` initializes `model.initDefaultTemplate()`, syncs it cleanly to `hbk-play` via `syncTemplateToEngine()`, and resets history via `ProjectCmd::ACTION_CLEAR_HISTORY = 9` so Undo starts at a clean post-template baseline.
   - Generator tracks live at `100 + channelIdx` (instrument exclusively in slot `0`), routing via `output_track_index` into Mixer Insert `GROUP` buses `1..12` (pure FX chains slots `0..9`), which route through topological DAG ordering into Master Track `0` (`TrackType::TRACK_MASTER`).
3. **Real-Time Hardening (P0)**
   - Audio thread pre-allocates scratch workspaces (`PlaybackThread::Workspace` and `RenderProjectWorkspace`) with cached 3-band Biquad EQ coefficients (`Track::EnsureEqCoeffs`, `Track::IsEqBypassed`), lock-free `std::atomic` `ProjectState` transport scalars, and split `tracks_mutex` / `plugins_mutex` synchronization + snapshot locking in `RenderTimeline` / `BounceProject`.
   - `AudioEditor` lifetime UAF fixed; Browser distinguishes single-click audition from double-click/drag assignment.
4. **Unified Live & Offline DSP (`RenderProjectAudioBlock`) & Topological Routing DAG (P1)**
   - Both `playback_thread.cpp` live playback and offline `BounceProject` call `RenderProjectAudioBlock(...)`:
     - **Pass 1**: Render `NORMAL` / generator tracks (`100+`).
     - **Pass 2**: Topological Kahn DAG over `GROUP`/`AUX` buses (`output_track_index`, `group_parent_index`, `aux_sends`) with cycle-rejection (`WouldCreateRoutingCycle` BFS in `track_commands.cpp`) and deterministic index-order fallback if legacy projects contain cycles.
     - **Pass 3**: Master Track `0` FX chain + EQ + Stereo Separation + `master_volume` + `master_pitch` + Metronome click.
   - `BounceTrackClip` preserves unity gain with `trim_start_beats`, `plugin_mix`, and 3-band EQ + stereo separation (`d_apply_track_eq_and_stereo_sep_f32`).
5. **Step Sequencer, 64 Steps, Step Pan DSP, & Clip Parity (P1/P2)**
   - Supports `16 / 32 / 64` pattern lengths (`SequencerCmd::ACTION_SET_PATTERN_LENGTH = 14`), per-step Graph Editor `step_pans` wired into `MidiNoteEvent::pan` across `BuiltinSampler`, `Builtin3xOsc`, and `BuiltinDrumMachine`, and full `HistoryManager` Undo/Redo across sequencer mutations.
   - `TimelineClipInfo.is_midi = 15` classifies pure MIDI vs Pattern vs Audio clips for mini-note rendering and double-click routing (`AudioEditorLoad` + `PreviewPlay` for Audio vs Piano Roll for MIDI/Pattern).
   - Remote VST3 editor `ColorDrawBuf` conversion skips redundant pixel copies via `PluginSlotState.editorRevision` caching.

---

## 2. Target End-to-End Architecture

### 2.1 Track Topology & Signal Routing DAG

```
  [Channel Generator Tracks (100 + channelIdx)]
       │  e.g. Ch 0 Kick    (track 100) ──> builtin://dr8_kick  ──┐
       │       Ch 1 Snare   (track 101) ──> builtin://dr8_snare ──┼── output_track_index = ch.mixerInsert (1..12 or 0)
       │       Ch 2 Hi-Hat  (track 102) ──> builtin://dr8_hat   ──┤
       │       Ch 3 Acid    (track 103) ──> builtin://acid_bass ──┤
       │       Ch 4 E-Piano (track 104) ──> builtin://epiano    ──┘
       ▼
  [Mixer Insert / Group / Aux Buses (1..12, TrackType = GROUP/AUX)]
       │  Topological DAG pass (Pass 2) + WouldCreateRoutingCycle BFS guard
       │  Per-Insert FX chain slots 0..9 (plugin_mix wet/dry + bypass)
       │  Cached 3-Band EQ (Low/Mid/High) + Stereo Separation + Pan + Fader + Mute/Solo/Record Arm
       │  Route ──> output_track_index = 0 (Master) or any upstream/downstream Group/Aux Insert
       ▼
  [Master Bus (Track 0, TrackType = TRACK_MASTER, Pass 3)]
       │  Master FX slots 0..9 + Master 3-Band EQ + Master StereoSep + Master Vol/Pitch + Metronome
       ▼
  [d_process_master_output / BounceProject WAV]
```

### 2.2 Playback Modes: `PAT` vs `SONG`

- **`SONG` mode (`PLAYBACK_MODE_SONG`)**: Plays timeline clips (`TimelineClip`), pattern clip references (`pattern_index`), MIDI clips (`is_midi`), and automation lanes (`pluginIndex`, `paramId`) sample-accurately inside `RenderProjectAudioBlock`.
- **`PAT` mode (`PLAYBACK_MODE_PAT`)**: Loops active Pattern (`length_steps` in `{16, 32, 64}` via `activePatternLengthSteps()` / `currentStepIndex()`), applying FL-style Swing on odd 16th steps and per-step Graph Editor `velocity` / `pitch` / `pan` / `shift`, plus pattern Piano Roll notes.
- **Recording Pre-Roll & Overdub**: Toolbar Record honors `countdownEnabled` (1-bar metronome pre-roll via UI animation timer before dispatching `ACTION_RECORD`) and `overdubEnabled` (preserve vs clear existing pattern take), plus Mixer `[R]` strip Record Arm buttons.

### 2.3 Protocol & DSP Extensions (`pb/*.proto`)

- `pb/core.proto`:
  - `Track`: `eq_low` (`23`), `eq_mid` (`24`), `eq_high` (`25`), `stereo_sep` (`26`), `repeated float plugin_mix = 27`, `TrackType::TRACK_MASTER = 3`.
  - `Project`: `playback_mode` (`5`), `swing_amount` (`6`), `selected_pattern_index` (`7`), `channels` (`8`), `patterns` (`9`), `metronome_enabled` (`10`), `optional float master_volume = 11`, `master_pitch` (`12`), `loop_enabled` (`13`), `loop_start_sec` (`14`), `loop_end_sec` (`15`).
  - `ChannelPatternData`: expanded up to 64 steps (`steps`, `step_velocities`, `step_pitches`, `step_pans`, `step_shifts`, `notes`).
- `pb/commands.proto`:
  - `ProjectCmd`: `ACTION_CLEAR_HISTORY = 9`.
  - `SequencerCmd`: `ACTION_SET_PLAYBACK_MODE = 1` .. `ACTION_SET_CHANNEL_NOTES = 13`, `ACTION_SET_PATTERN_LENGTH = 14`.
  - `TrackCmd`: `ACTION_ARM_RECORD = 9`, `ACTION_SET_RECORD_MODE = 12`, `ACTION_SET_OUTPUT_ROUTING = 23`, `ACTION_SET_AUX_SEND = 24`, `ACTION_SET_GROUP_PARENT = 25`, `ACTION_SET_EQ = 26`, `ACTION_SET_STEREO_SEP = 27`.
  - `PluginCmd`: `ACTION_SET_BYPASS = 11`, `ACTION_REORDER_PLUGIN = 12`, `ACTION_SET_MIX = 13`.
- `pb/notifications.proto`:
  - `TimelineClipInfo`: `optional int32 pattern_index = 14`, `bool is_midi = 15`.
  - `PlayheadInfo`: `cpu_percent`, `oscilloscope`, `playback_mode`, `transport_state`.
  - `PluginDescription`: includes bundle `path` (`4`) and `is_instrument` (`5`).

---

## 3. Phased Implementation Summary (Branch `d`)

- **Phases 0–5**: Initial architecture, `resolveEnginePath`, Master Bus & live telemetry, Channel Rack/Sequencer/`PAT` vs `SONG`, 3-Band EQ / Stereo Sep / Wet-Dry FX Mix, Plugin Wrapper remote VST3 GUI & metering/spectrum/scope, real Browser scanning/audition, Save/Load/Bounce/Collect/Settings modals.
- **Phase 6 (`ff3d078`)**: Verified full-repo build/tests and updated status docs.
- **Phase 7 (`d8a9fcb`)**: Per-message GC `Arena` in `decodeProtoOwned`, macOS shutdown clean exit, clip/slot/pattern sync fixes, initial live `//gui_d:gui_e2e_test`.
- **Phase 8 (`3374d31`, P0)**: Zero-alloc `PlaybackThread::Workspace`, cached EQ coefficients, atomic `ProjectState` scalars, split `tracks_mutex` / `plugins_mutex` concurrency, `AudioEditor` UAF fix, and Browser click/drag disambiguation.
- **Phase 9 (`6259950`, P1)**: Unified `RenderProjectAudioBlock`, Pass 2 topological routing DAG + `WouldCreateRoutingCycle` BFS guard, unity-gain `BounceTrackClip`, `Project` scalars `10..15` persistence, 64 steps (`ACTION_SET_PATTERN_LENGTH`), per-step voice `pan` DSP (`BuiltinSampler`, `Builtin3xOsc`, `BuiltinDrumMachine`), Sequencer Undo/Redo, `ACTION_CLEAR_HISTORY`, dynamic Automation targets, `TimelineClipInfo.is_midi`, and Audio vs MIDI/Pattern clip double-click parity.
- **Phase 10 (`b8ac57c`, P2 & Extended E2E)**: `stderr` thread & `onError` surfacing, 1-byte `extractNextFrame` resync, `editorRevision` framebuffer cache in `PluginWrapperWidget`, Mixer Record Arm `[R]`, 1-bar countdown pre-roll & overdub, timer-driven `PendingNoteOff[]` + `FocusOut` note cleanup, Timeline `SLIP`/`SELECT` + `DEL`, Piano Roll `snapBeats()` & `SELECT` + `DEL`, and expanded live `//gui_d:gui_e2e_test` (non-silent bounced `mix.wav` verification, `PAT`/`SONG`, 32-step Undo/Redo, `ClearHistory`, and `onError`).
- **Phase 11**: Updated `docs/d_v2.md`, added Japanese translation `docs/d_v2_ja.md`, ran final `./tools/format.sh`, full `bazel test //... -c opt`, and binary builds.

---

## 4. Verification Status (`d` branch)

- **`./tools/format.sh`**: Reformatted/verified all C++ (`206`), Java (`114`), `BUILD`, `.proto` (`4`), and D (`75`) source files.
- **`bazel test //... -c opt --test_output=errors`**: **105 of 107 test targets passed** with 0 failures (`//android/app:hibiki_engine_playback_test` and `//android/app:melody_sequencer_integration_test` skipped).
  - Includes `//engine/core:project_test`, `//engine/commands:commands_test`, `//engine:playback_d_test`, `//gui_d:gui_d_test`, and live subprocess `//gui_d:gui_e2e_test` (verifying `hbk-play` IPC handshake, BPM & template sync, non-silent bounced WAV samples, `PAT`/`SONG`, 32-step toggle + Undo/Redo + `ACTION_CLEAR_HISTORY`, `.hbk` save/load round-trip, and `onError`).
- **`bazel build //gui_d:hibiki_gui //engine:hbk-play //engine:hbk-plugin-worker //engine:hbk-worker-daemon -c opt`**: All 4 release binaries built cleanly with exit code `0`.
- **Known Scope Limit**: Automated Bazel runs exercise headless unit, DSP, widget (`ColorDrawBuf` offscreen canvas), and live `hbk-play` IPC E2E suites; interactive SDL2/Cocoa desktop window rendering with live CoreAudio hardware I/O and third-party `.vst3` desktop HWNDs requires manual desktop launch (`bazel run //gui_d:hibiki_gui`).
