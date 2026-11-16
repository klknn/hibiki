module gui_d.transport_model;

import core.stdc.math : expf, sinf;
import gui_d.theme : FlPalette, FlThemeId, getFlPalette;
import pb.core : MidiEvent, PbPlaybackMode = PlaybackMode, TransportState;
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
    MIDI,
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
    int laneIndex = 0;
    int patternIndex = 0;
    int pluginIndex = 0;
    uint paramId = 0;
    PlaylistClipKind kind = PlaylistClipKind.PATTERN;
    string name = "Pattern 1";
    string path;
    uint color = 0x00385770;
    double startTimeSec = 0.0;
    double durationSec = 4.0;
    double trimStartBeats = 0.0;
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

/// Per-pad state for `builtin://drum_machine`.
struct DrumPadSlotState {
    int padIndex = 0;
    string name = "Pad";
    string sampleName;
    float volume = 0.8f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    uint triggerNote = 36;
    float[] sampleWaveform;
    bool activeFlash = false;
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

    // Phase 4 live DSP telemetry, sampler waveform, drum pads, and remote VST3 editor frame
    float[] specInDb;
    float[] specOutDb;
    float meterInDb = -96.0f;
    float meterOutDb = -96.0f;
    float meterGrDb = 0.0f;
    float meterLowGrDb = 0.0f;
    float meterMidGrDb = 0.0f;
    float meterHighGrDb = 0.0f;
    float meterLowInDb = -96.0f;
    float meterMidInDb = -96.0f;
    float meterHighInDb = -96.0f;
    bool hasMetering = false;
    float[] scopeL;
    float[] scopeR;
    string sampleName;
    float[] sampleWaveform;
    double sampleDurationSec = 0.0;
    DrumPadSlotState[] drumPads;
    bool remoteEditorOpen = false;
    int editorW = 0;
    int editorH = 0;
    ubyte[] editorRgba;
    uint editorRevision = 0;
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
    bool[64] steps;
    float[64] stepVelocities;
    float[64] stepPitches; // [-1..1] (0 = C5)
    float[64] stepPans;    // [-1..1]
    float[64] stepShifts;  // [0..1]
    MidiEvent[] notes;
    PluginSlotState generatorPlugin;

    void initDefaults() @safe {
        foreach (i; 0 .. 64) {
            steps[i] = false;
            stepVelocities[i] = 0.78f;
            stepPitches[i] = 0.0f;
            stepPans[i] = 0.0f;
            stepShifts[i] = 0.0f;
        }
    }
}

/// Per-channel step sequencer & Piano Roll data saved per Pattern.
struct ChannelPatternSteps {
    int channelIndex = 0;
    bool[64] steps;
    float[64] stepVelocities;
    float[64] stepPitches;
    float[64] stepPans;
    float[64] stepShifts;
    MidiEvent[] notes;

    void initDefaults() @safe {
        foreach (i; 0 .. 64) {
            steps[i] = false;
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
    ChannelPatternSteps[] channelSteps;
}

/// Mixer Insert (Master + Inserts 1..12) and Playlist Track Lane state.
struct TrackStripState {
    int index = 0;
    string name = "Track";
    uint color = 0x00385770;
    float volume = 1.0f;    // linear gain (1.0 = 0 dB unity = 0.80 FL fader)
    float pan = 0.0f;       // [-1..1]
    float stereoSep = 0.0f; // [-1..1] (Merged <-> Separated)
    bool muted = false;
    bool soloed = false;
    bool recordArmed = false;
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
    PlaybackMode playbackMode = PlaybackMode.SONG;
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
    int activeAutomationPluginIndex = 0;
    uint activeAutomationParamId = 0;
    string activeAutomationTargetName = "Slot 0 : Cutoff / Gain";
    bool showClipPicker = true;
    bool showMixerFxRack = true;
    bool showVelocityLane = true;
    bool ghostNotesEnabled = true;
    string scaleHighlightName = "A Minor";
    bool graphEditorOpen = false;
    GraphEditorMode graphEditorMode = GraphEditorMode.VELOCITY;
    ChannelFilterMode channelFilter = ChannelFilterMode.ALL;

    static struct AutomationTargetChoice {
        int pluginIndex = 0;
        uint paramId = 0;
        string label;
    }

    int activePatternLengthSteps() const pure nothrow @safe {
        if (selectedPatternIndex >= 0 && selectedPatternIndex < cast(int)patterns.length) {
            const int l = patterns[selectedPatternIndex].lengthSteps;
            if (l >= 1 && l <= 64) return l;
        }
        return 16;
    }

    AutomationTargetChoice[] collectAutomationTargets(int trackIdx) const @safe {
        AutomationTargetChoice[] res;
        foreach (const ref t; tracks) {
            if (t.index != trackIdx) continue;
            foreach (const ref p; t.plugins) {
                if (p.params.length == 0) {
                    res ~= AutomationTargetChoice(p.pluginIndex, 0, p.name ~ " : Mix/Gain");
                } else {
                    foreach (const ref prm; p.params) {
                        res ~= AutomationTargetChoice(p.pluginIndex, prm.id, p.name ~ " : " ~ prm.name);
                        if (res.length >= 16) return res;
                    }
                }
            }
            break;
        }
        if (res.length == 0) {
            res ~= AutomationTargetChoice(0, 0, "Slot 0 : Cutoff / Param 0");
            res ~= AutomationTargetChoice(0, 1, "Slot 0 : Param 1");
        }
        return res;
    }

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

    // Phase 5 Project Paths, Audio/Plugin Settings, and Input Devices
    struct InputDeviceInfo {
        string id;
        string name;
        int channelCount = 2;
    }
    string currentProjectPath = "build/hibiki_save.hbk";
    string currentProjectDir;
    string lastBouncePath = "build/hibiki_bounce.wav";
    string[] sampleScanDirs = ["testdata"];
    int bufferLatencyMs = 10;
    bool useDoublePrecision = false;
    bool sandboxPlugins = false;
    string[] remotePluginHosts;
    InputDeviceInfo[] audioInputs;
    InputDeviceInfo[] midiInputs;

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
        if (uri == "builtin://sylph") {
            // Sylph maintains its own 46-parameter Trance/Kick defaults until engine PLUGIN_INFO syncs.
            return slot;
        }
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
            kick.steps[0 .. 16] = [true, false, false, false, true, false, false, false,
                                   true, false, false, false, true, false, false, false];
            kick.stepVelocities[0 .. 16] = [0.95f, 0.78f, 0.78f, 0.78f, 0.90f, 0.78f, 0.78f, 0.78f,
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
            snare.steps[0 .. 16] = [false, false, false, false, true, false, false, false,
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
            hat.steps[0 .. 16] = [true, false, true, false, true, false, true, false,
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
            p2.clipIndex = 0;
            p2.patternIndex = 1;
            p2.kind = PlaylistClipKind.PATTERN;
            p2.name = "Pattern 2 (Acid Bass)";
            p2.color = 0x007A5032;
            p2.startTimeSec = 0.0;
            p2.durationSec = barSec * 4.0;

            TimelineClipState aud;
            aud.clipIndex = 0;
            aud.kind = PlaylistClipKind.AUDIO;
            aud.name = "loop140.wav";
            aud.color = pal.clipAudio;
            aud.startTimeSec = barSec;
            aud.durationSec = barSec; // 1 bar at 140 BPM (1.714s), matching testdata/loop140.wav
            aud.waveform = generateSampleWaveform(768, 140, true);

            TimelineClipState aut;
            aut.clipIndex = 0;
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

        if (activeMidi.events.length == 0 && activeMidi.channelIndex != 0) {
            activeMidi.trackIndex = 0;
            activeMidi.clipIndex = 0;
            activeMidi.channelIndex = 0; // Pattern 1 (Drums) -> DR-8 Kick by default
            activeMidi.resolution = 480;
        }

        if (samplePreview.waveform.length == 0) {
            samplePreview.waveform = generateSampleWaveform(512, 140, true);
        }

        // Seed distinct pattern contents so PAT/SONG mode & Pattern switching have rich data
        if (patterns.length > 0 && patterns[0].channelSteps.length == 0) {
            // Pattern 0 (Pattern 1 Drums): Kick, Snare, Hat ONLY (no Acid Bass leak)
            saveCurrentPatternToStore();
            // Pattern 1 (Pattern 2 Acid Bass): Bass Piano Roll melody + Acid Bass steps + 4-on-floor kick
            if (patterns.length > 1) {
                ChannelPatternSteps kick1;
                kick1.initDefaults();
                kick1.channelIndex = 0;
                kick1.steps[0 .. 16] = [true, false, false, false, true, false, false, false,
                                        true, false, false, false, true, false, false, false];
                ChannelPatternSteps bass1;
                bass1.initDefaults();
                bass1.channelIndex = 3;
                bass1.steps[0 .. 16] = [true, false, true, false, false, true, false, true,
                                        true, false, false, true, false, true, true, false];
                static immutable uint[8] pitches = [57, 57, 69, 60, 64, 67, 64, 60];
                static immutable uint[8] vels    = [115, 92, 118, 100, 110, 122, 96, 104];
                foreach (i; 0 .. 8) {
                    MidiEvent ev;
                    ev.tick = cast(long)(i * 240);
                    ev.pitch = pitches[i];
                    ev.duration_ticks = 180;
                    ev.velocity = vels[i];
                    bass1.notes ~= ev;
                }
                patterns[1].channelSteps = [kick1, bass1];
            }
            // Pattern 2 (E-Piano): Lush Am7 chord progression
            if (patterns.length > 2) {
                ChannelPatternSteps ep2;
                ep2.initDefaults();
                ep2.channelIndex = 4;
                static immutable uint[4] chordA = [57, 60, 64, 67];
                static immutable uint[4] chordF = [53, 57, 60, 64];
                foreach (n; chordA) {
                    MidiEvent ev;
                    ev.tick = 0;
                    ev.pitch = n;
                    ev.duration_ticks = 900;
                    ev.velocity = 96;
                    ep2.notes ~= ev;
                }
                foreach (n; chordF) {
                    MidiEvent ev;
                    ev.tick = 960;
                    ev.pitch = n;
                    ev.duration_ticks = 900;
                    ev.velocity = 92;
                    ep2.notes ~= ev;
                }
                patterns[2].channelSteps = [ep2];
            }
            // Pattern 3 (Fill): Snare/Hat transition roll
            if (patterns.length > 3) {
                ChannelPatternSteps sn3;
                sn3.initDefaults();
                sn3.channelIndex = 1;
                sn3.steps[0 .. 16] = [false, false, false, false, true, false, false, false,
                                      true, false, true, true, true, true, true, true];
                ChannelPatternSteps ht3;
                ht3.initDefaults();
                ht3.channelIndex = 2;
                ht3.steps[0 .. 16] = [true, true, true, true, true, true, true, true,
                                      true, true, true, true, true, true, true, true];
                patterns[3].channelSteps = [sn3, ht3];
            }
        }
    }

    /// Snapshots current `channels` steps and Piano Roll notes into `patterns[selectedPatternIndex]`.
    void saveCurrentPatternToStore() @safe {
        if (selectedPatternIndex < 0 || selectedPatternIndex >= cast(int)patterns.length) return;
        if (activeMidi.channelIndex >= 0 && activeMidi.channelIndex < cast(int)channels.length) {
            channels[activeMidi.channelIndex].notes = activeMidi.events.dup;
        }
        ChannelPatternSteps[] list;
        foreach (ref ch; channels) {
            ChannelPatternSteps cp;
            cp.channelIndex = ch.index;
            cp.steps = ch.steps;
            cp.stepVelocities = ch.stepVelocities;
            cp.stepPitches = ch.stepPitches;
            cp.stepPans = ch.stepPans;
            cp.stepShifts = ch.stepShifts;
            cp.notes = ch.notes.dup;
            list ~= cp;
        }
        patterns[selectedPatternIndex].channelSteps = list;
    }

    /// Loads `patIdx`'s saved steps/notes into `channels` and `activeMidi`.
    void loadPatternFromStore(int patIdx) @safe {
        if (patIdx < 0 || patIdx >= cast(int)patterns.length) return;
        foreach (ref ch; channels) {
            bool found = false;
            foreach (ref cp; patterns[patIdx].channelSteps) {
                if (cp.channelIndex == ch.index) {
                    ch.steps = cp.steps;
                    ch.stepVelocities = cp.stepVelocities;
                    ch.stepPitches = cp.stepPitches;
                    ch.stepPans = cp.stepPans;
                    ch.stepShifts = cp.stepShifts;
                    ch.notes = cp.notes.dup;
                    found = true;
                    break;
                }
            }
            if (!found) {
                foreach (i; 0 .. 64) {
                    ch.steps[i] = false;
                    ch.stepVelocities[i] = 0.78f;
                    ch.stepPitches[i] = 0.0f;
                    ch.stepPans[i] = 0.0f;
                    ch.stepShifts[i] = 0.0f;
                }
                ch.notes = null;
            }
        }
        if (activeMidi.channelIndex >= 0 && activeMidi.channelIndex < cast(int)channels.length) {
            activeMidi.events = channels[activeMidi.channelIndex].notes.dup;
        }
    }

    private bool channelHasAnySteps(int chIdx, int patLen) const @safe {
        if (chIdx < 0 || chIdx >= cast(int)channels.length) return false;
        foreach (s; 0 .. patLen) {
            if (s < cast(int)channels[chIdx].steps.length && channels[chIdx].steps[s]) {
                return true;
            }
        }
        return false;
    }

    /// Selects the most appropriate channel to display after loading a pattern into `channels`.
    private int resolvePreferredChannelForLoadedPattern(int currentCh) const @safe {
        if (channels.length == 0) return 0;
        const int patLen = activePatternLengthSteps();
        int firstWithNotes = -1;
        int firstWithSteps = -1;
        foreach (ci, const ref ch; channels) {
            if (firstWithNotes < 0 && ch.notes.length > 0) firstWithNotes = cast(int)ci;
            if (firstWithSteps < 0 && channelHasAnySteps(cast(int)ci, patLen)) firstWithSteps = cast(int)ci;
        }
        const bool curValid = currentCh >= 0 && currentCh < cast(int)channels.length;
        if (firstWithNotes >= 0) {
            if (curValid && channels[currentCh].notes.length > 0) return currentCh;
            return firstWithNotes;
        }
        if (firstWithSteps >= 0) {
            if (curValid && channelHasAnySteps(currentCh, patLen)) return currentCh;
            return firstWithSteps;
        }
        if (selectedChannelIndex >= 0 && selectedChannelIndex < cast(int)channels.length) {
            return selectedChannelIndex;
        }
        return 0;
    }

    /// Switches active Channel Rack pattern, saving the outgoing pattern and restoring the incoming one.
    void selectPattern(int newPatIdx) @safe {
        if (patterns.length == 0) return;
        const int n = cast(int)patterns.length;
        int clamped = newPatIdx % n;
        if (clamped < 0) clamped += n;
        if (clamped == selectedPatternIndex && activeMidi.channelIndex >= 0) return;
        if (clamped != selectedPatternIndex) {
            saveCurrentPatternToStore();
            selectedPatternIndex = clamped;
        }
        activePickerKind = PlaylistClipKind.PATTERN;
        activePickerIndex = clamped;
        loadPatternFromStore(clamped);
        if (channels.length > 0) {
            const int targetCh = resolvePreferredChannelForLoadedPattern(activeMidi.channelIndex);
            activeMidi.channelIndex = targetCh;
            selectedChannelIndex = targetCh;
            activeMidi.events = channels[targetCh].notes.dup;
        }
    }

    /// Opens `newPatIdx` for Piano Roll editing, ensuring a populated channel is targeted and loaded.
    void selectPatternForPianoRoll(int newPatIdx) @safe {
        if (patterns.length == 0) return;
        const int n = cast(int)patterns.length;
        int clamped = newPatIdx % n;
        if (clamped < 0) clamped += n;
        if (clamped != selectedPatternIndex) {
            saveCurrentPatternToStore();
            selectedPatternIndex = clamped;
        }
        activePickerKind = PlaylistClipKind.PATTERN;
        activePickerIndex = clamped;
        loadPatternFromStore(clamped);

        if (channels.length == 0) return;
        const int targetCh = resolvePreferredChannelForLoadedPattern(activeMidi.channelIndex);
        activeMidi.channelIndex = targetCh;
        selectedChannelIndex = targetCh;
        activeMidi.events = channels[targetCh].notes.dup;
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

    /// Returns the next unique backend-aligned clipIndex for `trk` within `kind` (`AUTOMATION` vs timeline clips).
    int nextClipIndex(ref const(TrackStripState) trk, PlaylistClipKind kind, int laneIdx = 0) const pure nothrow @safe {
        int maxIdx = -1;
        foreach (ref c; trk.clips) {
            if (kind == PlaylistClipKind.AUTOMATION) {
                if (c.kind == PlaylistClipKind.AUTOMATION && c.laneIndex == laneIdx && c.clipIndex > maxIdx) {
                    maxIdx = c.clipIndex;
                }
            } else if (c.kind != PlaylistClipKind.AUTOMATION) {
                if (c.clipIndex > maxIdx) maxIdx = c.clipIndex;
            }
        }
        return maxIdx + 1;
    }

    /// Resequences clip indices on `trk` to stay 1:1 with backend `timeline_clips` (`0..N-1`) and `automation_lanes[lane].clips` (`0..K-1`).
    void resequenceTrackClips(ref TrackStripState trk) const pure nothrow @safe {
        int nextTimelineIdx = 0;
        foreach (i, ref c; trk.clips) {
            if (c.kind == PlaylistClipKind.AUTOMATION) {
                int autoIdx = 0;
                foreach (j; 0 .. i) {
                    if (trk.clips[j].kind == PlaylistClipKind.AUTOMATION &&
                        trk.clips[j].laneIndex == c.laneIndex) {
                        autoIdx++;
                    }
                }
                c.clipIndex = autoIdx;
            } else {
                c.clipIndex = nextTimelineIdx++;
            }
        }
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

    /// Returns pointer to the selected track or channel generator's `PluginSlotState` if present.
    PluginSlotState* findPlugin(int trackIdx, int pluginIdx) @trusted {
        if (trackIdx >= 100) {
            const int chIdx = trackIdx - 100;
            if (chIdx >= 0 && chIdx < cast(int)channels.length) {
                return &channels[chIdx].generatorPlugin;
            }
            return null;
        }
        foreach (ref t; tracks) {
            if (t.index == trackIdx) {
                foreach (ref p; t.plugins) {
                    if (p.pluginIndex == pluginIdx) return &p;
                }
            }
        }
        return null;
    }

    /// Current 16th-step index `[0..patLen-1]` for the Step Sequencer playhead indicator.
    int currentStepIndex() const pure nothrow @safe {
        if (!isPlaying) return -1;
        const double beats = playheadPosSec * ((bpm > 0.0 ? bpm : 140.0) / 60.0);
        const int len = activePatternLengthSteps();
        const int step = cast(int)(beats * 4.0) % len;
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

    /// Applies an engine `Notification` to update transport, mixer, timeline, MIDI, sequencer, and plugin state.
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
            if (p.playback_mode != PbPlaybackMode.PLAYBACK_MODE_UNSPECIFIED) {
                playbackMode = (p.playback_mode == PbPlaybackMode.PLAYBACK_MODE_PAT)
                    ? PlaybackMode.PAT : PlaybackMode.SONG;
                swingAmount = p.swing_amount;
                if (p.selected_pattern_index >= 0 &&
                    p.selected_pattern_index < cast(int)patterns.length &&
                    p.selected_pattern_index != selectedPatternIndex) {
                    selectPattern(p.selected_pattern_index);
                }
            }
        }
        if (notif.track_levels.hasValue) {
            foreach (lvl; notif.track_levels.value.levels) {
                if (lvl.track_index >= 100) continue;
                const idx = ensureTrackIndex(lvl.track_index);
                tracks[idx].peakL = lvl.peak_l;
                tracks[idx].peakR = lvl.peak_r;
            }
        }
        if (notif.track_info.hasValue) {
            const ti = notif.track_info.value;
            if (ti.track_index >= 100) {
                const int chIdx = ti.track_index - 100;
                if (chIdx >= 0 && chIdx < cast(int)channels.length) {
                    if (ti.name.length > 0) channels[chIdx].name = ti.name.idup;
                    if (ti.volume.hasValue) channels[chIdx].volume = ti.volume.value;
                    if (ti.pan.hasValue) channels[chIdx].pan = ti.pan.value;
                    channels[chIdx].muted = ti.muted;
                    channels[chIdx].soloed = ti.soloed;
                    if (ti.output_track_index >= 0) channels[chIdx].mixerInsert = ti.output_track_index;
                }
            } else {
                const idx = ensureTrackIndex(ti.track_index);
                if (ti.name.length > 0) tracks[idx].name = ti.name.idup;
                if (ti.volume.hasValue) tracks[idx].volume = ti.volume.value;
                if (ti.pan.hasValue) tracks[idx].pan = ti.pan.value;
                if (ti.eq_low.hasValue) tracks[idx].eqLow = ti.eq_low.value;
                if (ti.eq_mid.hasValue) tracks[idx].eqMid = ti.eq_mid.value;
                if (ti.eq_high.hasValue) tracks[idx].eqHigh = ti.eq_high.value;
                if (ti.stereo_sep.hasValue) tracks[idx].stereoSep = ti.stereo_sep.value;
                tracks[idx].muted = ti.muted;
                tracks[idx].soloed = ti.soloed;
                tracks[idx].routedTo = ti.output_track_index < 0 ? 0 : ti.output_track_index;
            }
        }
        if (notif.timeline_clip_info.hasValue) {
            const tci = notif.timeline_clip_info.value;
            if (tci.track_index >= 0 && tci.track_index < 100) {
                const idx = ensureTrackIndex(tci.track_index);
                const bool isPat = tci.pattern_index.hasValue && tci.pattern_index.value >= 0;
                const int patIdx = isPat ? tci.pattern_index.value : 0;
                uint patColor = palette().clipPattern;
                if (isPat && patIdx >= 0 && patIdx < cast(int)patterns.length) {
                    patColor = patterns[patIdx].color;
                }
                const PlaylistClipKind resolvedKind = isPat
                    ? PlaylistClipKind.PATTERN
                    : (tci.is_midi ? PlaylistClipKind.MIDI
                                   : (tci.waveform.length > 0 ? PlaylistClipKind.AUDIO : PlaylistClipKind.PATTERN));
                const uint resolvedColor = isPat
                    ? patColor
                    : (resolvedKind == PlaylistClipKind.AUDIO ? palette().clipAudio : palette().clipPattern);
                bool found = false;
                foreach (ref c; tracks[idx].clips) {
                    if (c.kind != PlaylistClipKind.AUTOMATION && c.clipIndex == tci.clip_index) {
                        if (isPat && patIdx >= 0 && patIdx < cast(int)patterns.length &&
                            patterns[patIdx].name.length > 0) {
                            c.name = patterns[patIdx].name;
                        } else if (tci.name.length > 0) {
                            c.name = tci.name.idup;
                        }
                        if (tci.path.length > 0) c.path = tci.path.idup;
                        c.startTimeSec = tci.start_time;
                        c.durationSec = tci.duration;
                        c.muted = tci.muted;
                        c.waveform = tci.waveform.dup;
                        c.kind = resolvedKind;
                        c.patternIndex = patIdx;
                        c.color = resolvedColor;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    TimelineClipState c;
                    c.clipIndex = tci.clip_index;
                    c.name = tci.name.length > 0 ? tci.name.idup : "Clip";
                    c.path = tci.path.idup;
                    c.color = resolvedColor;
                    c.startTimeSec = tci.start_time;
                    c.durationSec = tci.duration;
                    c.muted = tci.muted;
                    c.waveform = tci.waveform.dup;
                    c.kind = resolvedKind;
                    c.patternIndex = patIdx;
                    tracks[idx].clips ~= c;
                }
            }
        }
        if (notif.param_list.hasValue) {
            const pl = notif.param_list.value;
            if (pl.plugin_name.length == 0 && pl.params.length == 0) {
                if (pl.track_index >= 100) {
                    const int chIdx = pl.track_index - 100;
                    if (chIdx >= 0 && chIdx < cast(int)channels.length) {
                        channels[chIdx].generatorPlugin = PluginSlotState.init;
                    }
                } else {
                    foreach (ref t; tracks) {
                        if (t.index != pl.track_index) continue;
                        PluginSlotState[] kept;
                        foreach (ref p; t.plugins) {
                            if (p.pluginIndex == pl.plugin_index) continue;
                            if (p.pluginIndex > pl.plugin_index) p.pluginIndex -= 1;
                            kept ~= p;
                        }
                        t.plugins = kept;
                        if (selectedTrack == pl.track_index &&
                            selectedPluginIndex >= cast(int)t.plugins.length) {
                            selectedPluginIndex = t.plugins.length > 0
                                ? cast(int)t.plugins.length - 1 : 0;
                        }
                        break;
                    }
                }
            } else {
                PluginSlotState* target = findPlugin(pl.track_index, pl.plugin_index);
                PluginSlotState slot;
                if (target !is null) {
                    slot = *target;
                }
                slot.pluginIndex = pl.plugin_index;
                if (pl.plugin_name.length > 0) slot.name = pl.plugin_name.idup;
                else if (slot.name.length == 0) slot.name = "Plugin";
                slot.isInstrument = pl.is_instrument;
                slot.enabled = !pl.bypassed;
                if (pl.mix.hasValue) slot.mix = pl.mix.value;
                if (slot.presets.length == 0) {
                    slot.presets = ["Default", "Warm Analog", "Punchy Club", "Wide Stereo", "Init"];
                }
                slot.params.length = 0;
                foreach (pm; pl.params) {
                    ParamItemState item;
                    item.id = pm.id;
                    item.name = pm.name.idup;
                    item.defaultValue = pm.default_value;
                    item.currentValue = pm.default_value;
                    slot.params ~= item;
                }
                if (pl.track_index >= 100) {
                    const int chIdx = pl.track_index - 100;
                    if (chIdx >= 0 && chIdx < cast(int)channels.length) {
                        channels[chIdx].generatorPlugin = slot;
                    }
                } else if (target !is null) {
                    *target = slot;
                    selectedTrack = pl.track_index;
                    selectedPluginIndex = pl.plugin_index;
                } else {
                    const idx = ensureTrackIndex(pl.track_index);
                    tracks[idx].plugins ~= slot;
                    selectedTrack = pl.track_index;
                    selectedPluginIndex = pl.plugin_index;
                }
            }
        }
        if (notif.param_value_change.hasValue) {
            const pvc = notif.param_value_change.value;
            if (pvc.track_index >= 100) {
                const int chIdx = pvc.track_index - 100;
                if (chIdx >= 0 && chIdx < cast(int)channels.length) {
                    foreach (ref pm; channels[chIdx].generatorPlugin.params) {
                        if (pm.id == pvc.param_id) {
                            pm.currentValue = pvc.value;
                            break;
                        }
                    }
                }
            } else if (auto plug = findPlugin(pvc.track_index, pvc.plugin_index)) {
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
        if (notif.sequencer_sync.hasValue) {
            const ss = notif.sequencer_sync.value;
            if (ss.playback_mode != PbPlaybackMode.PLAYBACK_MODE_UNSPECIFIED) {
                playbackMode = (ss.playback_mode == PbPlaybackMode.PLAYBACK_MODE_PAT)
                    ? PlaybackMode.PAT : PlaybackMode.SONG;
            }
            swingAmount = ss.swing_amount;

            ChannelState[] prevChannels = channels;
            channels.length = 0;
            foreach (sc; ss.channels) {
                ChannelState ch;
                ch.initDefaults();
                ch.index = cast(int)sc.channel_index;
                ch.name = sc.name.idup;
                ch.uri = sc.uri.idup;
                ch.category = sc.category.length > 0 ? sc.category.idup : "Synths";
                ch.color = sc.color != 0 ? sc.color : palette().clipPattern;
                ch.mixerInsert = sc.mixer_insert >= 0 ? sc.mixer_insert : 1;
                ch.volume = sc.volume;
                ch.pan = sc.pan;
                ch.muted = sc.muted;
                ch.soloed = sc.soloed;
                if (ch.index >= 0 && ch.index < cast(int)prevChannels.length &&
                    prevChannels[ch.index].uri == ch.uri &&
                    prevChannels[ch.index].generatorPlugin.params.length > 0) {
                    ch.generatorPlugin = prevChannels[ch.index].generatorPlugin;
                } else {
                    ch.generatorPlugin = makeDefaultPluginSlot(ch.name, ch.uri, true, 0);
                }
                channels ~= ch;
            }

            patterns.length = 0;
            foreach (sp; ss.patterns) {
                PatternState pat;
                pat.index = cast(int)sp.pattern_index;
                pat.name = sp.name.length > 0 ? sp.name.idup : format("Pattern %d", pat.index + 1);
                pat.color = sp.color != 0 ? sp.color : palette().clipPattern;
                pat.lengthSteps = sp.length_steps > 0 ? sp.length_steps : 16;
                foreach (cp; sp.channels) {
                    ChannelPatternSteps cpe;
                    cpe.channelIndex = cast(int)cp.channel_index;
                    foreach (i; 0 .. 64) {
                        cpe.steps[i] = (i < cp.steps.length) ? cp.steps[i] : false;
                        cpe.stepVelocities[i] = (i < cp.step_velocities.length) ? cp.step_velocities[i] : 0.78f;
                        cpe.stepPitches[i] = (i < cp.step_pitches.length) ? cp.step_pitches[i] : 0.0f;
                        cpe.stepPans[i] = (i < cp.step_pans.length) ? cp.step_pans[i] : 0.0f;
                        cpe.stepShifts[i] = (i < cp.step_shifts.length) ? cp.step_shifts[i] : 0.0f;
                    }
                    cpe.notes = cp.notes.dup;
                    pat.channelSteps ~= cpe;
                }
                patterns ~= pat;
            }
            if (patterns.length > 0) {
                int sel = ss.selected_pattern_index;
                if (sel < 0 || sel >= cast(int)patterns.length) sel = 0;
                selectedPatternIndex = sel;
                activePickerKind = PlaylistClipKind.PATTERN;
                activePickerIndex = sel;
                loadPatternFromStore(sel);
            } else {
                selectedPatternIndex = 0;
            }
            if (channels.length > 0) {
                if (selectedChannelIndex < 0 || selectedChannelIndex >= cast(int)channels.length) {
                    selectedChannelIndex = 0;
                }
                if (activeMidi.channelIndex >= cast(int)channels.length) {
                    activeMidi.channelIndex = selectedChannelIndex;
                }
                if (activeMidi.channelIndex >= 0) {
                    activeMidi.channelIndex = resolvePreferredChannelForLoadedPattern(activeMidi.channelIndex);
                    selectedChannelIndex = activeMidi.channelIndex;
                    activeMidi.events = channels[activeMidi.channelIndex].notes.dup;
                }
            }
        }
        if (notif.plugin_spectrum.hasValue) {
            const ps = notif.plugin_spectrum.value;
            if (auto plug = findPlugin(ps.track_index, ps.plugin_index)) {
                plug.specInDb = ps.input_magnitudes.dup;
                plug.specOutDb = ps.output_magnitudes.dup;
            }
        }
        if (notif.plugin_metering.hasValue) {
            const pm = notif.plugin_metering.value;
            if (auto plug = findPlugin(pm.track_index, pm.plugin_index)) {
                plug.meterInDb = pm.input_db;
                plug.meterOutDb = pm.output_db;
                plug.meterGrDb = pm.gain_reduction_db;
                plug.meterLowGrDb = pm.low_gr_db;
                plug.meterMidGrDb = pm.mid_gr_db;
                plug.meterHighGrDb = pm.high_gr_db;
                plug.meterLowInDb = pm.low_in_db;
                plug.meterMidInDb = pm.mid_in_db;
                plug.meterHighInDb = pm.high_in_db;
                plug.hasMetering = true;
            }
        }
        if (notif.plugin_scope_data.hasValue) {
            const sc = notif.plugin_scope_data.value;
            if (auto plug = findPlugin(sc.track_index, sc.plugin_index)) {
                plug.scopeL = sc.left_samples.dup;
                plug.scopeR = sc.right_samples.dup;
            }
        }
        if (notif.plugin_sample_data.hasValue) {
            const sd = notif.plugin_sample_data.value;
            if (auto plug = findPlugin(sd.track_index, sd.plugin_index)) {
                plug.sampleWaveform = sd.waveform.dup;
                plug.sampleName = sd.sample_name.idup;
                plug.sampleDurationSec = sd.sample_duration_sec;
            }
        }
        if (notif.editor_frame_data.hasValue) {
            const ef = notif.editor_frame_data.value;
            if (auto plug = findPlugin(ef.track_index, ef.plugin_index)) {
                if (ef.width > 0 && ef.height > 0 && ef.image_data.length >= cast(size_t)(ef.width * ef.height * 4)) {
                    plug.editorW = ef.width;
                    plug.editorH = ef.height;
                    plug.editorRgba = ef.image_data.dup;
                    plug.remoteEditorOpen = true;
                    ++plug.editorRevision;
                }
            }
        }
        if (notif.drum_pad.hasValue) {
            const dp = notif.drum_pad.value;
            if (auto plug = findPlugin(cast(int)dp.track_index, cast(int)dp.plugin_index)) {
                const int pidx = cast(int)dp.pad_index;
                ptrdiff_t found = -1;
                foreach (i, const ref p; plug.drumPads) {
                    if (p.padIndex == pidx) { found = cast(ptrdiff_t)i; break; }
                }
                if (found < 0 && pidx >= 0 && pidx < 32) {
                    DrumPadSlotState np;
                    np.padIndex = pidx;
                    np.name = "Pad " ~ to!string(pidx + 1);
                    np.triggerNote = cast(uint)(36 + pidx);
                    plug.drumPads ~= np;
                    found = cast(ptrdiff_t)plug.drumPads.length - 1;
                }
                if (found >= 0) {
                    auto pad = &plug.drumPads[found];
                    if (dp.type == 2) { // TYPE_PAD_TRIGGER
                        pad.activeFlash = true;
                    } else {
                        pad.volume = dp.volume;
                        pad.pan = dp.pan;
                        pad.mute = dp.mute;
                        pad.solo = dp.solo;
                        if (dp.sample_name.length > 0) pad.sampleName = dp.sample_name.idup;
                        if (dp.sample_waveform.length > 0) pad.sampleWaveform = dp.sample_waveform.dup;
                        if (dp.trigger_note.hasValue) pad.triggerNote = dp.trigger_note.value;
                    }
                }
            }
        }
        if (notif.config.hasValue) {
            const cfg = notif.config.value;
            sandboxPlugins = (cast(int)cfg.plugin_host_mode == 1);
            if (cfg.buffer_latency_ms > 0) bufferLatencyMs = cfg.buffer_latency_ms;
            useDoublePrecision = cfg.use_double_precision;
            remotePluginHosts.length = 0;
            foreach (h; cfg.remote_hosts) remotePluginHosts ~= h.idup;
        }
        if (notif.audio_input_list.hasValue) {
            audioInputs.length = 0;
            foreach (d; notif.audio_input_list.value.devices) {
                audioInputs ~= InputDeviceInfo(d.id.idup, d.name.idup, d.channel_count);
            }
        }
        if (notif.midi_input_list.hasValue) {
            midiInputs.length = 0;
            foreach (d; notif.midi_input_list.value.devices) {
                midiInputs ~= InputDeviceInfo(d.id.idup, d.name.idup, d.port_count);
            }
        }
        if (notif.automation_lanes_data.hasValue) {
            const ald = notif.automation_lanes_data.value;
            if (ald.track_index >= 0 && ald.track_index < 100) {
                const idx = ensureTrackIndex(ald.track_index);
                TimelineClipState[] nonAuto;
                foreach (ref existing; tracks[idx].clips) {
                    if (existing.kind != PlaylistClipKind.AUTOMATION) nonAuto ~= existing;
                }
                const double secPerBeat = 60.0 / (bpm > 0.0 ? bpm : 140.0);
                foreach (ref lane; ald.lanes) {
                    foreach (ci, ref ptc; lane.clips) {
                        TimelineClipState ac;
                        ac.clipIndex = cast(int)ci;
                        ac.laneIndex = lane.lane_index;
                        ac.pluginIndex = lane.plugin_index;
                        ac.paramId = lane.param_id;
                        ac.kind = PlaylistClipKind.AUTOMATION;
                        ac.color = palette().clipAutomation;
                        ac.name = ptc.clip.name.length > 0
                            ? ptc.clip.name.idup
                            : (lane.param_name.length > 0 ? lane.param_name.idup : "Automation");
                        ac.startTimeSec = ptc.start_time_sec;
                        const double durBeats = ptc.duration_beats > 0.0f
                            ? cast(double)ptc.duration_beats
                            : (ptc.clip.duration_beats > 0.0f ? cast(double)ptc.clip.duration_beats : 4.0);
                        ac.durationSec = durBeats * secPerBeat;
                        ac.muted = ptc.muted;
                        foreach (ref pt; ptc.clip.automation_points) {
                            double nt = durBeats > 0.0 ? (cast(double)pt.time_beats / durBeats) : 0.0;
                            if (nt < 0.0) nt = 0.0;
                            else if (nt > 1.0) nt = 1.0;
                            float v = pt.value;
                            if (v < 0.0f) v = 0.0f;
                            else if (v > 1.0f) v = 1.0f;
                            ac.autoPoints ~= AutomationPoint(nt, v);
                        }
                        nonAuto ~= ac;
                    }
                }
                tracks[idx].clips = nonAuto;
            }
        }
        if (notif.bounce_finished.hasValue) {
            const bf = notif.bounce_finished.value;
            if (bf.path.length > 0) lastBouncePath = bf.path.idup;
            statusMessage = bf.success ? ("Bounced WAV: " ~ bf.path.idup) : ("Bounce FAILED: " ~ bf.path.idup);
            hintText = statusMessage;
        }
        if (notif.recording_finished.hasValue) {
            const rf = notif.recording_finished.value;
            isRecording = false;
            statusMessage = format("Recorded clip %d -> %s", rf.clip_index, rf.path);
            hintText = statusMessage;
        }
        if (notif.project_info.hasValue) {
            currentProjectDir = notif.project_info.value.project_dir.idup;
        }
        if (notif.audio_editor_data.hasValue) {
            const aed = notif.audio_editor_data.value;
            if (aed.waveform.length > 0) samplePreview.waveform = aed.waveform.dup;
            if (aed.file_name.length > 0) samplePreview.name = aed.file_name.idup;
            samplePreview.info = format("%d Hz | %.2fs", aed.sample_rate, aed.duration_sec);
        }
        if (notif.clear_project.hasValue) {
            tracks.length = 0;
            channels.length = 0;
            patterns.length = 0;
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
    assert(model.activeMidi.channelIndex == 0 && model.activeMidi.events.length == 0);
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
    model.selectPatternForPianoRoll(1); // Pattern 2 (Acid Bass) -> channel 3 with 8 notes
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

    // 4a. selectPatternForPianoRoll switches from timeline MIDI (channelIndex=-1) and between patterns
    model = TransportModel.init;
    model.initDefaultTemplate();
    model.activeMidi.channelIndex = -1;
    model.activeMidi.events = null;
    model.selectPatternForPianoRoll(2); // Pattern 2 (E-Piano): notes live on channel 4
    assert(model.selectedPatternIndex == 2 && model.activeMidi.channelIndex == 4 &&
           model.activeMidi.events.length == 8,
           "Expected selectPatternForPianoRoll(2) to select E-Piano channel 4 and load its 8 chord notes");
    model.selectPatternForPianoRoll(0); // Pattern 0 (Drums): switches away from Acid Bass/E-Piano to Kick (ch 0)
    assert(model.selectedPatternIndex == 0 && model.activeMidi.channelIndex == 0 &&
           model.activeMidi.events.length == 0 && model.channels[0].steps[0],
           "Expected selectPatternForPianoRoll(0) to switch to Pattern 0 Drum channel without stale Acid Bass notes");
    model.selectPatternForPianoRoll(1); // Pattern 1 (Acid Bass): loads Acid Bass notes cleanly
    assert(model.selectedPatternIndex == 1 && model.activeMidi.channelIndex == 3 &&
           model.activeMidi.events.length == 8,
           "Expected selectPatternForPianoRoll(1) to load Pattern 1 Acid Bass notes");

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

    // 6. Test pattern switching preserves per-pattern steps & notes
    TransportModel patModel;
    patModel.initDefaultTemplate();
    assert(patModel.selectedPatternIndex == 0);
    assert(patModel.channels[0].steps[0] && patModel.channels[1].steps[4]); // Pattern 1 Drums
    patModel.selectPattern(1);
    assert(patModel.selectedPatternIndex == 1);
    assert(!patModel.channels[1].steps[4]); // Pattern 2 (Acid Bass) has no Snare steps
    assert(patModel.channels[0].steps[4]);  // Pattern 2 has 4-on-floor Kick
    patModel.selectPattern(2);
    assert(!patModel.channels[0].steps[0]); // Pattern 3 (E-Piano) has no Kick steps
    patModel.channels[0].steps[1] = true;
    patModel.selectPattern(0);
    assert(!patModel.channels[0].steps[1]);
    patModel.selectPattern(2);
    assert(patModel.channels[0].steps[1]);

    // 7. Test SequencerSync, TimelineClipInfo pattern_index, and generator track >=100 routing
    import pb.notifications : ParamValueChange, SequencerSync, TimelineClipInfo;
    import pb.core : ChannelPatternData, SequencerChannel, SequencerPattern;

    Notification nSeq;
    SequencerSync ss;
    ss.playback_mode = PbPlaybackMode.PLAYBACK_MODE_SONG;
    ss.swing_amount = 0.35f;
    ss.selected_pattern_index = 1;
    SequencerChannel sc0;
    sc0.channel_index = 0;
    sc0.name = "Synced Kick";
    sc0.uri = "builtin://dr8_kick";
    sc0.mixer_insert = 4;
    sc0.volume = 0.72f;
    ss.channels ~= sc0;
    SequencerPattern sp0, sp1;
    sp0.pattern_index = 0;
    sp0.name = "Verse";
    sp0.length_steps = 16;
    sp1.pattern_index = 1;
    sp1.name = "Drop";
    sp1.length_steps = 16;
    ChannelPatternData cpd;
    cpd.channel_index = 0;
    cpd.steps = [true, false, false, false];
    sp1.channels ~= cpd;
    ss.patterns ~= sp0;
    ss.patterns ~= sp1;
    nSeq.sequencer_sync.set(ss);
    model.applyNotification(nSeq);

    assert(model.playbackMode == PlaybackMode.SONG);
    assert(model.swingAmount > 0.34f && model.swingAmount < 0.36f);
    assert(model.selectedPatternIndex == 1);
    assert(model.channels.length == 1 && model.channels[0].name == "Synced Kick");
    assert(model.channels[0].steps[0] && !model.channels[0].steps[1]);

    const size_t trackCountBefore = model.tracks.length;
    Notification nGenLvl;
    TrackLevels genTls;
    TrackLevel genTl;
    genTl.track_index = 100;
    genTl.peak_l = 0.9f;
    genTls.levels ~= genTl;
    nGenLvl.track_levels.set(genTls);
    model.applyNotification(nGenLvl);
    assert(model.tracks.length == trackCountBefore); // must NOT pollute mixer strips

    Notification nGenParam;
    ParamList gpl;
    gpl.track_index = 100;
    gpl.plugin_index = 0;
    gpl.plugin_name = "Synced Kick";
    gpl.is_instrument = true;
    ParamInfo gp0;
    gp0.id = 0;
    gp0.name = "Punch";
    gp0.default_value = 0.5f;
    gpl.params ~= gp0;
    nGenParam.param_list.set(gpl);
    model.applyNotification(nGenParam);
    assert(model.channels[0].generatorPlugin.params.length == 1);

    Notification nGenVal;
    ParamValueChange pvc;
    pvc.track_index = 100;
    pvc.plugin_index = 0;
    pvc.param_id = 0;
    pvc.value = 0.88f;
    nGenVal.param_value_change.set(pvc);
    model.applyNotification(nGenVal);
    assert(model.channels[0].generatorPlugin.params[0].currentValue > 0.87f);

    Notification nPatClip;
    TimelineClipInfo tci;
    tci.track_index = 1;
    tci.clip_index = 9;
    tci.name = "Drop";
    tci.start_time = 2.0f;
    tci.duration = 4.0f;
    tci.pattern_index.set(1);
    nPatClip.timeline_clip_info.set(tci);
    model.applyNotification(nPatClip);
    const size_t t1Idx = model.ensureTrackIndex(1);
    bool foundPat = false;
    foreach (ref cl; model.tracks[t1Idx].clips) {
        if (cl.clipIndex == 9) {
            foundPat = (cl.kind == PlaylistClipKind.PATTERN && cl.patternIndex == 1);
        }
    }
    assert(foundPat);

    // Phase 4: ParamList preserves presets/URI & telemetry notifications populate plugin slots
    assert(model.channels[0].generatorPlugin.presets.length >= 4);
    import pb.notifications : DrumPadNotification, PluginMeteringData, PluginSampleData, PluginScopeData;

    Notification nMet;
    PluginMeteringData pmd;
    pmd.track_index = 1;
    pmd.plugin_index = 0;
    pmd.input_db = -12.0f;
    pmd.output_db = -6.0f;
    pmd.gain_reduction_db = -4.5f;
    nMet.plugin_metering.set(pmd);
    model.applyNotification(nMet);
    assert(model.tracks[1].plugins[0].hasMetering);
    assert(model.tracks[1].plugins[0].meterGrDb < -4.0f);

    Notification nScp;
    PluginScopeData pscp;
    pscp.track_index = 4;
    pscp.plugin_index = 1;
    pscp.left_samples = [0.2f, -0.3f];
    pscp.right_samples = [-0.2f, 0.3f];
    nScp.plugin_scope_data.set(pscp);
    model.applyNotification(nScp);
    assert(model.tracks[4].plugins[1].scopeL.length == 2);

    Notification nSmp;
    PluginSampleData psd;
    psd.track_index = 100;
    psd.plugin_index = 0;
    psd.sample_name = "kick_punch.wav";
    psd.waveform = [0.8f, 0.4f, 0.1f];
    psd.sample_duration_sec = 0.75;
    nSmp.plugin_sample_data.set(psd);
    model.applyNotification(nSmp);
    assert(model.channels[0].generatorPlugin.sampleName == "kick_punch.wav");
    assert(model.channels[0].generatorPlugin.sampleWaveform.length == 3);

    Notification nPad;
    DrumPadNotification dpn;
    dpn.type = DrumPadNotification.Type.TYPE_PAD_STATE;
    dpn.track_index = 100;
    dpn.plugin_index = 0;
    dpn.pad_index = 1;
    dpn.sample_name = "snare.wav";
    dpn.trigger_note.set(38u);
    nPad.drum_pad.set(dpn);
    model.applyNotification(nPad);
    assert(model.channels[0].generatorPlugin.drumPads.length == 1);
    assert(model.channels[0].generatorPlugin.drumPads[0].triggerNote == 38u);

    // Phase 5: Config, Audio/MIDI Input Lists, BounceFinished, RecordingFinished, AudioEditorData
    import pb.commands : HibikiConfig, PluginHostMode;
    import pb.notifications : AudioEditorData, AudioInputDevice, AudioInputList, BounceFinished,
        MidiInputDevice, MidiInputList, ProjectInfo, RecordingFinished;

    Notification nCfg;
    HibikiConfig cfg;
    cfg.plugin_host_mode = PluginHostMode.PLUGIN_HOST_LOCAL_SANDBOX;
    cfg.buffer_latency_ms = 20;
    cfg.use_double_precision = true;
    nCfg.config.set(cfg);
    model.applyNotification(nCfg);
    assert(model.sandboxPlugins && model.bufferLatencyMs == 20 && model.useDoublePrecision);

    Notification nAIn;
    AudioInputList ail;
    ail.devices ~= AudioInputDevice("dev0", "Built-in Mic", 2);
    nAIn.audio_input_list.set(ail);
    model.applyNotification(nAIn);
    assert(model.audioInputs.length == 1 && model.audioInputs[0].name == "Built-in Mic");

    Notification nBnc;
    BounceFinished bf;
    bf.path = "build/master.wav";
    bf.success = true;
    nBnc.bounce_finished.set(bf);
    model.applyNotification(nBnc);
    assert(model.lastBouncePath == "build/master.wav");

    Notification nAe;
    AudioEditorData aed;
    aed.file_name = "loop140.wav";
    aed.sample_rate = 44100;
    aed.duration_sec = 3.43f;
    aed.waveform = [0.2f, -0.5f, 0.9f];
    nAe.audio_editor_data.set(aed);
    model.applyNotification(nAe);
    assert(model.samplePreview.name == "loop140.wav" && model.samplePreview.waveform.length == 3);

    // Regression: mixer_insert == 0 (Master) must be preserved in SequencerSync & TrackInfo
    SequencerSync ssMaster = ss;
    ssMaster.channels[0].mixer_insert = 0;
    Notification nSeqMaster;
    nSeqMaster.sequencer_sync.set(ssMaster);
    model.applyNotification(nSeqMaster);
    assert(model.channels[0].mixerInsert == 0,
           "Expected SequencerSync mixer_insert == 0 (Master) to be preserved, not coerced to 1");

    import pb.notifications : AutomationLaneInfo, AutomationLanesData, TrackInfo;
    import pb.core : PbAutoClip = Clip, PbAutoPt = AutomationPoint, PbTimelineClip = TimelineClip;
    TrackInfo tiGen;
    tiGen.track_index = 100;
    tiGen.output_track_index = 0;
    Notification nTiGen;
    nTiGen.track_info.set(tiGen);
    model.channels[0].mixerInsert = 3;
    model.applyNotification(nTiGen);
    assert(model.channels[0].mixerInsert == 0,
           "Expected TrackInfo output_track_index == 0 on generator track to route channel to Master (0)");

    // Regression: Empty ParamList (sent by ACTION_REMOVE) must remove the slot instead of creating a zombie 'Plugin'
    model.ensureTrackIndex(2);
    model.tracks[model.ensureTrackIndex(2)].plugins = [
        TransportModel.makeDefaultPluginSlot("EQ", "builtin://eq", false, 0),
        TransportModel.makeDefaultPluginSlot("Delay", "builtin://delay", false, 1),
    ];
    Notification nRemPlug;
    ParamList plEmpty;
    plEmpty.track_index = 2;
    plEmpty.plugin_index = 0;
    plEmpty.plugin_name = "";
    nRemPlug.param_list.set(plEmpty);
    model.applyNotification(nRemPlug);
    assert(model.findPlugin(2, 0) is null || model.findPlugin(2, 0).name != "Plugin",
           "Expected empty ParamList from ACTION_REMOVE not to leave a zombie 'Plugin' slot");

    // Regression: AutomationLanesData notification restores Playlist AUTOMATION clips after ClearProject
    Notification nClear;
    nClear.clear_project.set(ClearProject.init);
    model.applyNotification(nClear);
    AutomationLanesData ald;
    ald.track_index = 4;
    AutomationLaneInfo ali;
    ali.track_index = 4;
    ali.lane_index = 0;
    ali.param_name = "Acid Cutoff Env";
    PbTimelineClip ptc;
    ptc.start_time_sec = 0.0f;
    PbAutoClip pac;
    pac.name = "Acid Cutoff Env";
    pac.duration_beats = 8.0f;
    pac.automation_points ~= PbAutoPt(0.0f, 0.2f, 0.0f);
    pac.automation_points ~= PbAutoPt(8.0f, 0.9f, 0.0f);
    ptc.clip = pac;
    ali.clips ~= ptc;
    ald.lanes ~= ali;
    Notification nAld;
    nAld.automation_lanes_data.set(ald);
    model.applyNotification(nAld);
    const size_t t4 = model.ensureTrackIndex(4);
    assert(model.tracks[t4].clips.length == 1 &&
           model.tracks[t4].clips[0].kind == PlaylistClipKind.AUTOMATION &&
           model.tracks[t4].clips[0].autoPoints.length == 2,
           "Expected AutomationLanesData to restore Playlist automation clips after ClearProject");

    // Regression: TimelineClipInfo with is_midi=true and non-empty preview waveform must resolve to MIDI (not AUDIO)
    Notification nMidiTci;
    TimelineClipInfo tciMidi;
    tciMidi.track_index = 2;
    tciMidi.clip_index = 0;
    tciMidi.name = "rec_take.mid";
    tciMidi.waveform = [0.2f, 0.5f, 0.8f];
    tciMidi.is_midi = true;
    nMidiTci.timeline_clip_info.set(tciMidi);
    model.applyNotification(nMidiTci);
    const size_t t2Midi = model.ensureTrackIndex(2);
    assert(model.tracks[t2Midi].clips[0].kind == PlaylistClipKind.MIDI,
           "Expected is_midi=true TimelineClipInfo with preview waveform to classify as MIDI");

    // Regression: collectAutomationTargets surfaces real plugin params on target track
    PluginSlotState eqPlug;
    eqPlug.pluginIndex = 1;
    eqPlug.name = "Parametric EQ";
    eqPlug.params = [ParamItemState(2, "High Gain", 0.5f, 0.5f)];
    model.tracks[t2Midi].plugins ~= eqPlug;
    const auto autoTargets = model.collectAutomationTargets(2);
    assert(autoTargets.length >= 1 && autoTargets[0].pluginIndex == 1 && autoTargets[0].paramId == 2,
           "Expected collectAutomationTargets to list track plugin parameters dynamically");
}
