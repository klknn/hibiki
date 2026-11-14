module gui_d.transport_model;

import pb.core : MidiEvent, TransportState;
import pb.notifications : Notification, PluginDescription;

/// Timeline arrangement clip state for a track.
struct TimelineClipState {
    int clipIndex = 0;
    string name = "Clip";
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

/// Loaded plugin slot on a track.
struct PluginSlotState {
    int pluginIndex = 0;
    string name = "Plugin";
    bool isInstrument = false;
    ParamItemState[] params;
}

/// Mixer strip & timeline track state for a single track.
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
    int resolution = 480;
    MidiEvent[] events;
}

/// UI-side state model synchronized with `hbk-play` notifications.
struct TransportModel {
    double playheadPosSec = 0.0;
    double bpm = 140.0;
    bool isPlaying = false;
    bool isRecording = false;
    bool loopEnabled = false;
    double loopStartSec = 0.0;
    double loopEndSec = 0.0;
    string statusMessage = "Ready (dlangui + hbk-play)";
    int selectedTrack = 0;
    int selectedClipIndex = 0;
    int selectedPluginIndex = 0;
    TrackStripState[] tracks;
    ActiveMidiClip activeMidi;
    PluginDescription[] scannedPlugins;

    /// Ensures a track slot exists for `trackIndex` and returns its array index.
    size_t ensureTrackIndex(int trackIndex) @safe {
        foreach (i, ref t; tracks) {
            if (t.index == trackIndex) return i;
        }
        TrackStripState t;
        t.index = trackIndex;
        import std.conv : to;
        t.name = "Track " ~ to!string(trackIndex + 1);
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
        PlayheadInfo, PluginListResponse, TimelineClipInfo, TrackInfo, TrackLevel, TrackLevels;

    TransportModel model;

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
    assert(model.tracks.length == 1);

    Notification n3;
    TrackInfo ti;
    ti.track_index = 0;
    ti.name = "Bassline";
    n3.track_info.set(ti);
    model.applyNotification(n3);
    assert(model.tracks[0].name == "Bassline");

    Notification n3b;
    TimelineClipInfo tci;
    tci.track_index = 0;
    tci.clip_index = 0;
    tci.name = "Sub Bass";
    tci.start_time = 2.0f;
    tci.duration = 4.0f;
    tci.waveform = [0.2f, 0.8f, 0.5f];
    n3b.timeline_clip_info.set(tci);
    model.applyNotification(n3b);
    assert(model.tracks[0].clips.length == 1);

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
    assert(model.tracks[0].plugins[0].name == "Acid Bass");
    assert(model.tracks[0].plugins[0].params.length == 1);

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
    assert(model.activeMidi.events[0].pitch == 60);

    Notification n4;
    ClearProject cp;
    n4.clear_project.set(cp);
    model.applyNotification(n4);
    assert(model.tracks.length == 0);
    assert(!model.isPlaying);
}
