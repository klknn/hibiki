module engine.core.project;

import core.math : fabs;
import std.algorithm : sort;
import std.math : isNaN, round;
import dproto.proto : decodeProto, encodeProto;
import engine.core.midi : isNoteOff, isNoteOn, MidiEvent;
import pb.core : AutomationPoint, AuxSend, Clip, ClipType, PbAutomationLane = AutomationLane,
                 PbMidiEvent = MidiEvent, Project, SessionSlot, TimelineClip, Track,
                 TrackType, TransportState;

/// Convert a duration in beats to seconds at the given BPM.
double beatsToSec(double beats, double bpm) pure nothrow @safe {
    if (bpm <= 0.0 || bpm.isNaN) return 0.0;
    return beats * 60.0 / bpm;
}

/// Convert a duration in seconds to beats at the given BPM.
double secToBeats(double sec, double bpm) pure nothrow @safe {
    if (bpm <= 0.0 || bpm.isNaN) return 0.0;
    return sec * bpm / 60.0;
}

/// Tension-based interpolation between two automation points.
/// tension=0 -> linear, >0 -> ease-in (slow start), <0 -> ease-out (fast start)
float interpolateAutomation(float v0, float v1, float t, float tension) pure nothrow @safe @nogc {
    if (t <= 0.0f) return v0;
    if (t >= 1.0f) return v1;
    float exponent = 2.0f ^^ tension;
    float curved_t = t ^^ exponent;
    return v0 + (v1 - v0) * curved_t;
}

class ClipD {
    enum Type { MIDI, AUDIO, AUTOMATION }
    Type type = Type.MIDI;
    string name;
    string path;
    bool isLoop = false;
    int numChannels = 0;
    double sampleRate = 0.0;
    double durationSec = 0.0;
    double durationBeats = 0.0;
    double trimStartBeats = 0.0;
    float[] waveformSummary;
    float[] audioData;
    MidiEvent[] midiEvents;
    AutomationPoint[] automationPoints;

    ClipD dup() const @safe {
        auto c = new ClipD();
        c.type = this.type;
        c.name = this.name;
        c.path = this.path;
        c.isLoop = this.isLoop;
        c.numChannels = this.numChannels;
        c.sampleRate = this.sampleRate;
        c.durationSec = this.durationSec;
        c.durationBeats = this.durationBeats;
        c.trimStartBeats = this.trimStartBeats;
        c.waveformSummary = this.waveformSummary.dup;
        c.audioData = this.audioData.dup;
        c.midiEvents = this.midiEvents.dup;
        c.automationPoints = this.automationPoints.dup;
        return c;
    }
}

class TimelineClipD {
    ClipD clip;
    double startTimeSec = 0.0;
    double durationSec = 0.0;
    double durationBeats = 0.0;
    double trimStartBeats = 0.0;
    int aliasSource = -1;
    double loopIntervalBeats = 0.0;
    double fadeInSec = 0.0;
    double fadeOutSec = 0.0;
    bool muted = false;

    TimelineClipD dup() const @safe {
        auto tc = new TimelineClipD();
        tc.clip = this.clip ? this.clip.dup() : null;
        tc.startTimeSec = this.startTimeSec;
        tc.durationSec = this.durationSec;
        tc.durationBeats = this.durationBeats;
        tc.trimStartBeats = this.trimStartBeats;
        tc.aliasSource = this.aliasSource;
        tc.loopIntervalBeats = this.loopIntervalBeats;
        tc.fadeInSec = this.fadeInSec;
        tc.fadeOutSec = this.fadeOutSec;
        tc.muted = this.muted;
        return tc;
    }
}

struct AutomationLaneD {
    int pluginIdx = 0;
    uint paramId = 0;
    TimelineClipD[] clips;

    AutomationLaneD dup() const @safe {
        AutomationLaneD lane;
        lane.pluginIdx = this.pluginIdx;
        lane.paramId = this.paramId;
        lane.clips = new TimelineClipD[this.clips.length];
        foreach (i, c; this.clips) {
            lane.clips[i] = c ? c.dup() : null;
        }
        return lane;
    }
}

/// Samples a sorted automation point curve at `localBeats` using binary search.
private float sampleAutomationCurve(const AutomationPoint[] points, double localBeats) pure nothrow @safe @nogc {
    if (points.length == 0) return 0.0f;
    if (points.length == 1 || localBeats <= points[0].time_beats) return points[0].value;
    if (localBeats >= points[$ - 1].time_beats) return points[$ - 1].value;

    size_t lo = 0;
    size_t hi = points.length - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (points[mid].time_beats <= localBeats) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const p0 = points[lo];
    const p1 = points[hi];
    float denom = p1.time_beats - p0.time_beats;
    float segmentT = (denom > 0.0f) ? cast(float)((localBeats - p0.time_beats) / denom) : 0.0f;
    return interpolateAutomation(p0.value, p1.value, segmentT, p0.tension);
}

/// Evaluates an automation lane at `timeBeats` (holding last/first value outside clips).
float getAutomationValue(const ref AutomationLaneD lane, double timeBeats, double bpm) pure nothrow @safe {
    if (lane.clips.length == 0) return 0.0f;

    const double beatsPerSec = bpm / 60.0;
    ptrdiff_t prevIdx = -1;
    ptrdiff_t firstIdx = -1;

    foreach (i, tc; lane.clips) {
        if (!tc || !tc.clip || tc.clip.type != ClipD.Type.AUTOMATION) continue;

        if (firstIdx == -1 || tc.startTimeSec < lane.clips[firstIdx].startTimeSec) {
            firstIdx = i;
        }

        double clipStartBeats = tc.startTimeSec * beatsPerSec;
        double clipEndBeats = clipStartBeats + tc.durationBeats;

        if (timeBeats >= clipStartBeats && timeBeats < clipEndBeats) {
            return sampleAutomationCurve(tc.clip.automationPoints, timeBeats - clipStartBeats);
        }
        if (timeBeats >= clipEndBeats) {
            if (prevIdx == -1 || tc.startTimeSec > lane.clips[prevIdx].startTimeSec) {
                prevIdx = i;
            }
        }
    }

    if (prevIdx >= 0) {
        const points = lane.clips[prevIdx].clip.automationPoints;
        return points.length > 0 ? points[$ - 1].value : 0.0f;
    }
    if (firstIdx >= 0) {
        const points = lane.clips[firstIdx].clip.automationPoints;
        return points.length > 0 ? points[0].value : 0.0f;
    }
    return 0.0f;
}

class TrackD {
    enum TrackType { NORMAL = 0, GROUP = 1, AUX = 2 }
    struct AuxSendInfo {
        int auxTrackIndex = -1;
        float level = 0.0f;
        bool preFader = false;
    }

    int index;
    string name;
    TrackType trackType = TrackType.NORMAL;
    int outputTrackIndex = 0;
    int groupParentIndex = -1;
    bool recordArmed = false;
    string inputDeviceId;
    uint inputChannelStart = 0;
    bool inputStereo = false;
    string midiInputDeviceId;
    AuxSendInfo[] auxSends;
    ClipD[int] clips;
    TimelineClipD[] timelineClips;
    AutomationLaneD[] automationLanes;
    float volume = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    bool soloed = false;
    int playingSlot = -1;
    double currentTimeSec = 0.0;

    this(int idx = 0) @safe {
        this.index = idx;
    }

    void stop() @safe {
        playingSlot = -1;
        currentTimeSec = 0.0;
    }

    void addTimelineClip(string path, double startTimeSec, double bpm, double durationBeats = 4.0, double sampleRate = 44100.0) @safe {
        auto clip = new ClipD();
        clip.path = path;
        clip.durationBeats = durationBeats;
        clip.sampleRate = sampleRate;
        clip.durationSec = beatsToSec(durationBeats, bpm);

        auto tc = new TimelineClipD();
        tc.clip = clip;
        tc.startTimeSec = startTimeSec;
        tc.durationBeats = durationBeats;
        tc.durationSec = clip.durationSec;
        timelineClips ~= tc;
    }

    void addAutomationLane(int pluginIdx, uint paramId) @safe {
        AutomationLaneD lane;
        lane.pluginIdx = pluginIdx;
        lane.paramId = paramId;
        automationLanes ~= lane;
    }

    void removeAutomationLane(size_t laneIndex) @safe {
        if (laneIndex < automationLanes.length) {
            automationLanes = automationLanes[0 .. laneIndex] ~ automationLanes[laneIndex + 1 .. $];
        }
    }
}

Clip clipToProto(const ClipD clip) @safe {
    Clip pc;
    if (clip is null) return pc;
    pc.type = (clip.type == ClipD.Type.AUDIO) ? ClipType.CLIP_TYPE_AUDIO : ClipType.CLIP_TYPE_MIDI;
    pc.path = clip.path;
    pc.name = clip.name;
    pc.is_loop = clip.isLoop;
    pc.duration_beats = cast(float)clip.durationBeats;
    pc.duration_sec = cast(float)clip.durationSec;
    pc.trim_start_beats = cast(float)clip.trimStartBeats;
    pc.waveform_summary = clip.waveformSummary.dup;

    if (clip.type == ClipD.Type.MIDI) {
        const double ppq = 480.0;
        pc.resolution = cast(uint)ppq;

        for (size_t i = 0; i < clip.midiEvents.length; ++i) {
            const ev = clip.midiEvents[i];
            if (isNoteOn(ev)) {
                double duration = 0.1;
                for (size_t j = i + 1; j < clip.midiEvents.length; ++j) {
                    const offEv = clip.midiEvents[j];
                    if (offEv.note == ev.note && offEv.channel == ev.channel && isNoteOff(offEv)) {
                        duration = offEv.beats - ev.beats;
                        break;
                    }
                }
                PbMidiEvent pme;
                pme.tick = cast(long)round(ev.beats * ppq);
                pme.pitch = ev.note;
                pme.duration_ticks = cast(long)round(duration * ppq);
                pme.velocity = ev.velocity;
                pc.midi_events ~= pme;
            }
        }
    }

    foreach (pt; clip.automationPoints) {
        pc.automation_points ~= pt;
    }

    return pc;
}

ClipD clipFromProto(const ref Clip pc) @safe {
    auto clip = new ClipD();
    clip.type = (pc.type == ClipType.CLIP_TYPE_AUDIO) ? ClipD.Type.AUDIO : ClipD.Type.MIDI;
    clip.path = pc.path;
    clip.name = pc.name;
    clip.isLoop = pc.is_loop;
    clip.durationBeats = pc.duration_beats;
    clip.durationSec = pc.duration_sec;
    clip.trimStartBeats = pc.trim_start_beats;
    clip.waveformSummary = pc.waveform_summary.dup;

    if (clip.type == ClipD.Type.MIDI && pc.midi_events.length > 0) {
        const double ppq = (pc.resolution > 0.0f) ? cast(double)pc.resolution : 480.0;
        MidiEvent[] events;
        foreach (const ref ev; pc.midi_events) {
            double startBeats = cast(double)ev.tick / ppq;
            double endBeats = cast(double)(ev.tick + ev.duration_ticks) / ppq;

            MidiEvent noteOn;
            noteOn.beats = startBeats;
            noteOn.type = 0x90;
            noteOn.channel = 0;
            noteOn.note = cast(ubyte)ev.pitch;
            noteOn.velocity = cast(ubyte)ev.velocity;
            events ~= noteOn;

            MidiEvent noteOff;
            noteOff.beats = endBeats;
            noteOff.type = 0x80;
            noteOff.channel = 0;
            noteOff.note = cast(ubyte)ev.pitch;
            noteOff.velocity = 0;
            events ~= noteOff;
        }
        events.sort!((a, b) => a.beats < b.beats);
        clip.midiEvents = events;
    }

    foreach (pt; pc.automation_points) {
        clip.automationPoints ~= pt;
    }

    return clip;
}

/// Serializes a `TimelineClipD` to its Protobuf representation.
TimelineClip timelineClipToProto(const TimelineClipD tc) @safe {
    TimelineClip ptc;
    if (tc is null) return ptc;
    ptc.start_time_sec = cast(float)tc.startTimeSec;
    ptc.duration_beats = cast(float)tc.durationBeats;
    ptc.alias_source = tc.aliasSource;
    ptc.loop_interval_beats = cast(float)tc.loopIntervalBeats;
    ptc.fade_in_sec = cast(float)tc.fadeInSec;
    ptc.fade_out_sec = cast(float)tc.fadeOutSec;
    ptc.muted = tc.muted;
    if (tc.clip) {
        ptc.clip = clipToProto(tc.clip);
        ptc.clip.duration_sec = cast(float)tc.durationSec;
        ptc.clip.trim_start_beats = cast(float)tc.trimStartBeats;
    }
    return ptc;
}

/// Deserializes a `TimelineClipD` from its Protobuf representation.
TimelineClipD timelineClipFromProto(const ref TimelineClip ptc) @safe {
    auto tc = new TimelineClipD();
    tc.startTimeSec = ptc.start_time_sec;
    tc.durationBeats = ptc.duration_beats;
    tc.aliasSource = ptc.alias_source;
    tc.loopIntervalBeats = ptc.loop_interval_beats;
    tc.fadeInSec = ptc.fade_in_sec;
    tc.fadeOutSec = ptc.fade_out_sec;
    tc.muted = ptc.muted;
    tc.clip = clipFromProto(ptc.clip);
    tc.durationSec = tc.clip ? tc.clip.durationSec : 0.0;
    tc.trimStartBeats = tc.clip ? tc.clip.trimStartBeats : 0.0;
    return tc;
}

/// Serializes a `TrackD` to its Protobuf representation.
Track trackToProto(int idx, const TrackD track) @safe {
    Track pt;
    pt.track_index = cast(uint)idx;
    pt.name = track.name;
    pt.record_armed = track.recordArmed;
    pt.input_device_id = track.inputDeviceId;
    pt.input_channel_start = track.inputChannelStart;
    pt.input_stereo = track.inputStereo;
    pt.track_type = cast(TrackType)cast(int)track.trackType;
    pt.output_track_index = track.outputTrackIndex;
    pt.group_parent_index = track.groupParentIndex;

    foreach (send; track.auxSends) {
        AuxSend as;
        as.aux_track_index = send.auxTrackIndex;
        as.level = send.level;
        as.pre_fader = send.preFader;
        pt.aux_sends ~= as;
    }

    foreach (slot, clip; track.clips) {
        if (!clip) continue;
        SessionSlot ss;
        ss.slot_index = cast(uint)slot;
        ss.clip = clipToProto(clip);
        pt.session_slots ~= ss;
    }

    foreach (tc; track.timelineClips) {
        if (tc) pt.timeline_clips ~= timelineClipToProto(tc);
    }

    foreach (lane; track.automationLanes) {
        PbAutomationLane al;
        al.plugin_index = lane.pluginIdx;
        al.param_id = lane.paramId;
        foreach (tc; lane.clips) {
            if (!tc) continue;
            TimelineClip altc;
            altc.start_time_sec = cast(float)tc.startTimeSec;
            if (tc.clip) {
                altc.clip = clipToProto(tc.clip);
                altc.clip.type = ClipType.CLIP_TYPE_AUTOMATION;
                altc.clip.duration_beats = cast(float)tc.durationBeats;
            }
            al.clips ~= altc;
        }
        pt.automation_lanes ~= al;
    }
    return pt;
}

/// Populates a `TrackD` from its Protobuf representation.
void trackFromProto(const ref Track pt, TrackD track) @safe {
    track.name = pt.name;
    track.recordArmed = pt.record_armed;
    track.inputDeviceId = pt.input_device_id;
    track.inputChannelStart = pt.input_channel_start;
    track.inputStereo = pt.input_stereo;
    track.trackType = cast(TrackD.TrackType)cast(int)pt.track_type;
    track.outputTrackIndex = pt.output_track_index;
    track.groupParentIndex = pt.group_parent_index;

    track.auxSends = null;
    foreach (const ref send; pt.aux_sends) {
        TrackD.AuxSendInfo s;
        s.auxTrackIndex = send.aux_track_index;
        s.level = send.level;
        s.preFader = send.pre_fader;
        track.auxSends ~= s;
    }

    track.clips.clear();
    foreach (const ref ss; pt.session_slots) {
        track.clips[cast(int)ss.slot_index] = clipFromProto(ss.clip);
    }

    track.timelineClips = null;
    foreach (const ref ptc; pt.timeline_clips) {
        track.timelineClips ~= timelineClipFromProto(ptc);
    }

    track.automationLanes = null;
    foreach (const ref al; pt.automation_lanes) {
        AutomationLaneD lane;
        lane.pluginIdx = cast(int)al.plugin_index;
        lane.paramId = al.param_id;
        foreach (const ref altc; al.clips) {
            auto tc = new TimelineClipD();
            tc.startTimeSec = altc.start_time_sec;
            tc.clip = clipFromProto(altc.clip);
            if (tc.clip) tc.durationBeats = tc.clip.durationBeats;
            lane.clips ~= tc;
        }
        track.automationLanes ~= lane;
    }
}

/// Top-level DAW project state for D command dispatch and undo/redo snapshots.
class ProjectStateD {
    double bpm = 120.0;
    double sampleRate = 44100.0;
    double playheadPosSec = 0.0;
    bool isPlaying = false;
    bool isTimelinePlaying = false;
    bool isRecording = false;
    double recordStartSec = 0.0;
    bool loopEnabled = false;
    double loopStartSec = 0.0;
    double loopEndSec = 0.0;
    bool quit = false;
    string projectDir;
    int bufferLatencyMs = 200;
    bool useDoublePrecision = false;
    float[2] levels = [0.0f, 0.0f];
    TrackD[int] tracks;

    TrackD getOrCreateTrack(int trackIndex) @safe {
        if (trackIndex !in tracks) {
            tracks[trackIndex] = new TrackD(trackIndex);
        }
        return tracks[trackIndex];
    }

    void removeTrack(int trackIndex) @safe {
        tracks.remove(trackIndex);
    }

    void clear() @safe {
        tracks.clear();
        playheadPosSec = 0.0;
        isTimelinePlaying = false;
        isPlaying = false;
        isRecording = false;
        recordStartSec = 0.0;
        loopEnabled = false;
        loopStartSec = 0.0;
        loopEndSec = 0.0;
        projectDir = "";
    }

    void setBpm(double newBpm) @safe {
        if (newBpm <= 0.0 || newBpm.isNaN) return;
        double oldBpm = this.bpm;
        this.bpm = newBpm;
        if (!oldBpm.isNaN && oldBpm > 0.0 && oldBpm != newBpm) {
            double scale = oldBpm / newBpm;
            foreach (tidx, track; tracks) {
                foreach (tc; track.timelineClips) {
                    if (tc) tc.startTimeSec *= scale;
                }
                foreach (ref lane; track.automationLanes) {
                    foreach (tc; lane.clips) {
                        if (tc) tc.startTimeSec *= scale;
                    }
                }
            }
            playheadPosSec *= scale;
            recordStartSec *= scale;
            loopStartSec *= scale;
            loopEndSec *= scale;
        }
    }

    double getDuration() const @safe {
        double maxDuration = 0.0;
        foreach (idx, track; tracks) {
            foreach (tc; track.timelineClips) {
                if (!tc) continue;
                double clipDur = (tc.durationBeats > 0)
                    ? tc.durationBeats * 60.0 / this.bpm
                    : tc.durationSec;
                double endTime = tc.startTimeSec + clipDur;
                if (endTime > maxDuration) {
                    maxDuration = endTime;
                }
            }
        }
        return maxDuration > 0.0 ? maxDuration + 2.0 : 0.0;
    }

    /// Serializes the full project state to a Protobuf `Project` message.
    Project toProto() const @safe {
        Project p;
        p.bpm = cast(float)this.bpm;
        p.playhead_sec = cast(float)this.playheadPosSec;
        p.transport_state = this.isRecording
            ? TransportState.TRANSPORT_STATE_RECORDING
            : (this.isTimelinePlaying
                ? TransportState.TRANSPORT_STATE_PLAYING
                : TransportState.TRANSPORT_STATE_STOPPED);

        foreach (idx, track; tracks) {
            p.tracks ~= trackToProto(idx, track);
        }
        return p;
    }

    /// Restores the full project state from a Protobuf `Project` message.
    void fromProto(const ref Project p) @safe {
        this.bpm = p.bpm;
        this.playheadPosSec = p.playhead_sec;
        this.isTimelinePlaying = (p.transport_state == TransportState.TRANSPORT_STATE_PLAYING);
        this.isRecording = (p.transport_state == TransportState.TRANSPORT_STATE_RECORDING);
        this.tracks.clear();

        foreach (const ref pt; p.tracks) {
            auto track = getOrCreateTrack(cast(int)pt.track_index);
            trackFromProto(pt, track);
        }
    }

    /// Encodes a binary snapshot for undo/redo history.
    ubyte[] captureState() const @safe {
        Project proto = toProto();
        return encodeProto(proto);
    }

    /// Restores project state from a binary undo/redo snapshot.
    bool applyState(const(ubyte)[] data) @safe {
        Project proto;
        if (!decodeProto(data, proto)) {
            return false;
        }
        fromProto(proto);
        return true;
    }
}
