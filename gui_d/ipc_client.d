module gui_d.ipc_client;

import core.stdc.string : memcpy;
import dproto.proto : decodeProto, encodeProto;
import pb.commands : MidiCmd, PluginCmd, ProjectCmd, Request, SendVirtualMidi,
                       TrackCmd, TransportCmd;
import pb.core : Clip, EntityRef, MidiEvent;
import pb.notifications : Notification;
import std.process : Pipe, ProcessPipes, pipeProcess, Redirect, tryWait, wait;
import std.stdio : stderr;

/// Magic header `0x48424B49` ("HBKI") written by `engine/ipc/ipc.cpp` in little-endian.
enum uint HBKI_MAGIC = 0x48424B49;

/// Maximum single IPC notification payload size (4 MiB).
enum uint MAX_IPC_FRAME_BYTES = 4 * 1024 * 1024;

/// Encodes a Protobuf `Request` with a 4-byte little-endian length prefix for `hbk-play` stdin.
ubyte[] encodeLengthPrefixedRequest(const ref Request req) @safe {
    ubyte[] payload = encodeProto(req);
    const uint len = cast(uint)payload.length;
    auto framed = new ubyte[4 + payload.length];
    framed[0] = cast(ubyte)(len & 0xFF);
    framed[1] = cast(ubyte)((len >> 8) & 0xFF);
    framed[2] = cast(ubyte)((len >> 16) & 0xFF);
    framed[3] = cast(ubyte)((len >> 24) & 0xFF);
    framed[4 .. $] = payload[];
    return framed;
}

/// Extracts one `HBKI`-framed payload from `rxBuf`, advancing `rxBuf` past the consumed frame.
/// Returns `true` if a complete frame was extracted, `false` if more bytes are needed.
bool extractNextFrame(ref ubyte[] rxBuf, out ubyte[] outPayload) @safe {
    while (rxBuf.length >= 8) {
        const uint magic = cast(uint)rxBuf[0] |
                           (cast(uint)rxBuf[1] << 8) |
                           (cast(uint)rxBuf[2] << 16) |
                           (cast(uint)rxBuf[3] << 24);
        if (magic != HBKI_MAGIC) {
            // Resynchronize by scanning forward 1 byte.
            rxBuf = rxBuf[1 .. $];
            continue;
        }
        const uint len = cast(uint)rxBuf[4] |
                         (cast(uint)rxBuf[5] << 8) |
                         (cast(uint)rxBuf[6] << 16) |
                         (cast(uint)rxBuf[7] << 24);
        if (len > MAX_IPC_FRAME_BYTES) {
            rxBuf = rxBuf[4 .. $];
            continue;
        }
        if (rxBuf.length < 8 + len) {
            return false;
        }
        outPayload = rxBuf[8 .. 8 + len].dup;
        rxBuf = rxBuf[8 + len .. $];
        return true;
    }
    return false;
}

/// Helper constructors for common engine commands.
Request makeTransportRequest(TransportCmd.Action action) @safe {
    Request req;
    TransportCmd cmd;
    cmd.action = action;
    req.transport.set(cmd);
    return req;
}

Request makeSetLoopRequest(bool enabled, double startSec, double endSec) @safe {
    Request req;
    TransportCmd cmd;
    cmd.action = TransportCmd.Action.ACTION_SET_LOOP;
    cmd.loop_enabled = enabled;
    cmd.loop_start = startSec;
    cmd.loop_end = endSec;
    req.transport.set(cmd);
    return req;
}

Request makeSeekRequest(double posSec) @safe {
    Request req;
    TransportCmd cmd;
    cmd.action = TransportCmd.Action.ACTION_SEEK;
    cmd.seek_pos = cast(float)(posSec < 0.0 ? 0.0 : posSec);
    req.transport.set(cmd);
    return req;
}

Request makeSetBpmRequest(float bpm) @safe {
    Request req;
    ProjectCmd cmd;
    cmd.action = ProjectCmd.Action.ACTION_SET_BPM;
    cmd.bpm = bpm;
    req.project.set(cmd);
    return req;
}

Request makeQuitRequest() @safe {
    Request req;
    ProjectCmd cmd;
    cmd.action = ProjectCmd.Action.ACTION_QUIT;
    req.project.set(cmd);
    return req;
}

Request makeTrackMuteRequest(int trackIdx, bool muted) @safe {
    Request req;
    TrackCmd cmd;
    cmd.action = TrackCmd.Action.ACTION_SET_MUTE;
    EntityRef target;
    target.track_index = trackIdx;
    cmd.target = target;
    cmd.flag = muted;
    req.track.set(cmd);
    return req;
}

Request makeTrackSoloRequest(int trackIdx, bool soloed) @safe {
    Request req;
    TrackCmd cmd;
    cmd.action = TrackCmd.Action.ACTION_SET_SOLO;
    EntityRef target;
    target.track_index = trackIdx;
    cmd.target = target;
    cmd.flag = soloed;
    req.track.set(cmd);
    return req;
}

Request makeAddTrackRequest(int trackIdx) @safe {
    Request req;
    TrackCmd cmd;
    cmd.action = TrackCmd.Action.ACTION_SET_TRACK_TYPE;
    EntityRef target;
    target.track_index = trackIdx;
    cmd.target = target;
    cmd.value = 0.0f;
    req.track.set(cmd);
    return req;
}

Request makeTrackVolumeRequest(int trackIdx, float volume) @safe {
    Request req;
    TrackCmd cmd;
    cmd.action = TrackCmd.Action.ACTION_SET_VOLUME;
    EntityRef target;
    target.track_index = trackIdx;
    cmd.target = target;
    cmd.value = volume;
    req.track.set(cmd);
    return req;
}

Request makeProjectLoadRequest(string path) @safe {
    Request req;
    ProjectCmd cmd;
    cmd.action = ProjectCmd.Action.ACTION_LOAD;
    cmd.path = path;
    req.project.set(cmd);
    return req;
}

Request makeProjectSaveRequest(string path) @safe {
    Request req;
    ProjectCmd cmd;
    cmd.action = ProjectCmd.Action.ACTION_SAVE;
    cmd.path = path;
    req.project.set(cmd);
    return req;
}

Request makeUndoRedoRequest(bool redo) @safe {
    Request req;
    ProjectCmd cmd;
    cmd.action = redo ? ProjectCmd.Action.ACTION_REDO : ProjectCmd.Action.ACTION_UNDO;
    req.project.set(cmd);
    return req;
}

Request makeAddTimelineClipRequest(int trackIdx, string path, float startSec, float durBeats) @safe {
    Request req;
    TrackCmd cmd;
    cmd.action = TrackCmd.Action.ACTION_ADD_TIMELINE_CLIP;
    EntityRef target;
    target.track_index = trackIdx;
    cmd.target = target;
    cmd.value = startSec;
    Clip clip;
    clip.path = path;
    clip.duration_beats = durBeats;
    cmd.clip_data = clip;
    req.track.set(cmd);
    return req;
}

Request makeRemoveTimelineClipRequest(int trackIdx, int clipIdx) @safe {
    Request req;
    TrackCmd cmd;
    cmd.action = TrackCmd.Action.ACTION_REMOVE_TIMELINE_CLIP;
    EntityRef target;
    target.track_index = trackIdx;
    target.timeline_clip.set(cast(uint)clipIdx);
    cmd.target = target;
    req.track.set(cmd);
    return req;
}

Request makeMidiGetRequest(int trackIdx, int clipIdx) @safe {
    Request req;
    MidiCmd cmd;
    cmd.action = MidiCmd.Action.ACTION_GET;
    EntityRef target;
    target.track_index = trackIdx;
    target.timeline_clip.set(cast(uint)clipIdx);
    cmd.target = target;
    req.midi.set(cmd);
    return req;
}

Request makeMidiUpdateRequest(int trackIdx, int clipIdx, uint resolution, const(MidiEvent)[] events) @safe {
    Request req;
    MidiCmd cmd;
    cmd.action = MidiCmd.Action.ACTION_UPDATE;
    EntityRef target;
    target.track_index = trackIdx;
    target.timeline_clip.set(cast(uint)clipIdx);
    cmd.target = target;
    cmd.resolution = (resolution > 0) ? resolution : 480;
    cmd.events = events.dup;
    req.midi.set(cmd);
    return req;
}

Request makePluginLoadRequest(int trackIdx, string path, int subPluginIndex = 0) @safe {
    Request req;
    PluginCmd cmd;
    cmd.action = PluginCmd.Action.ACTION_LOAD;
    EntityRef target;
    target.track_index = trackIdx;
    target.plugin_index.set(cast(uint)subPluginIndex);
    cmd.target = target;
    cmd.path = path;
    req.plugin.set(cmd);
    return req;
}

Request makePluginRemoveRequest(int trackIdx, int pluginIdx) @safe {
    Request req;
    PluginCmd cmd;
    cmd.action = PluginCmd.Action.ACTION_REMOVE;
    EntityRef target;
    target.track_index = trackIdx;
    target.plugin_index.set(cast(uint)pluginIdx);
    cmd.target = target;
    req.plugin.set(cmd);
    return req;
}

Request makePluginShowGuiRequest(int trackIdx, int pluginIdx) @safe {
    Request req;
    PluginCmd cmd;
    cmd.action = PluginCmd.Action.ACTION_SHOW_GUI;
    EntityRef target;
    target.track_index = trackIdx;
    target.plugin_index.set(cast(uint)pluginIdx);
    cmd.target = target;
    req.plugin.set(cmd);
    return req;
}

Request makePluginSetParamRequest(int trackIdx, int pluginIdx, uint paramId, float value) @safe {
    Request req;
    PluginCmd cmd;
    cmd.action = PluginCmd.Action.ACTION_SET_PARAM;
    EntityRef target;
    target.track_index = trackIdx;
    target.plugin_index.set(cast(uint)pluginIdx);
    cmd.target = target;
    cmd.param_id = paramId;
    cmd.param_value = value;
    req.plugin.set(cmd);
    return req;
}

Request makePluginListRequest(string searchPath) @safe {
    Request req;
    PluginCmd cmd;
    cmd.action = PluginCmd.Action.ACTION_LIST;
    cmd.path = searchPath;
    req.plugin.set(cmd);
    return req;
}

Request makeSendVirtualMidiRequest(int trackIdx, uint note, uint velocity, bool noteOn) @safe {
    Request req;
    SendVirtualMidi cmd;
    cmd.track_index = trackIdx;
    cmd.note = note;
    cmd.velocity = velocity;
    cmd.note_on = noteOn;
    req.send_virtual_midi.set(cmd);
    return req;
}

/// Manages the `hbk-play` child process and its binary Protobuf IPC pipes.
class EngineProcessClient {
    import core.atomic : atomicLoad, atomicStore;
    import core.sync.mutex : Mutex;
    import core.thread : Thread;

    private ProcessPipes pipes_;
    private Mutex writeMutex_;
    private Thread readerThread_;
    private shared bool running_ = false;
    private void delegate(Notification) onNotification_;

    this(void delegate(Notification) onNotification) {
        writeMutex_ = new Mutex();
        onNotification_ = onNotification;
    }

    bool isRunning() const {
        return atomicLoad(running_);
    }

    bool start(string engineBinaryPath) {
        try {
            pipes_ = pipeProcess([engineBinaryPath], Redirect.stdin | Redirect.stdout);
        } catch (Exception e) {
            stderr.writeln("Failed to start engine ", engineBinaryPath, ": ", e.msg);
            return false;
        }
        atomicStore(running_, true);
        readerThread_ = new Thread(&readLoop);
        readerThread_.isDaemon = true;
        readerThread_.start();
        return true;
    }

    void sendRequest(Request req) {
        if (!atomicLoad(running_)) return;
        ubyte[] framed = encodeLengthPrefixedRequest(req);
        synchronized (writeMutex_) {
            if (!atomicLoad(running_)) return;
            try {
                pipes_.stdin.rawWrite(framed);
                pipes_.stdin.flush();
            } catch (Exception) {
                atomicStore(running_, false);
            }
        }
    }

    void stop() {
        if (!atomicLoad(running_)) return;
        sendRequest(makeQuitRequest());
        atomicStore(running_, false);
        try {
            pipes_.stdin.close();
        } catch (Exception) {}
        try {
            wait(pipes_.pid);
        } catch (Exception) {}
    }

    private void readLoop() {
        ubyte[4096] chunk;
        ubyte[] rxBuf;
        while (atomicLoad(running_)) {
            ubyte[] readSlice;
            try {
                readSlice = pipes_.stdout.rawRead(chunk[]);
            } catch (Exception) {
                break;
            }
            if (readSlice.length == 0) break;
            rxBuf ~= readSlice;

            ubyte[] payload;
            while (atomicLoad(running_) && extractNextFrame(rxBuf, payload)) {
                Notification notif;
                if (decodeProto(payload, notif) && onNotification_ !is null && atomicLoad(running_)) {
                    onNotification_(notif);
                }
            }
        }
        atomicStore(running_, false);
    }
}

unittest {
    // 1. Test length-prefixed request encoding
    auto req = makeTransportRequest(TransportCmd.Action.ACTION_PLAY);
    ubyte[] framed = encodeLengthPrefixedRequest(req);
    assert(framed.length > 4);
    uint len = cast(uint)framed[0] |
               (cast(uint)framed[1] << 8) |
               (cast(uint)framed[2] << 16) |
               (cast(uint)framed[3] << 24);
    assert(len == framed.length - 4);
    Request decoded;
    assert(decodeProto(framed[4 .. $], decoded));
    assert(decoded.transport.hasValue);
    assert(decoded.transport.value.action == TransportCmd.Action.ACTION_PLAY);

    // 2. Test HBKI frame extraction across fragmented chunks and garbage prefix
    import pb.notifications : PlayheadInfo;

    Notification notif;
    PlayheadInfo pi;
    pi.position_sec = 1.25f;
    pi.bpm = 135.0f;
    notif.playhead_info.set(pi);
    ubyte[] payload = encodeProto(notif);

    ubyte[] stream;
    stream ~= [0x00, 0xFF]; // noise before magic
    stream ~= [
        cast(ubyte)(HBKI_MAGIC & 0xFF),
        cast(ubyte)((HBKI_MAGIC >> 8) & 0xFF),
        cast(ubyte)((HBKI_MAGIC >> 16) & 0xFF),
        cast(ubyte)((HBKI_MAGIC >> 24) & 0xFF),
    ];
    const uint plen = cast(uint)payload.length;
    stream ~= [
        cast(ubyte)(plen & 0xFF),
        cast(ubyte)((plen >> 8) & 0xFF),
        cast(ubyte)((plen >> 16) & 0xFF),
        cast(ubyte)((plen >> 24) & 0xFF),
    ];
    stream ~= payload;

    // Feed partial first half, expect false
    ubyte[] rx = stream[0 .. 8].dup;
    ubyte[] outP;
    assert(!extractNextFrame(rx, outP));
    // Feed remaining bytes, expect true and matching decoded notification
    rx ~= stream[8 .. $];
    assert(extractNextFrame(rx, outP));
    assert(rx.length == 0);
    Notification decodedNotif;
    assert(decodeProto(outP, decodedNotif));
    assert(decodedNotif.playhead_info.hasValue);
    assert(decodedNotif.playhead_info.value.bpm == 135.0f);
}
