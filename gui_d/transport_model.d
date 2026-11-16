module gui_d.transport_model;

import core.stdc.math : expf, sinf;
import gui_d.theme : FlPalette, FlThemeId, getFlPalette;
import pb.core : MidiEvent, TransportState;
import pb.notifications : Notification, PluginDescription;
import std.conv : to;
import std.format : format;

/// FL Studio Pattern vs Song playback mode.
enum PlaybackMode {
    PAT,
    SONG,
}

/// Global snap resolution for Playlist and Piano Roll.
enum SnapMode {
    LINE,
    CELL,
    STEP_1_4,
    STEP_1_2,
    STEP,
    BEAT_1_4,
    BEAT_1_2,
    BEAT,
    BAR,
    NONE,
}

/// Playlist and Piano Roll mouse editing tools.
enum EditorTool {
    DRAW,
    PAINT,
    DELETE,
    MUTE,
    SLIP,
    SLICE,
    SELECT,
    ZOOM,
}

/// Playlist clip type matching FL Studio's Pattern / Audio / Automation clips.
enum PlaylistClipKind {
    PATTERN,
    AUDIO,
    AUTOMATION,
}

/// Browser top-level category tabs matching FL Studio 2026 Browser.
enum BrowserCategory {
    ALL,
    PROJECT,
    PLUGINS,
    SAMPLES,
    RECENT,
}

/// Channel Rack Graph Editor parameter lane.
enum GraphEditorMode {
    VELOCITY,
    PITCH,
    PAN,
    SHIFT,
}

/// Time LCD display mode (Time vs Bar:Beat:Step).
enum TimeDisplayMode {
    TIME_MS,
    BAR_BEAT,
}

/// Channel Rack filter category.
enum ChannelFilterMode {
    ALL,
    DRUMS,
    SYNTHS,
    AUDIO,
}

/// Control point inside a Playlist Automation Clip.
struct AutomationPoint {
    double normTime = 0.0; // [0..1] across clip duration
    float value = 0.5f;    // [0..1]
}

/// Playlist time marker (e.g., Intro, Drop, Break).
struct TimeMarkerState {
    double timeSec = 0.0;
    string label = "Marker";
    uint color = 0x00FF8F2D;
}

/// Timeline / Playlist clip state for a track.
struct TimelineClipState {
    int clipIndex = 0;
    int patternIndex = 0;
    PlaylistClipKind kind = PlaylistClipKind.PATTERN;
    string name = "Pattern 1";
    uint color = 0x00385770;
    double startTimeSec = 0.0;
    double durationSec = 4.0;
    bool muted = false;
    float[] waveform;
    AutomationPoint[] autoPoints;
}

/// Plugin parameter metadata & current normalized value.
struct ParamItemState {
    uint id = 0;
    string name;
    float defaultValue = 0.5f;
    float currentValue = 0.5f;
}

/// Loaded plugin slot on a Channel or Mixer Insert FX Rack.
struct PluginSlotState {
    int pluginIndex = 0;
    string name = "Plugin";
    string uri;
    bool enabled = true;
    float mix = 1.0f;
    bool isInstrument = false;
    int presetIndex = 0;
    string[] presets;
    ParamItemState[] params;
}

/// Channel Rack generator/sampler row with 16-step sequencer, Graph Editor values, and mixer routing.
struct ChannelState {
    int index = 0;
    string name = "Channel";
    string uri = "builtin://3xosc";
    string category = "Synths";
    uint color = 0x004A6A8A;
    float volume = 0.78f;
    float pan = 0.0f; // [-1..1]
    bool muted = false;
    bool soloed = false;
    int mixerInsert = 1; // 0 = Master, 1..N = Insert
    bool[16] steps;
    float[16] stepVelocities;
    float[16] stepPitches; // [-1..1] (0 = C5)
    float[16] stepPans;    // [-1..1]
    float[16] stepShifts;  // [0..1]
    MidiEvent[] notes;
    PluginSlotState generatorPlugin;

    void initDefaults() @safe {
        foreach (i; 0 .. 16) {
            stepVelocities[i] = 0.78f;
            stepPitches[i] = 0.0f;
            stepPans[i] = 0.0f;
            stepShifts[i] = 0.0f;
        }
    }
}

/// FL Studio Pattern metadata.
struct PatternState {
    int index = 0;
    string name = "Pattern 1";
    uint color = 0x00385770;
    int lengthSteps = 16;
}

/// Mixer Insert (Master + Inserts 1..12) and Playlist Track Lane state.
struct TrackStripState {
    int index = 0;
    string name = "Track";
    uint color = 0x00385770;
    float volume = 0.8f;
    float pan = 0.0f;       // [-1..1]
    float stereoSep = 0.0f; // [-1..1] (Merged <-> Separated)
    bool muted = false;
    bool soloed = false;
    bool locked = false;
    float peakL = 0.0f;
    float peakR = 0.0f;
    float eqLow = 0.5f;
    float eqMid = 0.5f;
    float eqHigh = 0.5f;
    int routedTo = 0; // 0 = Master
    float sendLevel = 0.85f;
    TimelineClipState[] clips;
    PluginSlotState[] plugins; // Up to 10 Mixer FX slots in FL Studio
}

/// Active MIDI clip state shown in the Piano Roll.
struct ActiveMidiClip {
    int trackIndex = 0;
    int clipIndex = 0;
    int channelIndex = 0;
    int resolution = 480;
    MidiEvent[] events;
}

/// Browser sample audition metadata for the bottom waveform preview panel.
struct SamplePreviewState {
    string name = "loop140.wav";
    string path = "testdata/loop140.wav";
    string info = "140 BPM | 44.1 kHz | 16-bit";
    float[] waveform;
}

/// Synthesizes a high-resolution deterministic audio waveform (`count` signed samples in `[-1, 1]`)
/// with punchy transients, harmonic body, and high-frequency texture for crisp clip/preview rendering.
float[] generateSampleWaveform(size_t count = 768, uint seed = 1, bool isLoop = true) nothrow @safe {
    if (count == 0) return null;
    float[] wf = new float[count];
    uint state = seed == 0 ? 0x9E3779B9u : (seed * 747796405u + 2891336453u);
    const double phaseSeed = cast(double)(seed & 0x3F) * 0.17;
    foreach (i; 0 .. count) {
        const double t = cast(double)i / cast(double)(count > 1 ? count - 1 : 1);
        state = state * 1664525u + 1013904223u;
        const float noise = (cast(float)(state & 0xFFFF) / 32768.0f) - 1.0f;
        float env;
        float carrier;
        if (isLoop) {
            const double beat = (t * 8.0) % 1.0;
            const int beatIdx = cast(int)(t * 8.0);
            const double sixteenth = (t * 32.0) % 1.0;
            const float kickEnv = ((beatIdx % 2) == 0 || beatIdx == 3)
                ? expf(-cast(float)(beat * 7.5)) * 0.92f : 0.0f;
            const float snareEnv = ((beatIdx % 2) == 1)
                ? expf(-cast(float)(beat * 9.0)) * 0.85f : 0.0f;
            const float hatEnv = expf(-cast(float)(sixteenth * 9.5)) * 0.34f;
            const float bassEnv = 0.22f + 0.18f * sinf(cast(float)(t * 12.566 + phaseSeed));
            env = kickEnv + snareEnv * 0.85f + hatEnv + bassEnv;
            if (env > 0.98f) env = 0.98f;
            const double f0 = 38.0 + phaseSeed * 4.0;
            carrier = 0.52f * sinf(cast(float)(t * 6.28318 * f0)) +
                      0.26f * sinf(cast(float)(t * 6.28318 * (f0 * 2.37) + 0.7)) +
                      (0.22f + snareEnv * 0.35f) * noise;
        } else {
            const float attack = (t < 0.015) ? cast(float)(t / 0.015) : 1.0f;
            env = attack * (0.86f * expf(-cast(float)(t * 4.2)) + 0.16f * expf(-cast(float)(t * 1.4)));
            const double sweep = 26.0 + 34.0 * expf(-cast(float)(t * 8.0));
            carrier = 0.62f * sinf(cast(float)(t * 6.28318 * sweep + phaseSeed)) +
                      0.24f * sinf(cast(float)(t * 6.28318 * (sweep * 3.1))) +
                      0.14f * noise;
        }
        float v = env * carrier;
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        wf[i] = v;
    }
    return wf;
}

/// UI-side FL Studio 2026 DAW state model synchronized with `hbk-play` notifications.
struct TransportModel {
    FlThemeId themeId = FlThemeId.DEFAULT_DARK;

    double playheadPosSec = 0.0;
    double bpm = 140.0;
    bool isPlaying = false;
    bool isRecording = false;
    bool loopEnabled = true;
    double loopStartSec = 0.0;
    double loopEndSec = 6.857; // 4 bars at 140 BPM
    PlaybackMode playbackMode = PlaybackMode.PAT;
    SnapMode snapMode = SnapMode.BEAT_1_4;
    TimeDisplayMode timeDisplayMode = TimeDisplayMode.BAR_BEAT;

    float masterVolume = 0.82f;
    float masterPitch = 0.0f;
    float swingAmount = 0.0f;
    bool metronomeEnabled = false;
    bool waitForInput = false;
    bool countdownEnabled = false;
    bool overdubEnabled = true;
    bool loopRecordEnabled = true;
    bool typingKeyboardEnabled = true;

    int cpuPercent = 0;
    float[] oscilloscopeSamples;
    int voiceCount = 4;
    string hintText = "Ready — Drag window title bars to move (snap on; hold Alt to bypass)";
    string statusMessage = "FL Studio 2026 Prototype Ready";

    // Active Editor States
    EditorTool playlistTool = EditorTool.PAINT;
    EditorTool pianoRollTool = EditorTool.DRAW;
    PlaylistClipKind activePickerKind = PlaylistClipKind.PATTERN;
    int activePickerIndex = 0;
    bool showClipPicker = true;
    bool showMixerFxRack = true;
    bool showVelocityLane = true;
    bool ghostNotesEnabled = true;
    string scaleHighlightName = "A Minor";
    bool graphEditorOpen = false;
    GraphEditorMode graphEditorMode = GraphEditorMode.VELOCITY;
    ChannelFilterMode channelFilter = ChannelFilterMode.ALL;

    int selectedTrack = 0;
    int selectedClipIndex = 0;
    int selectedPluginIndex = 0;
    int selectedPatternIndex = 0;
    int selectedChannelIndex = 0;
    int selectedMixerInsert = 1;
    int selectedFxSlot = 0;

    ChannelState[] channels;
    PatternState[] patterns;
    TrackStripState[] tracks; // 0 = Master, 1..12 = Mixer Inserts / Playlist Lanes
    TimeMarkerState[] timeMarkers;
    ActiveMidiClip activeMidi;
    PluginDescription[] scannedPlugins;
    SamplePreviewState samplePreview;

    /// Returns the active FL Studio 2026 color palette.
    FlPalette palette() const pure nothrow @safe {
        return getFlPalette(themeId);
    }

    /// Creates a rich built-in plugin slot template for generators or effects.
    static PluginSlotState makeDefaultPluginSlot(string name, string uri, bool isInst, int idx = 0) @safe {
        PluginSlotState slot;
        slot.pluginIndex = idx;
        slot.name = name;
        slot.uri = uri;
        slot.enabled = true;
        slot.mix = 1.0f;
        slot.isInstrument = isInst;
        slot.presets = ["Default", "Warm Analog", "Punchy Club", "Wide Stereo", "Init"];
        if (isInst) {
            slot.params = [
                ParamItemState(0, "Cutoff", 0.65f, 0.65f),
                ParamItemState(1, "Resonance", 0.30f, 0.30f),
                ParamItemState(2, "Env Mod", 0.48f, 0.48f),
                ParamItemState(3, "Drive", 0.22f, 0.22f),
                ParamItemState(4, "Attack", 0.05f, 0.05f),
                ParamItemState(5, "Decay", 0.55f, 0.55f),
                ParamItemState(6, "Sustain", 0.70f, 0.70f),
                ParamItemState(7, "Release", 0.35f, 0.35f),
            ];
        } else {
            slot.params = [
                ParamItemState(0, "Amount / Drive", 0.52f, 0.52f),
                ParamItemState(1, "Tone / Freq", 0.60f, 0.60f),
                ParamItemState(2, "Feedback / Time", 0.38f, 0.38f),
                ParamItemState(3, "Stereo Width", 0.66f, 0.66f),
                ParamItemState(4, "Output Gain", 0.75f, 0.75f),
            ];
        }
        return slot;
    }

    /// Populates a full FL Studio 2026 starter template with Channels, Patterns, Playlist clips, Piano Roll, and Mixer FX.
    void initDefaultTemplate() @safe {
        const pal = palette();
        if (patterns.length == 0) {
            patterns = [
                PatternState(0, "Pattern 1 (Drums)", pal.clipPattern, 16),
                PatternState(1, "Pattern 2 (Acid Bass)", pal.accentOrange & 0x00FFFFFF, 16),
                PatternState(2, "Pattern 3 (E-Piano)", pal.clipAudio, 16),
                PatternState(3, "Pattern 4 (Fill)", pal.clipAutomation, 16),
            ];
            patterns[1].color = 0x007A5032;
        }
        if (channels.length == 0) {
            ChannelState kick;
            kick.initDefaults();
            kick.index = 0;
            kick.name = "DR-8 Kick";
            kick.uri = "builtin://dr8_kick";
            kick.category = "Drums";
            kick.color = 0x00466480;
            kick.mixerInsert = 1;
            kick.steps = [true, false, false, false, true, false, false, false,
                          true, false, false, false, true, false, false, false];
            kick.stepVelocities = [0.95f, 0.78f, 0.78f, 0.78f, 0.90f, 0.78f, 0.78f, 0.78f,
                                   0.95f, 0.78f, 0.78f, 0.78f, 0.88f, 0.78f, 0.78f, 0.78f];
            kick.generatorPlugin = makeDefaultPluginSlot("DR-8 Kick", "builtin://dr8_kick", true, 0);

            ChannelState snare;
            snare.initDefaults();
            snare.index = 1;
            snare.name = "DR-8 Snare";
            snare.uri = "builtin://dr8_snare";
            snare.category = "Drums";
            snare.color = 0x00664B78;
            snare.mixerInsert = 2;
            snare.steps = [false, false, false, false, true, false, false, false,
                           false, false, false, false, true, false, false, true];
            snare.stepVelocities[4] = 0.92f;
            snare.stepVelocities[12] = 0.95f;
            snare.stepVelocities[15] = 0.55f;
            snare.generatorPlugin = makeDefaultPluginSlot("DR-8 Snare", "builtin://dr8_snare", true, 0);

            ChannelState hat;
            hat.initDefaults();
            hat.index = 2;
            hat.name = "DR-8 Hat";
            hat.uri = "builtin://dr8_hat";
            hat.category = "Drums";
            hat.color = 0x0033665E;
            hat.mixerInsert = 3;
            hat.pan = 0.18f;
            hat.steps = [true, false, true, false, true, false, true, false,
                         true, false, true, false, true, false, true, true];
            hat.generatorPlugin = makeDefaultPluginSlot("DR-8 Hat", "builtin://dr8_hat", true, 0);

            ChannelState bass;
            bass.initDefaults();
            bass.index = 3;
            bass.name = "Acid Bass";
            bass.uri = "builtin://acid_bass";
            bass.category = "Synths";
            bass.color = 0x00785535;
            bass.mixerInsert = 4;
            bass.steps = [true, false, true, false, false, true, false, true,
                          true, false, false, true, false, true, true, false];
            bass.generatorPlugin = makeDefaultPluginSlot("Acid Bass", "builtin://acid_bass", true, 0);

            ChannelState epiano;
            epiano.initDefaults();
            epiano.index = 4;
            epiano.name = "E-Piano";
            epiano.uri = "builtin://epiano";
            epiano.category = "Synths";
            epiano.color = 0x003B5E7A;
            epiano.mixerInsert = 5;
            epiano.pan = -0.15f;
            epiano.generatorPlugin = makeDefaultPluginSlot("E-Piano", "builtin://epiano", true, 0);

            channels = [kick, snare, hat, bass, epiano];
        }

        if (tracks.length == 0) {
            static immutable string[13] names = [
                "Master", "Kick", "Snare", "Hi-Hats", "Acid Bass", "E-Piano",
                "Drum Bus", "Reverb Send", "Delay Send", "Synth Pad", "Vox", "Perc", "Sub"
            ];
            static immutable uint[13] cols = [
                0x0046505E, 0x00466480, 0x00664B78, 0x0033665E, 0x00785535, 0x003B5E7A,
                0x00526646, 0x0059436D, 0x003E5E6E, 0x006E4858, 0x004F5B78, 0x00615A3E, 0x003D5A52
            ];
            foreach (i; 0 .. 13) {
                auto idx = ensureTrackIndex(i);
                tracks[idx].name = names[i];
                tracks[idx].color = cols[i];
            }

            // Pre-populate realistic Mixer FX slots
            tracks[0].plugins = [
                makeDefaultPluginSlot("Fruity Limiter", "builtin://limiter", false, 0),
                makeDefaultPluginSlot("Parametric EQ", "builtin://eq", false, 1),
            ];
            tracks[1].plugins = [
                makeDefaultPluginSlot("Parametric EQ", "builtin://eq", false, 0),
                makeDefaultPluginSlot("Compressor", "builtin://compressor", false, 1),
            ];
            tracks[4].plugins = [
                makeDefaultPluginSlot("HOTT Multiband", "builtin://hott", false, 0),
                makeDefaultPluginSlot("Phaser", "builtin://phaser", false, 1),
            ];
            tracks[5].plugins = [
                makeDefaultPluginSlot("Chorus", "builtin://chorus", false, 0),
                makeDefaultPluginSlot("Stereo Reverb", "builtin://reverb", false, 1),
                makeDefaultPluginSlot("Ping-Pong Delay", "builtin://delay", false, 2),
            ];

            // Pre-populate Playlist Clips (Pattern, Audio, Automation)
            const double barSec = 4.0 * (60.0 / bpm);
            TimelineClipState p1;
            p1.clipIndex = 0;
            p1.patternIndex = 0;
            p1.kind = PlaylistClipKind.PATTERN;
            p1.name = "Pattern 1 (Drums)";
            p1.color = pal.clipPattern;
            p1.startTimeSec = 0.0;
            p1.durationSec = barSec * 2.0;

            TimelineClipState p1b = p1;
            p1b.clipIndex = 1;
            p1b.startTimeSec = barSec * 2.0;
            p1b.durationSec = barSec * 2.0;

            TimelineClipState p2;
            p2.clipIndex = 2;
            p2.patternIndex = 1;
            p2.kind = PlaylistClipKind.PATTERN;
            p2.name = "Pattern 2 (Acid Bass)";
            p2.color = 0x007A5032;
            p2.startTimeSec = 0.0;
            p2.durationSec = barSec * 4.0;

            TimelineClipState aud;
            aud.clipIndex = 3;
            aud.kind = PlaylistClipKind.AUDIO;
            aud.name = "loop140.wav";
            aud.color = pal.clipAudio;
            aud.startTimeSec = barSec;
            aud.durationSec = barSec * 2.0;
            aud.waveform = generateSampleWaveform(768, 140, true);

            TimelineClipState aut;
            aut.clipIndex = 4;
            aut.kind = PlaylistClipKind.AUTOMATION;
            aut.name = "Acid Cutoff Env";
            aut.color = pal.clipAutomation;
            aut.startTimeSec = 0.0;
            aut.durationSec = barSec * 4.0;
            aut.autoPoints = [
                AutomationPoint(0.0, 0.2f),
                AutomationPoint(0.25, 0.65f),
                AutomationPoint(0.50, 0.40f),
                AutomationPoint(0.75, 0.92f),
                AutomationPoint(1.0, 0.30f),
            ];

            tracks[1].clips ~= [p1, p1b];
            tracks[2].clips ~= p2;
            tracks[3].clips ~= aud;
            tracks[4].clips ~= aut;
        }

        if (timeMarkers.length == 0) {
            const double barSec = 4.0 * (60.0 / bpm);
            timeMarkers = [
                TimeMarkerState(0.0, "Intro", pal.accentOrange),
                TimeMarkerState(barSec * 2.0, "Drop", pal.accentGreen),
                TimeMarkerState(barSec * 4.0, "Break", pal.accentCyan),
            ];
        }

        if (activeMidi.events.length == 0) {
            activeMidi.trackIndex = 0;
            activeMidi.clipIndex = 0;
            activeMidi.channelIndex = 3; // Acid Bass
            activeMidi.resolution = 480;
            static immutable uint[8] pitches = [57, 57, 69, 60, 64, 67, 64, 60];
            static immutable uint[8] vels    = [115, 92, 118, 100, 110, 122, 96, 104];
            foreach (i; 0 .. 8) {
                MidiEvent ev;
                ev.tick = cast(long)(i * 240);
                ev.pitch = pitches[i];
                ev.duration_ticks = 180;
                ev.velocity = vels[i];
                activeMidi.events ~= ev;
            }
            if (channels.length > 3 && channels[3].notes.length == 0) {
                channels[3].notes = activeMidi.events.dup;
            }
        }

        if (samplePreview.waveform.length == 0) {
            samplePreview.waveform = generateSampleWaveform(512, 140, true);
        }
    }

    /// Saves current `activeMidi.events` back to its channel and loads `newChannelIdx`'s notes.
    void selectPianoRollChannel(int newChannelIdx) @safe {
        if (channels.length == 0) {
            activeMidi.channelIndex = newChannelIdx;
            return;
        }
        const int prev = activeMidi.channelIndex;
        if (prev >= 0 && prev < cast(int)channels.length) {
            channels[prev].notes = activeMidi.events.dup;
        }
        int clamped = newChannelIdx % cast(int)channels.length;
        if (clamped < 0) clamped += cast(int)channels.length;
        activeMidi.channelIndex = clamped;
        selectedChannelIndex = clamped;
        activeMidi.events = channels[clamped].notes.dup;
    }

    /// Returns the next unique clipIndex for `trk`.
    int nextClipIndex(ref const(TrackStripState) trk) const pure nothrow @safe {
        int maxIdx = -1;
        foreach (ref c; trk.clips) {
            if (c.clipIndex > maxIdx) maxIdx = c.clipIndex;
        }
        return maxIdx + 1;
    }

    /// Ensures a track slot exists for `trackIndex` and returns its array index.
    size_t ensureTrackIndex(int trackIndex) @safe {
        foreach (i, ref t; tracks) {
            if (t.index == trackIndex) return i;
        }
        TrackStripState t;
        t.index = trackIndex;
        t.name = (trackIndex == 0) ? "Master" : ("Insert " ~ to!string(trackIndex));
        t.color = palette().clipPattern;
        tracks ~= t;
        return tracks.length - 1;
    }

    /// Returns pointer to the selected track's `PluginSlotState` if present.
    PluginSlotState* findPlugin(int trackIdx, int pluginIdx) @trusted {
        foreach (ref t; tracks) {
            if (t.index == trackIdx) {
                foreach (ref p; t.plugins) {
                    if (p.pluginIndex == pluginIdx) return &p;
                }
            }
        }
        return null;
    }

    /// Current 16th-step index `[0..15]` for the Step Sequencer playhead indicator.
    int currentStepIndex() const pure nothrow @safe {
        if (!isPlaying) return -1;
        const double beats = playheadPosSec * ((bpm > 0.0 ? bpm : 140.0) / 60.0);
        const int step = cast(int)(beats * 4.0) % 16;
        return step < 0 ? 0 : step;
    }

    /// Returns snap step size in beats for the active `snapMode`.
    double snapBeats() const pure nothrow @safe {
        final switch (snapMode) {
            case SnapMode.LINE:
            case SnapMode.CELL:
            case SnapMode.BEAT_1_4:
            case SnapMode.STEP:
                return 0.25;
            case SnapMode.STEP_1_4:
                return 0.0625;
            case SnapMode.STEP_1_2:
                return 0.125;
            case SnapMode.BEAT_1_2:
                return 0.5;
            case SnapMode.BEAT:
                return 1.0;
            case SnapMode.BAR:
                return 4.0;
            case SnapMode.NONE:
                return 0.0;
        }
    }

    /// Applies an engine `Notification` to update transport, mixer, timeline, MIDI, and plugin state.
    void applyNotification(ref const(Notification) notif) @safe {
        if (notif.playhead_info.hasValue) {
            const p = notif.playhead_info.value;
            playheadPosSec = p.position_sec;
            if (p.bpm > 0.0f) bpm = p.bpm;
            isPlaying = (p.transport_state == TransportState.TRANSPORT_STATE_PLAYING ||
                         p.transport_state == TransportState.TRANSPORT_STATE_RECORDING);
            isRecording = (p.transport_state == TransportState.TRANSPORT_STATE_RECORDING);
            loopEnabled = p.loop_enabled;
            loopStartSec = p.loop_start;
            loopEndSec = p.loop_end;
            cpuPercent = p.cpu_percent;
            oscilloscopeSamples = p.oscilloscope.dup;
        }
        if (notif.track_levels.hasValue) {
            foreach (lvl; notif.track_levels.value.levels) {
                const idx = ensureTrackIndex(lvl.track_index);
                tracks[idx].peakL = lvl.peak_l;
                tracks[idx].peakR = lvl.peak_r;
            }
        }
        if (notif.track_info.hasValue) {
            const ti = notif.track_info.value;
            const idx = ensureTrackIndex(ti.track_index);
            if (ti.name.length > 0) tracks[idx].name = ti.name.idup;
            if (ti.volume.hasValue) tracks[idx].volume = ti.volume.value;
            if (ti.pan.hasValue) tracks[idx].pan = ti.pan.value;
            tracks[idx].muted = ti.muted;
            tracks[idx].soloed = ti.soloed;
            tracks[idx].routedTo = ti.output_track_index < 0 ? 0 : ti.output_track_index;
        }
        if (notif.timeline_clip_info.hasValue) {
            const tci = notif.timeline_clip_info.value;
            const idx = ensureTrackIndex(tci.track_index);
            bool found = false;
            foreach (ref c; tracks[idx].clips) {
                if (c.clipIndex == tci.clip_index) {
                    if (tci.name.length > 0) c.name = tci.name.idup;
                    c.startTimeSec = tci.start_time;
                    c.durationSec = tci.duration;
                    c.waveform = tci.waveform.dup;
                    c.kind = tci.waveform.length > 0 ? PlaylistClipKind.AUDIO : PlaylistClipKind.PATTERN;
                    found = true;
                    break;
                }
            }
            if (!found) {
                TimelineClipState c;
                c.clipIndex = tci.clip_index;
                c.name = tci.name.length > 0 ? tci.name.idup : "Clip";
                c.color = tci.waveform.length > 0 ? palette().clipAudio : palette().clipPattern;
                c.startTimeSec = tci.start_time;
                c.durationSec = tci.duration;
                c.waveform = tci.waveform.dup;
                c.kind = tci.waveform.length > 0 ? PlaylistClipKind.AUDIO : PlaylistClipKind.PATTERN;
                tracks[idx].clips ~= c;
            }
        }
        if (notif.param_list.hasValue) {
            const pl = notif.param_list.value;
            const idx = ensureTrackIndex(pl.track_index);
            PluginSlotState slot;
            slot.pluginIndex = pl.plugin_index;
            slot.name = pl.plugin_name.length > 0 ? pl.plugin_name.idup : "Plugin";
            slot.isInstrument = pl.is_instrument;
            slot.enabled = !pl.bypassed;
            foreach (pm; pl.params) {
                ParamItemState item;
                item.id = pm.id;
                item.name = pm.name.idup;
                item.defaultValue = pm.default_value;
                item.currentValue = pm.default_value;
                slot.params ~= item;
            }
            bool updated = false;
            foreach (ref existing; tracks[idx].plugins) {
                if (existing.pluginIndex == pl.plugin_index) {
                    existing = slot;
                    updated = true;
                    break;
                }
            }
            if (!updated) tracks[idx].plugins ~= slot;
            selectedTrack = pl.track_index;
            selectedPluginIndex = pl.plugin_index;
        }
        if (notif.param_value_change.hasValue) {
            const pvc = notif.param_value_change.value;
            if (auto plug = findPlugin(pvc.track_index, pvc.plugin_index)) {
                foreach (ref pm; plug.params) {
                    if (pm.id == pvc.param_id) {
                        pm.currentValue = pvc.value;
                        break;
                    }
                }
            }
        }
        if (notif.clip_midi_data.hasValue) {
            const md = notif.clip_midi_data.value;
            activeMidi.trackIndex = md.track_index;
            activeMidi.clipIndex = md.clip_index;
            activeMidi.resolution = (md.resolution > 0) ? md.resolution : 480;
            activeMidi.events = md.events.dup;
        }
        if (notif.plugin_list.hasValue) {
            foreach (pd; notif.plugin_list.value.plugins) {
                bool dup = false;
                foreach (existing; scannedPlugins) {
                    if (existing.path == pd.path && existing.index == pd.index) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) scannedPlugins ~= pd;
            }
        }
        if (notif.clear_project.hasValue) {
            tracks.length = 0;
            activeMidi = ActiveMidiClip.init;
            playheadPosSec = 0.0;
            isPlaying = false;
            isRecording = false;
            selectedTrack = 0;
            selectedClipIndex = 0;
            selectedPluginIndex = 0;
        }
        if (notif.log.hasValue && notif.log.value.message.length > 0) {
            statusMessage = notif.log.value.message.idup;
        } else if (notif.acknowledge.hasValue) {
            const ack = notif.acknowledge.value;
            statusMessage = ack.command_type.idup ~ (ack.success ? ": OK" : ": FAILED");
        }
    }
}

unittest {
    import pb.notifications : ClearProject, ClipMidiData, ParamInfo, ParamList,
        PlayheadInfo, TrackLevel, TrackLevels;

    TransportModel model;
    model.initDefaultTemplate();
    assert(model.channels.length == 5);
    assert(model.patterns.length == 4);
    assert(model.tracks.length == 13);
    assert(model.activeMidi.events.length == 8);
    assert(model.tracks[0].plugins.length == 2);

    Notification n1;
    PlayheadInfo pi;
    pi.position_sec = 4.5f;
    pi.bpm = 128.0f;
    pi.transport_state = TransportState.TRANSPORT_STATE_PLAYING;
    pi.loop_enabled = true;
    pi.loop_start = 2.0f;
    pi.loop_end = 10.0f;
    n1.playhead_info.set(pi);
    model.applyNotification(n1);

    assert(model.isPlaying);
    assert(model.bpm == 128.0f);
    assert(model.playheadPosSec == 4.5f);
    assert(model.loopEnabled);
    assert(model.currentStepIndex() >= 0 && model.currentStepIndex() < 16);

    Notification n2;
    TrackLevels tls;
    TrackLevel tl;
    tl.track_index = 0;
    tl.peak_l = 0.75f;
    tl.peak_r = 0.50f;
    tls.levels ~= tl;
    n2.track_levels.set(tls);
    model.applyNotification(n2);
    assert(model.tracks[0].peakL == 0.75f);

    Notification nPlug;
    ParamList pl;
    pl.track_index = 0;
    pl.plugin_index = 5;
    pl.plugin_name = "Acid Filter";
    pl.is_instrument = false;
    ParamInfo p0;
    p0.id = 0;
    p0.name = "Cutoff";
    p0.default_value = 0.5f;
    pl.params ~= p0;
    nPlug.param_list.set(pl);
    model.applyNotification(nPlug);
    assert(model.findPlugin(0, 5) !is null);

    Notification nMidi;
    ClipMidiData md;
    md.track_index = 0;
    md.clip_index = 0;
    md.resolution = 480;
    MidiEvent ev;
    ev.tick = 0;
    ev.pitch = 60;
    ev.duration_ticks = 240;
    ev.velocity = 100;
    md.events ~= ev;
    nMidi.clip_midi_data.set(md);
    model.applyNotification(nMidi);
    assert(model.activeMidi.events.length == 1);

    Notification n4;
    ClearProject cp;
    n4.clear_project.set(cp);
    model.applyNotification(n4);
    assert(model.tracks.length == 0);
    assert(!model.isPlaying);

    // 3. Test selectPianoRollChannel preserves per-channel notes and wraps safely
    model.initDefaultTemplate();
    assert(model.activeMidi.channelIndex == 3);
    assert(model.activeMidi.events.length == 8);
    model.selectPianoRollChannel(0);
    assert(model.activeMidi.channelIndex == 0);
    assert(model.activeMidi.events.length == 0);
    MidiEvent kickEv;
    kickEv.tick = 0;
    kickEv.pitch = 60;
    kickEv.duration_ticks = 120;
    kickEv.velocity = 120;
    model.activeMidi.events ~= kickEv;
    model.selectPianoRollChannel(3);
    assert(model.activeMidi.events.length == 8);
    model.selectPianoRollChannel(0);
    assert(model.activeMidi.events.length == 1 && model.activeMidi.events[0].pitch == 60);

    // 4. Test nextClipIndex after deletions/slices
    TrackStripState tr;
    TimelineClipState c0, c5;
    c0.clipIndex = 0;
    c5.clipIndex = 5;
    tr.clips = [c0, c5];
    assert(model.nextClipIndex(tr) == 6);

    // 5. Test PlayheadInfo CPU%, oscilloscope waveform, and TrackInfo mix sync
    import pb.notifications : TrackInfo;
    Notification nTel;
    PlayheadInfo tel;
    tel.position_sec = 2.0f;
    tel.bpm = 142.0f;
    tel.transport_state = TransportState.TRANSPORT_STATE_RECORDING;
    tel.cpu_percent = 18;
    tel.oscilloscope = [0.25f, -0.5f, 0.75f];
    nTel.playhead_info.set(tel);
    model.applyNotification(nTel);
    assert(model.isRecording && model.isPlaying);
    assert(model.cpuPercent == 18);
    assert(model.oscilloscopeSamples.length == 3 && model.oscilloscopeSamples[1] == -0.5f);

    Notification nMix;
    TrackInfo ti;
    ti.track_index = 2;
    ti.name = "Bass Bus";
    ti.volume.set(0.65f);
    ti.pan.set(0.25f);
    ti.muted = true;
    ti.soloed = false;
    nMix.track_info.set(ti);
    model.applyNotification(nMix);
    const size_t t2 = model.ensureTrackIndex(2);
    assert(model.tracks[t2].name == "Bass Bus");
    assert(model.tracks[t2].muted);
    assert(model.tracks[t2].volume > 0.64f && model.tracks[t2].volume < 0.66f);

    Notification nByp;
    ParamList plByp;
    plByp.track_index = 2;
    plByp.plugin_index = 0;
    plByp.plugin_name = "EQ";
    plByp.bypassed = true;
    nByp.param_list.set(plByp);
    model.applyNotification(nByp);
    assert(!model.findPlugin(2, 0).enabled);
}
