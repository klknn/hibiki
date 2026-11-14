module gui_d.transport_model;

import pb.core : MidiEvent, TransportState;
import pb.notifications : Notification, PluginDescription;
import std.conv : to;

/// FL Studio Pattern vs Song playback mode.
enum PlaybackMode {
    PAT,
    SONG,
}

/// Global snap resolution for Playlist and Piano Roll.
enum SnapMode {
    LINE,
    CELL,
    HALF_BEAT,
    QUARTER_BEAT,
    NONE,
}

/// Playlist clip type matching FL Studio's Pattern / Audio / Automation clips.
enum PlaylistClipKind {
    PATTERN,
    AUDIO,
    AUTOMATION,
}

/// Timeline / Playlist clip state for a track.
struct TimelineClipState {
    int clipIndex = 0;
    int patternIndex = 0;
    PlaylistClipKind kind = PlaylistClipKind.PATTERN;
    string name = "Pattern 1";
    double startTimeSec = 0.0;
    double durationSec = 4.0;
    float[] waveform;
}

/// Plugin parameter metadata & current normalized value.
struct ParamItemState {
    uint id = 0;
    string name;
    float defaultValue = 0.0f;
    float currentValue = 0.0f;
}

/// Loaded plugin slot on a track or mixer insert.
struct PluginSlotState {
    int pluginIndex = 0;
    string name = "Plugin";
    string uri;
    bool enabled = true;
    float mix = 1.0f;
    bool isInstrument = false;
    ParamItemState[] params;
}

/// Channel Rack generator/sampler row with 16-step sequencer and mixer routing.
struct ChannelState {
    int index = 0;
    string name = "Channel";
    string uri = "builtin://3xosc";
    uint color = 0x003D5A73;
    float volume = 0.78f;
    float pan = 0.0f;
    bool muted = false;
    int mixerInsert = 1; // 0 = Master, 1..N = Insert
    bool[16] steps;
    MidiEvent[] notes;
}

/// FL Studio Pattern containing step sequences & notes per channel.
struct PatternState {
    int index = 0;
    string name = "Pattern 1";
    uint color = 0x003D5A73;
}

/// Mixer strip & timeline track state for a single track/insert.
struct TrackStripState {
    int index = 0;
    string name = "Track";
    float volume = 0.8f;
    float pan = 0.0f;
    bool muted = false;
    bool soloed = false;
    float peakL = 0.0f;
    float peakR = 0.0f;
    TimelineClipState[] clips;
    PluginSlotState[] plugins;
}

/// Active MIDI clip state shown in the Piano Roll.
struct ActiveMidiClip {
    int trackIndex = 0;
    int clipIndex = 0;
    int channelIndex = 0;
    int resolution = 480;
    MidiEvent[] events;
}

/// UI-side FL Studio 2026 DAW state model synchronized with `hbk-play` notifications.
struct TransportModel {
    double playheadPosSec = 0.0;
    double bpm = 140.0;
    bool isPlaying = false;
    bool isRecording = false;
    bool loopEnabled = false;
    double loopStartSec = 0.0;
    double loopEndSec = 8.0;
    PlaybackMode playbackMode = PlaybackMode.PAT;
    SnapMode snapMode = SnapMode.QUARTER_BEAT;
    float masterVolume = 0.82f;
    float masterPitch = 0.0f;
    float swingAmount = 0.0f;

    string hintText = "Ready — Hover any control for tips | F5 Playlist, F6 Channel Rack, F7 Piano Roll, F9 Mixer";
    string statusMessage = "Ready (FL Studio 2026 dlangui)";

    int selectedTrack = 0;
    int selectedClipIndex = 0;
    int selectedPluginIndex = 0;
    int selectedPatternIndex = 0;
    int selectedChannelIndex = 0;
    int selectedMixerInsert = 1;

    ChannelState[] channels;
    PatternState[] patterns;
    TrackStripState[] tracks;
    ActiveMidiClip activeMidi;
    PluginDescription[] scannedPlugins;

    /// Populates a realistic FL Studio default template (4 channels, 4 patterns, 8 tracks).
    void initDefaultTemplate() @safe {
        if (patterns.length == 0) {
            patterns = [
                PatternState(0, "Pattern 1", 0x003D5A73),
                PatternState(1, "Pattern 2", 0x002D5F5D),
                PatternState(2, "Pattern 3", 0x005B4570),
                PatternState(3, "Pattern 4", 0x006A4B33),
            ];
        }
        if (channels.length == 0) {
            ChannelState kick;
            kick.index = 0;
            kick.name = "DR-8 Kick";
            kick.uri = "builtin://dr8_kick";
            kick.color = 0x00465E73;
            kick.mixerInsert = 1;
            kick.steps = [true, false, false, false, true, false, false, false,
                          true, false, false, false, true, false, false, false];

            ChannelState snare;
            snare.index = 1;
            snare.name = "DR-8 Snare";
            snare.uri = "builtin://dr8_snare";
            snare.color = 0x005C496B;
            snare.mixerInsert = 2;
            snare.steps = [false, false, false, false, true, false, false, false,
                           false, false, false, false, true, false, false, false];

            ChannelState hat;
            hat.index = 2;
            hat.name = "DR-8 Hat";
            hat.uri = "builtin://dr8_hat";
            hat.color = 0x00385E58;
            hat.mixerInsert = 3;
            hat.steps = [true, false, true, false, true, false, true, false,
                         true, false, true, false, true, false, true, false];

            ChannelState bass;
            bass.index = 3;
            bass.name = "Acid Bass";
            bass.uri = "builtin://acid_bass";
            bass.color = 0x006B5038;
            bass.mixerInsert = 4;

            channels = [kick, snare, hat, bass];
        }
        if (tracks.length == 0) {
            foreach (i; 0 .. 8) {
                ensureTrackIndex(i);
            }
            TimelineClipState c0;
            c0.clipIndex = 0;
            c0.patternIndex = 0;
            c0.kind = PlaylistClipKind.PATTERN;
            c0.name = "Pattern 1";
            c0.startTimeSec = 0.0;
            c0.durationSec = 4.0;
            tracks[0].clips ~= c0;
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
        PlayheadInfo, TimelineClipInfo, TrackInfo, TrackLevel, TrackLevels;

    TransportModel model;
    model.initDefaultTemplate();
    assert(model.channels.length == 4);
    assert(model.patterns.length == 4);
    assert(model.tracks.length == 8);

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
    pl.plugin_index = 0;
    pl.plugin_name = "Acid Bass";
    pl.is_instrument = true;
    ParamInfo p0;
    p0.id = 0;
    p0.name = "Cutoff";
    p0.default_value = 0.5f;
    pl.params ~= p0;
    nPlug.param_list.set(pl);
    model.applyNotification(nPlug);
    assert(model.tracks[0].plugins.length == 1);

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
}
