module pb.pb_d_bridge;

import core.runtime : rt_init, rt_term;
import core.stdc.string : memcpy;
import dproto.proto : decodeProto, encodeProto;
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

/// Decodes a C++-serialized Project in D, verifies key invariants, and re-encodes it into outBuf.
/// Returns the encoded byte length, or -1 on failure.
extern (C++) int d_roundtrip_project(
    const(ubyte)* inData,
    int inLen,
    ubyte* outBuf,
    int outCapacity
) nothrow {
    if (inData is null || inLen < 0 || outBuf is null || outCapacity <= 0) {
        return -1;
    }
    const(ubyte)[] slice = inData[0 .. inLen];
    Project proj;
    if (!decodeProto(slice, proj)) {
        return -1;
    }
    ubyte[] encoded = encodeProto(proj);
    if (encoded.length > cast(size_t)outCapacity) {
        return -1;
    }
    if (encoded.length > 0) {
        memcpy(outBuf, encoded.ptr, encoded.length);
    }
    return cast(int)encoded.length;
}

/// Decodes a C++-serialized Request in D and re-encodes it into outBuf.
extern (C++) int d_roundtrip_request(
    const(ubyte)* inData,
    int inLen,
    ubyte* outBuf,
    int outCapacity
) nothrow {
    if (inData is null || inLen < 0 || outBuf is null || outCapacity <= 0) {
        return -1;
    }
    const(ubyte)[] slice = inData[0 .. inLen];
    Request req;
    if (!decodeProto(slice, req)) {
        return -1;
    }
    ubyte[] encoded = encodeProto(req);
    if (encoded.length > cast(size_t)outCapacity) {
        return -1;
    }
    if (encoded.length > 0) {
        memcpy(outBuf, encoded.ptr, encoded.length);
    }
    return cast(int)encoded.length;
}

/// Decodes a C++-serialized Notification in D and re-encodes it into outBuf.
extern (C++) int d_roundtrip_notification(
    const(ubyte)* inData,
    int inLen,
    ubyte* outBuf,
    int outCapacity
) nothrow {
    if (inData is null || inLen < 0 || outBuf is null || outCapacity <= 0) {
        return -1;
    }
    const(ubyte)[] slice = inData[0 .. inLen];
    Notification notif;
    if (!decodeProto(slice, notif)) {
        return -1;
    }
    ubyte[] encoded = encodeProto(notif);
    if (encoded.length > cast(size_t)outCapacity) {
        return -1;
    }
    if (encoded.length > 0) {
        memcpy(outBuf, encoded.ptr, encoded.length);
    }
    return cast(int)encoded.length;
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
