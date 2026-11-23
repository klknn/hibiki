module dproto.dproto_test;

import dproto.proto : decodeProto, encodeProto;
import dproto.wire;
import pb.commands;
import pb.core;
import pb.notifications;
import pb.plugin_worker;

@safe unittest {
    // 1. Varint and negative int32 (10-byte sign extension) edge cases
    ubyte[16] buf;
    size_t n = writeInt32(buf[], -1);
    assert(n == 10, "Negative int32 (-1) must encode as 10-byte varint in proto3");
    foreach (i; 0 .. 9) {
        assert(buf[i] == 0xFF);
    }
    assert(buf[9] == 0x01);

    const(ubyte)[] slice = buf[0 .. n];
    int decodedInt = 0;
    assert(readInt32(slice, decodedInt));
    assert(decodedInt == -1);
    assert(slice.length == 0);

    // Boundary values for int32
    size_t nMin = writeInt32(buf[], int.min);
    assert(nMin == 10);
    slice = buf[0 .. nMin];
    assert(readInt32(slice, decodedInt) && decodedInt == int.min);

    size_t nMax = writeInt32(buf[], int.max);
    assert(nMax == 5);
    slice = buf[0 .. nMax];
    assert(readInt32(slice, decodedInt) && decodedInt == int.max);
}

@safe unittest {
    // 2. Truncated / malformed wire buffer safety
    const(ubyte)[] empty;
    ulong v;
    assert(!readVarint(empty, v), "Empty varint must fail safely");

    // 10 bytes all with continuation bit set -> overflow
    const(ubyte)[11] badVarint = [0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80];
    const(ubyte)[] badSlice = badVarint[];
    assert(!readVarint(badSlice, v), "Overlong varint must fail safely");

    // Truncated length-delimited field
    const(ubyte)[3] truncLen = [cast(ubyte)makeTag(2, WireType.lengthDelimited), 10, 0x41];
    Clip clip;
    assert(!decodeProto(truncLen[], clip), "Truncated length-delimited field must fail");
}

@safe unittest {
    // 3. Full Project round-trip with packed floats, optional fields, and negative ints
    Project proj;
    proj.bpm = 138.5f;
    proj.playhead_sec = 4.25f;
    proj.transport_state = TransportState.TRANSPORT_STATE_PLAYING;

    Track trk;
    trk.track_index = 0;
    trk.name = "Acid Lead";
    trk.output_track_index = -1;
    trk.group_parent_index = -1;

    Plugin plug;
    plug.plugin_index = 0;
    plug.path = "builtin://303";
    plug.name = "AcidBass";
    plug.is_instrument = true;
    plug.sidechain_track_index = 0; // explicitly set to 0!
    plug.state = [0xDE, 0xAD, 0xBE, 0xEF];
    trk.plugins ~= plug;

    TimelineClip tclip;
    tclip.clip_index = 0;
    tclip.start_time_sec = 2.0f;
    tclip.alias_source = -1;
    tclip.clip.type = ClipType.CLIP_TYPE_MIDI;
    tclip.clip.name = "Pattern 1";
    tclip.clip.resolution = 480;
    tclip.clip.waveform_summary = [-0.5f, 0.0f, 0.75f, 1.0f];

    pb.core.MidiEvent ev;
    ev.tick = 240;
    ev.pitch = 60;
    ev.duration_ticks = 120;
    ev.velocity = 100;
    ev.cc_number = 128;
    ev.cc_value = -8192;
    tclip.clip.midi_events ~= ev;

    trk.timeline_clips ~= tclip;
    proj.tracks ~= trk;

    ubyte[] bytes = encodeProto(proj);
    assert(bytes.length > 0);

    Project decoded;
    assert(decodeProto(bytes, decoded));
    assert(decoded.bpm == 138.5f);
    assert(decoded.playhead_sec == 4.25f);
    assert(decoded.transport_state == TransportState.TRANSPORT_STATE_PLAYING);
    assert(decoded.tracks.length == 1);
    assert(decoded.tracks[0].name == "Acid Lead");
    assert(decoded.tracks[0].output_track_index == -1);
    assert(decoded.tracks[0].plugins.length == 1);
    assert(decoded.tracks[0].plugins[0].sidechain_track_index.hasValue);
    assert(decoded.tracks[0].plugins[0].sidechain_track_index.value == 0);
    assert(decoded.tracks[0].plugins[0].state == [0xDE, 0xAD, 0xBE, 0xEF]);
    assert(decoded.tracks[0].timeline_clips.length == 1);
    assert(decoded.tracks[0].timeline_clips[0].clip.waveform_summary == [-0.5f, 0.0f, 0.75f, 1.0f]);
    assert(decoded.tracks[0].timeline_clips[0].clip.midi_events.length == 1);
    assert(decoded.tracks[0].timeline_clips[0].clip.midi_events[0].cc_value == -8192);
}

@safe unittest {
    // 4. Oneof semantics: last decoded field wins, and presence of 0-valued fields works
    EntityRef ref1;
    ref1.track_index = 2;
    ref1.session_slot = 0; // value is 0, but present in oneof!

    ubyte[] b1 = encodeProto(ref1);
    EntityRef ref2;
    assert(decodeProto(b1, ref2));
    assert(ref2.track_index == 2);
    assert(ref2.session_slot.hasValue);
    assert(ref2.session_slot.value == 0);
    assert(!ref2.timeline_clip.hasValue);

    // Concatenate another oneof variant on the wire (timeline_clip = 5)
    EntityRef refOverride;
    refOverride.timeline_clip = 5;
    ubyte[] bCombined = b1 ~ encodeProto(refOverride);

    EntityRef refMerged;
    assert(decodeProto(bCombined, refMerged));
    assert(!refMerged.session_slot.hasValue, "Previous oneof field must be cleared when later oneof arrives");
    assert(refMerged.timeline_clip.hasValue && refMerged.timeline_clip.value == 5);
}

@nogc nothrow @safe unittest {
    // 5. Real-time @nogc ProcessAudio encoding
    RealtimeAudioPacket pkt;
    pkt.numSamples = 4;
    pkt.sampleRate = 48000.0;
    pkt.tempo = 140.0;
    pkt.timeSigNumerator = 4;
    pkt.timeSigDenominator = 4;
    pkt.continuousTimeSamples = 1024;
    pkt.projectTimeMusic = 2.5;
    pkt.hasInputs = true;

    immutable float[4] samples = [0.25f, -0.25f, 0.5f, -0.5f];
    ubyte[256] wireBuf;
    size_t written = encodeRealtimeProcessAudio(pkt, samples[], wireBuf[]);
    assert(written > 0);

    // Insufficient buffer size must return 0 without crashing
    ubyte[4] tinyBuf;
    assert(encodeRealtimeProcessAudio(pkt, samples[], tinyBuf[]) == 0);
}

@nogc nothrow @safe unittest {
    // 6. Zero-Phobos, @nogc Arena-based encoding and decoding
    ubyte[16384] arenaMem;
    Arena arena = Arena(arenaMem[]);

    Project proj;
    proj.bpm = 140.0f;
    proj.playhead_sec = 8.0f;
    proj.transport_state = TransportState.TRANSPORT_STATE_PLAYING;

    ubyte[4096] destBuf;
    size_t written = encodeProto(proj, destBuf[]);
    assert(written > 0);

    Project decoded;
    bool ok = decodeProto(destBuf[0 .. written], decoded, arena);
    assert(ok);
    assert(decoded.bpm == 140.0f);
    assert(decoded.playhead_sec == 8.0f);
    assert(decoded.transport_state == TransportState.TRANSPORT_STATE_PLAYING);
}
