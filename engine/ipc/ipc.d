module engine.ipc.ipc;

import dproto.proto : encodeProto;
import pb.core : TransportState;
import pb.notifications : Acknowledge, BounceFinished, ClearProject, ClipInfo, Log,
                         Notification, ParamValueChange, PlayheadInfo,
                         PluginSpectrumData, TrackInfo;

/// Magic header ("HBKI") prefixing framed IPC packets.
enum uint IPC_MAGIC = 0x48424B49;

private void writeLE32(ubyte[] dst, uint value) pure nothrow @nogc @safe {
    dst[0] = cast(ubyte)(value & 0xFF);
    dst[1] = cast(ubyte)((value >> 8) & 0xFF);
    dst[2] = cast(ubyte)((value >> 16) & 0xFF);
    dst[3] = cast(ubyte)((value >> 24) & 0xFF);
}

private string sliceStr(const(char)* ptr, size_t len) pure nothrow @nogc @trusted {
    return (ptr !is null && len > 0) ? cast(string)ptr[0 .. len] : "";
}

private int encodeNotif(const ref Notification notif, ubyte* outBuf, int cap) pure nothrow @nogc @trusted {
    if (outBuf is null || cap <= 0) return -1;
    size_t written = encodeProto(notif, outBuf[0 .. cap]);
    return (written > 0) ? cast(int)written : -1;
}

/// Writes an IPC packet with the HBKI magic header and payload length.
ubyte[] frameIpcPacket(const(ubyte)[] payload) pure @safe {
    ubyte[] packet = new ubyte[8 + payload.length];
    writeLE32(packet[0 .. 4], IPC_MAGIC);
    writeLE32(packet[4 .. 8], cast(uint)payload.length);
    packet[8 .. $] = payload[];
    return packet;
}

/// Creates a Notification with an Acknowledge payload and serializes it.
ubyte[] buildAckNotification(string cmdType, bool success) @safe {
    Notification notif;
    Acknowledge ack;
    ack.command_type = cmdType;
    ack.success = success;
    notif.acknowledge.set(ack);
    return encodeProto(notif);
}

/// Creates a Notification with a PlayheadInfo payload and serializes it.
ubyte[] buildPlayheadNotification(
    float posSec,
    float bpm,
    bool isPlaying,
    bool loopEnabled = false,
    float loopStart = 0.0f,
    float loopEnd = 0.0f
) @safe {
    Notification notif;
    PlayheadInfo phi;
    phi.position_sec = posSec;
    phi.bpm = bpm;
    phi.transport_state = isPlaying
        ? TransportState.TRANSPORT_STATE_PLAYING
        : TransportState.TRANSPORT_STATE_STOPPED;
    phi.loop_enabled = loopEnabled;
    phi.loop_start = loopStart;
    phi.loop_end = loopEnd;
    notif.playhead_info.set(phi);
    return encodeProto(notif);
}

extern (C) {
    /// Encodes an `Acknowledge` notification into `outBuf` (@nogc). Returns bytes written or -1.
    int d_encode_ack(const(char)* cmdType, size_t cmdLen, bool success, ubyte* outBuf, int cap) @nogc nothrow {
        Notification notif;
        Acknowledge ack;
        ack.command_type = sliceStr(cmdType, cmdLen);
        ack.success = success;
        notif.acknowledge.set(ack);
        return encodeNotif(notif, outBuf, cap);
    }

    /// Encodes a `Log` notification into `outBuf` (@nogc). Returns bytes written or -1.
    int d_encode_log(const(char)* msg, size_t msgLen, ubyte* outBuf, int cap) @nogc nothrow {
        Notification notif;
        Log logMsg;
        logMsg.message = sliceStr(msg, msgLen);
        notif.log.set(logMsg);
        return encodeNotif(notif, outBuf, cap);
    }

    /// Encodes a `ClipInfo` notification into `outBuf` (@nogc). Returns bytes written or -1.
    int d_encode_clip_info(
        int trackIdx,
        int slotIdx,
        const(char)* name,
        size_t nameLen,
        const(char)* path,
        size_t pathLen,
        ubyte* outBuf,
        int cap
    ) @nogc nothrow {
        Notification notif;
        ClipInfo ci;
        ci.track_index = trackIdx;
        ci.slot_index = slotIdx;
        ci.name = sliceStr(name, nameLen);
        ci.path = sliceStr(path, pathLen);
        notif.clip_info.set(ci);
        return encodeNotif(notif, outBuf, cap);
    }

    /// Encodes a `ClearProject` notification into `outBuf` (@nogc). Returns bytes written or -1.
    int d_encode_clear_project(ubyte* outBuf, int cap) @nogc nothrow {
        Notification notif;
        ClearProject cp;
        notif.clear_project.set(cp);
        return encodeNotif(notif, outBuf, cap);
    }

    /// Encodes a `PlayheadInfo` notification into `outBuf` (@nogc). Returns bytes written or -1.
    int d_encode_playhead_info(
        float posSec,
        float bpm,
        bool isPlaying,
        bool loopEnabled,
        float loopStart,
        float loopEnd,
        ubyte* outBuf,
        int cap
    ) @nogc nothrow {
        Notification notif;
        PlayheadInfo phi;
        phi.position_sec = posSec;
        phi.bpm = bpm;
        phi.transport_state = isPlaying
            ? TransportState.TRANSPORT_STATE_PLAYING
            : TransportState.TRANSPORT_STATE_STOPPED;
        phi.loop_enabled = loopEnabled;
        phi.loop_start = loopStart;
        phi.loop_end = loopEnd;
        notif.playhead_info.set(phi);
        return encodeNotif(notif, outBuf, cap);
    }

    /// Encodes a `BounceFinished` notification into `outBuf` (@nogc). Returns bytes written or -1.
    int d_encode_bounce_finished(const(char)* path, size_t pathLen, bool success, ubyte* outBuf, int cap) @nogc nothrow {
        Notification notif;
        BounceFinished bf;
        bf.path = sliceStr(path, pathLen);
        bf.success = success;
        notif.bounce_finished.set(bf);
        return encodeNotif(notif, outBuf, cap);
    }

    /// Encodes a `TrackInfo` notification into `outBuf` (@nogc). Returns bytes written or -1.
    int d_encode_track_info(int trackIdx, const(char)* name, size_t nameLen, ubyte* outBuf, int cap) @nogc nothrow {
        Notification notif;
        TrackInfo ti;
        ti.track_index = trackIdx;
        ti.name = sliceStr(name, nameLen);
        notif.track_info.set(ti);
        return encodeNotif(notif, outBuf, cap);
    }

    /// Encodes a `ParamValueChange` notification into `outBuf` (@nogc). Returns bytes written or -1.
    int d_encode_param_value_change(
        int trackIdx,
        int pluginIdx,
        uint paramId,
        float value,
        ubyte* outBuf,
        int cap
    ) @nogc nothrow {
        Notification notif;
        ParamValueChange pvc;
        pvc.track_index = trackIdx;
        pvc.plugin_index = pluginIdx;
        pvc.param_id = paramId;
        pvc.value = value;
        notif.param_value_change.set(pvc);
        return encodeNotif(notif, outBuf, cap);
    }

    /// Encodes a `PluginSpectrumData` notification into `outBuf` (@nogc). Returns bytes written or -1.
    int d_encode_plugin_spectrum(
        int trackIdx,
        int pluginIdx,
        const(float)* inDb,
        const(float)* outDb,
        int numBins,
        ubyte* outBuf,
        int cap
    ) @nogc nothrow {
        Notification notif;
        PluginSpectrumData spec;
        spec.track_index = trackIdx;
        spec.plugin_index = pluginIdx;
        if (inDb !is null && numBins > 0) {
            spec.input_magnitudes = cast(float[])inDb[0 .. numBins];
        }
        if (outDb !is null && numBins > 0) {
            spec.output_magnitudes = cast(float[])outDb[0 .. numBins];
        }
        notif.plugin_spectrum.set(spec);
        return encodeNotif(notif, outBuf, cap);
    }
}

unittest {
    import dproto.proto : decodeProto;

    ubyte[512] buf = void;

    int len = d_encode_clear_project(buf.ptr, buf.length);
    assert(len > 0);
    Notification n1;
    assert(decodeProto(buf[0 .. len], n1));
    assert(n1.clear_project.hasValue);

    const(char)[] cmd = "PLAY";
    len = d_encode_ack(cmd.ptr, cmd.length, true, buf.ptr, buf.length);
    assert(len > 0);
    Notification n2;
    assert(decodeProto(buf[0 .. len], n2));
    assert(n2.acknowledge.hasValue);
    assert(n2.acknowledge.value.command_type == "PLAY");
    assert(n2.acknowledge.value.success == true);

    len = d_encode_param_value_change(2, 1, 7, 0.75f, buf.ptr, buf.length);
    assert(len > 0);
    Notification n3;
    assert(decodeProto(buf[0 .. len], n3));
    assert(n3.param_value_change.hasValue);
    assert(n3.param_value_change.value.track_index == 2);
    assert(n3.param_value_change.value.param_id == 7);
}
