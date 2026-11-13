module engine.ipc.ipc;

import dproto.proto : encodeProto;
import engine.bridge : hibiki;
import pb.core : TransportState;
import pb.notifications : Acknowledge, Notification, PlayheadInfo;
import std.string : toStringz;

/// Magic header ("HBKI") prefixing framed IPC packets.
enum uint IPC_MAGIC = 0x48424B49;

private void writeLE32(ubyte[] dst, uint value) pure nothrow @nogc @safe {
    dst[0] = cast(ubyte)(value & 0xFF);
    dst[1] = cast(ubyte)((value >> 8) & 0xFF);
    dst[2] = cast(ubyte)((value >> 16) & 0xFF);
    dst[3] = cast(ubyte)((value >> 24) & 0xFF);
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

/// Sends a serialized notification buffer through the C++ IPC bridge.
void sendNotificationD(const(ubyte)[] data) @trusted {
    if (data.length > 0) {
        hibiki.sendNotification(data.ptr, data.length);
    }
}

/// Sends an acknowledgment message through the C++ IPC bridge.
void sendAckD(string cmdType, bool success) @trusted {
    hibiki.sendAck(cmdType.toStringz(), success);
}
