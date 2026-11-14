module engine.commands.dispatcher;

import dproto.proto : decodeProto;
import engine.core.history : HistoryManagerD;
import engine.core.project : ClipD, ProjectStateD, TimelineClipD, TrackD;
import pb.commands : AutomationCmd, MidiCmd, PluginCmd, ProjectCmd, Request,
                     TrackCmd, TransportCmd;

import engine.bridge : hibiki;

/// Safe D command handler for TransportCmd on ProjectStateD.
void handleTransportCmdD(
    const ref TransportCmd cmd,
    ProjectStateD state,
    void delegate(string, bool) @safe ackSender = null
) @safe {
    switch (cmd.action) {
    case TransportCmd.Action.ACTION_PLAY:
        state.isTimelinePlaying = true;
        if (ackSender) ackSender("PLAY", true);
        break;
    case TransportCmd.Action.ACTION_STOP:
        state.isTimelinePlaying = false;
        state.isRecording = false;
        foreach (track; state.tracks) {
            track.stop();
        }
        if (ackSender) ackSender("STOP", true);
        break;
    case TransportCmd.Action.ACTION_RECORD:
        state.recordStartSec = state.playheadPosSec;
        state.isRecording = true;
        state.isTimelinePlaying = true;
        if (ackSender) ackSender("RECORD", true);
        break;
    case TransportCmd.Action.ACTION_SEEK:
        state.playheadPosSec = cmd.seek_pos;
        if (ackSender) ackSender("SEEK", true);
        break;
    case TransportCmd.Action.ACTION_SET_LOOP:
        state.loopEnabled = cmd.loop_enabled;
        state.loopStartSec = cmd.loop_start;
        state.loopEndSec = cmd.loop_end;
        if (ackSender) ackSender("SET_LOOP", true);
        break;
    default:
        break;
    }
}

/// Safe D command handler for ProjectCmd on ProjectStateD.
void handleProjectCmdD(
    const ref ProjectCmd cmd,
    ProjectStateD state,
    ref HistoryManagerD history,
    void delegate(string, bool) @safe ackSender = null
) @safe {
    switch (cmd.action) {
    case ProjectCmd.Action.ACTION_QUIT:
        state.quit = true;
        break;
    case ProjectCmd.Action.ACTION_SET_BPM:
        state.setBpm(cmd.bpm);
        if (ackSender) ackSender("SET_BPM", true);
        break;
    case ProjectCmd.Action.ACTION_UNDO:
        ubyte[] current = state.captureState();
        ubyte[] prev;
        if (history.undo(current, prev)) {
            state.applyState(prev);
            if (ackSender) ackSender("UNDO", true);
        } else {
            if (ackSender) ackSender("UNDO", false);
        }
        break;
    case ProjectCmd.Action.ACTION_REDO:
        ubyte[] current = state.captureState();
        ubyte[] next;
        if (history.redo(current, next)) {
            state.applyState(next);
            if (ackSender) ackSender("REDO", true);
        } else {
            if (ackSender) ackSender("REDO", false);
        }
        break;
    default:
        break;
    }
}

/// Safe D command handler for TrackCmd on ProjectStateD.
void handleTrackCmdD(
    const ref TrackCmd cmd,
    ProjectStateD state,
    ref HistoryManagerD history,
    void delegate(string, bool) @safe ackSender = null
) @safe {
    int tidx = cmd.target.track_index;
    auto t = state.getOrCreateTrack(tidx);
    switch (cmd.action) {
    case TrackCmd.Action.ACTION_SET_VOLUME:
        t.volume = cmd.value;
        if (ackSender) ackSender("SET_VOLUME", true);
        break;
    case TrackCmd.Action.ACTION_SET_PAN:
        t.pan = cmd.value;
        if (ackSender) ackSender("SET_PAN", true);
        break;
    case TrackCmd.Action.ACTION_SET_MUTE:
        t.muted = cmd.flag || (cmd.value > 0.5f);
        if (ackSender) ackSender("SET_MUTE", true);
        break;
    case TrackCmd.Action.ACTION_SET_SOLO:
        t.soloed = cmd.flag || (cmd.value > 0.5f);
        if (ackSender) ackSender("SET_SOLO", true);
        break;
    case TrackCmd.Action.ACTION_ARM_RECORD:
        t.recordArmed = cmd.flag;
        if (ackSender) ackSender("ARM_RECORD", true);
        break;
    case TrackCmd.Action.ACTION_SET_TRACK_TYPE:
        t.trackType = cast(TrackD.TrackType)cast(int)cmd.value;
        if (ackSender) ackSender("SET_TRACK_TYPE", true);
        break;
    default:
        break;
    }
}

/// Safe D command handler for AutomationCmd on ProjectStateD.
void handleAutomationCmdD(
    const ref AutomationCmd cmd,
    ProjectStateD state,
    ref HistoryManagerD history,
    void delegate(string, bool) @safe ackSender = null
) @safe {
    int tidx = cmd.target.track_index;
    auto track = state.getOrCreateTrack(tidx);
    switch (cmd.action) {
    case AutomationCmd.Action.ACTION_ADD_LANE:
        int pidx = cmd.target.plugin_index.hasValue ? cast(int)cmd.target.plugin_index.value : 0;
        track.addAutomationLane(pidx, cmd.param_id);
        if (ackSender) ackSender("ADD_LANE", true);
        break;
    case AutomationCmd.Action.ACTION_REMOVE_LANE:
        size_t lidx = cmd.target.lane_index.hasValue ? cast(size_t)cmd.target.lane_index.value : 0;
        track.removeAutomationLane(lidx);
        if (ackSender) ackSender("REMOVE_LANE", true);
        break;
    case AutomationCmd.Action.ACTION_ADD_CLIP:
        size_t lidx = cmd.target.lane_index.hasValue ? cast(size_t)cmd.target.lane_index.value : 0;
        if (lidx < track.automationLanes.length) {
            auto tc = new TimelineClipD();
            tc.startTimeSec = cmd.start_time_sec;
            tc.durationBeats = cmd.duration_beats;
            tc.clip = new ClipD();
            tc.clip.type = ClipD.Type.AUTOMATION;
            tc.clip.durationBeats = cmd.duration_beats;
            track.automationLanes[lidx].clips ~= tc;
        }
        if (ackSender) ackSender("ADD_CLIP", true);
        break;
    default:
        break;
    }
}

/// Main D request dispatcher operating on ProjectStateD.
bool dispatchRequestD(
    ref const(Request) req,
    ProjectStateD state,
    ref HistoryManagerD history,
    void delegate(string, bool) @safe ackSender = null
) @safe {
    if (req.transport.hasValue) {
        handleTransportCmdD(req.transport.value, state, ackSender);
        return true;
    } else if (req.project.hasValue) {
        handleProjectCmdD(req.project.value, state, history, ackSender);
        return true;
    } else if (req.track.hasValue) {
        handleTrackCmdD(req.track.value, state, history, ackSender);
        return true;
    } else if (req.automation.hasValue) {
        handleAutomationCmdD(req.automation.value, state, history, ackSender);
        return true;
    }
    return false;
}

/// Dispatches an IPC request with D handling for core transport/project commands,
/// falling back to the C++ backend for audio-device and VST3-heavy commands.
bool dispatchCommandWithFallback(
    const(ubyte)[] buffer,
    hibiki.ProjectState* cppState,
    hibiki.HistoryManager* cppHistory
) {
    Request req;
    if (!decodeProto(buffer, req)) {
        return false;
    }

    if (req.transport.hasValue) {
        const t = req.transport.value;
        switch (t.action) {
        case TransportCmd.Action.ACTION_PLAY:
            hibiki.setTimelinePlaying(cppState, true);
            hibiki.sendAck("PLAY", true);
            return true;
        case TransportCmd.Action.ACTION_STOP:
            if (!hibiki.isRecording(cppState)) {
                hibiki.setTimelinePlaying(cppState, false);
                hibiki.stopPlaybackTracks(cppState);
                hibiki.sendAck("STOP", true);
                return true;
            }
            break;
        case TransportCmd.Action.ACTION_SEEK:
            hibiki.setPlayheadPos(cppState, t.seek_pos);
            hibiki.sendAck("SEEK", true);
            return true;
        case TransportCmd.Action.ACTION_SET_LOOP:
            hibiki.setLoop(cppState, t.loop_enabled, t.loop_start, t.loop_end);
            hibiki.sendAck("SET_LOOP", true);
            return true;
        default:
            break;
        }
    } else if (req.project.hasValue) {
        const p = req.project.value;
        if (p.action == ProjectCmd.Action.ACTION_QUIT) {
            hibiki.setStateQuit(cppState, true);
            return true;
        }
    }

    // Fall back to C++ command handler for recording, plugins, bouncing, and complex track operations
    return hibiki.dispatchIpcCommand(buffer.ptr, buffer.length, *cppState, *cppHistory);
}
