module engine.ipc.ipc;

import dproto.proto : encodeProto;
import pb.core : TransportState;
import pb.notifications : Acknowledge, Notification, PlayheadInfo;

enum uint IPC_MAGIC = 0x48424B49; // "HBKI"

/// Writes an IPC packet with the HBKI magic header and payload length.
ubyte[] frameIpcPacket(const(ubyte)[] payload) pure @safe {
    ubyte[] packet = new ubyte[8 + payload.length];
    // Little-endian IPC_MAGIC
    packet[0] = cast(ubyte)(IPC_MAGIC & 0xFF);
    packet[1] = cast(ubyte)((IPC_MAGIC >> 8) & 0xFF);
    packet[2] = cast(ubyte)((IPC_MAGIC >> 16) & 0xFF);
    packet[3] = cast(ubyte)((IPC_MAGIC >> 24) & 0xFF);
    // Little-endian length
    uint len = cast(uint)payload.length;
    packet[4] = cast(ubyte)(len & 0xFF);
    packet[5] = cast(ubyte)((len >> 8) & 0xFF);
    packet[6] = cast(ubyte)((len >> 16) & 0xFF);
    packet[7] = cast(ubyte)((len >> 24) & 0xFF);
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
    phi.transport_state = isPlaying ? TransportState.TRANSPORT_STATE_PLAYING : TransportState.TRANSPORT_STATE_STOPPED;
    phi.loop_enabled = loopEnabled;
    phi.loop_start = loopStart;
    phi.loop_end = loopEnd;
    notif.playhead_info.set(phi);
    return encodeProto(notif);
}

import engine.bridge : hibiki;

void sendNotificationD(const(ubyte)[] data) @trusted {
    if (data.length > 0) {
        hibiki.sendNotification(data.ptr, data.length);
    }
}

void sendAckD(string cmdType, bool success) @trusted {
    import std.string : toStringz;
    hibiki.sendAck(cmdType.toStringz(), success);
}
