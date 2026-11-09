module pb.pb_d_bridge;

import core.runtime : rt_init, rt_term;
import dproto.proto : Arena, computeProtoSize, decodeProto, encodeProto;
import dproto.wire : RealtimeAudioPacket, encodeRealtimeProcessAudio;
import pb.commands : Request;
import pb.core : Project;
import pb.notifications : Notification;

extern (C++) int hibiki_d_rt_init() {
    return rt_init();
}

extern (C++) int hibiki_d_rt_term() {
    return rt_term();
}

/// Decodes a C++-serialized Project in D using Arena, verifies invariants, and re-encodes it directly into outBuf without GC.
/// Returns the encoded byte length, or -1 on failure.
extern (C++) int d_roundtrip_project(
    const(ubyte)* inData,
    int inLen,
    ubyte* outBuf,
    int outCapacity
) @nogc nothrow {
    if (inData is null || inLen < 0 || outBuf is null || outCapacity <= 0) {
        return -1;
    }
    const(ubyte)[] slice = inData[0 .. inLen];
    Project proj;
    ubyte[65536] scratch;
    Arena arena = Arena(scratch[]);
    if (!decodeProto(slice, proj, arena)) {
        return -1;
    }
    size_t written = encodeProto(proj, outBuf[0 .. outCapacity]);
    if (written == 0 && computeProtoSize(proj) > 0) {
        return -1;
    }
    return cast(int)written;
}

/// Decodes a C++-serialized Request in D using Arena and re-encodes it directly into outBuf without GC.
extern (C++) int d_roundtrip_request(
    const(ubyte)* inData,
    int inLen,
    ubyte* outBuf,
    int outCapacity
) @nogc nothrow {
    if (inData is null || inLen < 0 || outBuf is null || outCapacity <= 0) {
        return -1;
    }
    const(ubyte)[] slice = inData[0 .. inLen];
    Request req;
    ubyte[65536] scratch;
    Arena arena = Arena(scratch[]);
    if (!decodeProto(slice, req, arena)) {
        return -1;
    }
    size_t written = encodeProto(req, outBuf[0 .. outCapacity]);
    if (written == 0 && computeProtoSize(req) > 0) {
        return -1;
    }
    return cast(int)written;
}

/// Decodes a C++-serialized Notification in D using Arena and re-encodes it directly into outBuf without GC.
extern (C++) int d_roundtrip_notification(
    const(ubyte)* inData,
    int inLen,
    ubyte* outBuf,
    int outCapacity
) @nogc nothrow {
    if (inData is null || inLen < 0 || outBuf is null || outCapacity <= 0) {
        return -1;
    }
    const(ubyte)[] slice = inData[0 .. inLen];
    Notification notif;
    ubyte[65536] scratch;
    Arena arena = Arena(scratch[]);
    if (!decodeProto(slice, notif, arena)) {
        return -1;
    }
    size_t written = encodeProto(notif, outBuf[0 .. outCapacity]);
    if (written == 0 && computeProtoSize(notif) > 0) {
        return -1;
    }
    return cast(int)written;
}

/// Encodes a ProcessAudio message using the @nogc real-time serializer in D.
extern (C++) int d_encode_realtime_process_audio(
    int numSamples,
    double sampleRate,
    double tempo,
    const(float)* samples,
    int sampleCount,
    ubyte* outBuf,
    int outCapacity
) @nogc nothrow {
    if (outBuf is null || outCapacity <= 0 || sampleCount < 0) {
        return -1;
    }
    RealtimeAudioPacket pkt;
    pkt.numSamples = numSamples;
    pkt.sampleRate = sampleRate;
    pkt.tempo = tempo;
    pkt.timeSigNumerator = 4;
    pkt.timeSigDenominator = 4;
    pkt.continuousTimeSamples = 2048;
    pkt.projectTimeMusic = 4.0;
    pkt.hasInputs = (sampleCount > 0);

    const(float)[] sampleSlice = (samples !is null && sampleCount > 0)
        ? samples[0 .. sampleCount]
        : null;
    size_t written = encodeRealtimeProcessAudio(
        pkt,
        sampleSlice,
        outBuf[0 .. outCapacity]
    );
    return (written > 0) ? cast(int)written : -1;
}
