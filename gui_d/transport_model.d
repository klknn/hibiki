module gui_d.transport_model;

import pb.core : TransportState;
import pb.notifications : Notification;

/// Timeline arrangement clip state for a track.
struct TimelineClipState {
    int clipIndex = 0;
    string name = "Clip";
    double startTimeSec = 0.0;
    double durationSec = 4.0;
    float[] waveform;
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
    TrackStripState[] tracks;

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

    /// Applies an engine `Notification` to update transport, mixer, and timeline state.
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
            if (ti.name.length > 0) tracks[idx].name = ti.name;
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
        if (notif.clear_project.hasValue) {
            tracks.length = 0;
            playheadPosSec = 0.0;
            isPlaying = false;
            isRecording = false;
        }
        if (notif.log.hasValue && notif.log.value.message.length > 0) {
            statusMessage = notif.log.value.message;
        }
    }
}

unittest {
    import pb.notifications : ClearProject, PlayheadInfo, TrackInfo, TrackLevel, TrackLevels;

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
    assert(model.tracks[0].peakL == 0.75f);
    assert(model.tracks[0].peakR == 0.50f);

    Notification n3;
    TrackInfo ti;
    ti.track_index = 0;
    ti.name = "Bassline";
    n3.track_info.set(ti);
    model.applyNotification(n3);
    assert(model.tracks[0].name == "Bassline");

    import pb.notifications : TimelineClipInfo;
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
    assert(model.tracks[0].clips[0].name == "Sub Bass");
    assert(model.tracks[0].clips[0].waveform.length == 3);

    Notification n4;
    ClearProject cp;
    n4.clear_project.set(cp);
    model.applyNotification(n4);
    assert(model.tracks.length == 0);
    assert(!model.isPlaying);
}
