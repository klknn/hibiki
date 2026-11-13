module engine.benchmark.benchmark_d;

import core.runtime : rt_init, rt_term;
import dproto.proto : Arena, decodeProto, encodeProto;
import engine.core.biquad_filter : BiquadFilter;
import engine.core.history : HistoryManagerD;
import engine.core.math : constantPowerPan, dbToLinear;
import engine.effects.builtin_compressor : CompressorDspState, d_compressor_process;
import engine.playback_mixer : accumulateMix, applyTrackGainAndPan, processMasterOutput, TrackMixStrip;
import pb.commands : Request, TransportCmd;
import pb.core : Clip, ClipType, MidiEvent, Project, TimelineClip, Track;
import pb.notifications : Notification, PlayheadInfo;

private Request makeSampleRequest() @nogc nothrow pure @safe {
    Request req;
    TransportCmd t;
    t.action = TransportCmd.Action.ACTION_PLAY;
    req.transport.set(t);
    return req;
}

private Notification makeSampleNotification() @nogc nothrow pure @safe {
    Notification notif;
    PlayheadInfo phi;
    phi.position_sec = 12.5f;
    phi.bpm = 128.0f;
    phi.loop_enabled = true;
    phi.loop_start = 1.0f;
    phi.loop_end = 5.0f;
    notif.playhead_info.set(phi);
    return notif;
}

private Project makeSampleProject() @safe {
    Project proj;
    proj.bpm = 128.0f;
    proj.playhead_sec = 16.0f;
    proj.tracks = new Track[](8);
    foreach (t; 0 .. 8) {
        Track track;
        track.track_index = cast(uint)t;
        track.name = "Track";
        track.timeline_clips = new TimelineClip[](4);
        foreach (c; 0 .. 4) {
            TimelineClip tc;
            tc.clip_index = cast(uint)c;
            tc.start_time_sec = c * 4.0f;
            tc.duration_beats = 4.0f;
            Clip clip;
            clip.type = ClipType.CLIP_TYPE_MIDI;
            clip.name = "Midi Clip";
            clip.midi_events = new MidiEvent[](16);
            foreach (m; 0 .. 16) {
                MidiEvent ev;
                ev.tick = m * 120;
                ev.pitch = cast(uint)(60 + (m % 12));
                ev.duration_ticks = 100;
                ev.velocity = 100;
                clip.midi_events[m] = ev;
            }
            tc.clip = clip;
            track.timeline_clips[c] = tc;
        }
        proj.tracks[t] = track;
    }
    return proj;
}

extern (C) {
    int d_bench_init() {
        return rt_init();
    }

    int d_bench_term() {
        return rt_term();
    }

    // 1. Protobuf Request encode / decode (loop in D, pre-built message, reused Arena)
    int d_bench_encode_request_once(ubyte* outBuf, int capacity) @nogc nothrow {
        Request req = makeSampleRequest();
        return cast(int)encodeProto(req, outBuf[0 .. capacity]);
    }

    ulong d_bench_encode_request_loop(ubyte* outBuf, int capacity, int iterations) @nogc nothrow {
        Request req = makeSampleRequest();
        ubyte[] buf = outBuf[0 .. capacity];
        ulong checksum = 0;
        foreach (_; 0 .. iterations) {
            size_t written = encodeProto(req, buf);
            checksum += written + buf[0];
        }
        return checksum;
    }

    ulong d_bench_decode_request_loop(const(ubyte)* inData, int len, int iterations) @nogc nothrow {
        if (!inData || len <= 0) return 0;
        const(ubyte)[] slice = inData[0 .. len];
        ubyte[4096] scratch = void;
        Arena arena = Arena(scratch[]);
        ulong checksum = 0;
        foreach (_; 0 .. iterations) {
            arena.reset();
            Request req;
            if (decodeProto(slice, req, arena)) {
                checksum += cast(ulong)req.transport.value.action;
            }
        }
        return checksum;
    }

    // 2. Protobuf Notification encode / decode
    int d_bench_encode_notification_once(ubyte* outBuf, int capacity) @nogc nothrow {
        Notification notif = makeSampleNotification();
        return cast(int)encodeProto(notif, outBuf[0 .. capacity]);
    }

    ulong d_bench_encode_notification_loop(ubyte* outBuf, int capacity, int iterations) @nogc nothrow {
        Notification notif = makeSampleNotification();
        ubyte[] buf = outBuf[0 .. capacity];
        ulong checksum = 0;
        foreach (_; 0 .. iterations) {
            size_t written = encodeProto(notif, buf);
            checksum += written + buf[0];
        }
        return checksum;
    }

    ulong d_bench_decode_notification_loop(const(ubyte)* inData, int len, int iterations) @nogc nothrow {
        if (!inData || len <= 0) return 0;
        const(ubyte)[] slice = inData[0 .. len];
        ubyte[4096] scratch = void;
        Arena arena = Arena(scratch[]);
        ulong checksum = 0;
        foreach (_; 0 .. iterations) {
            arena.reset();
            Notification notif;
            if (decodeProto(slice, notif, arena)) {
                checksum += cast(ulong)notif.playhead_info.value.bpm;
            }
        }
        return checksum;
    }

    // 3. Protobuf Full Project Session (pre-built Project outside loop, reused Arena for decode)
    int d_bench_encode_project_once(ubyte* outBuf, int capacity) {
        Project proj = makeSampleProject();
        return cast(int)encodeProto(proj, outBuf[0 .. capacity]);
    }

    ulong d_bench_encode_project_loop(ubyte* outBuf, int capacity, int iterations) {
        Project proj = makeSampleProject();
        ubyte[] buf = outBuf[0 .. capacity];
        ulong checksum = 0;
        foreach (_; 0 .. iterations) {
            size_t written = encodeProto(proj, buf);
            checksum += written + buf[0];
        }
        return checksum;
    }

    ulong d_bench_decode_project_loop(const(ubyte)* inData, int len, int iterations) @nogc nothrow {
        if (!inData || len <= 0) return 0;
        const(ubyte)[] slice = inData[0 .. len];
        ubyte[65536] scratch = void;
        Arena arena = Arena(scratch[]);
        ulong checksum = 0;
        foreach (_; 0 .. iterations) {
            arena.reset();
            Project proj;
            if (decodeProto(slice, proj, arena)) {
                checksum += proj.tracks.length + cast(ulong)proj.bpm;
            }
        }
        return checksum;
    }

    // 4. DSP Biquad Filter (reads from immutable src, writes to dst)
    void d_bench_biquad(const(float)* src, float* dst, int numSamples) @nogc nothrow {
        BiquadFilter filter;
        filter.setParams(BiquadFilter.Type.LOWPASS, 1000.0f, 0.707f, 0.0f, 44100.0f);
        foreach (i; 0 .. numSamples) {
            dst[i] = filter.process(src[i]);
        }
    }

    // 5. DSP Compressor (full production d_compressor_process)
    void d_bench_compressor(
        const(float)* inL,
        const(float)* inR,
        float* outL,
        float* outR,
        int numBlocks,
        int blockSize
    ) @nogc nothrow {
        CompressorDspState state;
        state.reset();

        foreach (b; 0 .. numBlocks) {
            int offset = b * blockSize;
            d_compressor_process(
                state,
                inL + offset,
                inR + offset,
                outL + offset,
                outR + offset,
                blockSize,
                44100.0,
                0.7,  // threshold (-18 dB)
                0.75, // ratio (4:1)
                0.2,  // attack
                0.3,  // release
                0.2,  // knee (6 dB soft knee)
                0.2,  // makeup
                0.0,  // up_threshold
                0.0,  // up_ratio
                false,
                true,
                null,
                null
            );
        }
    }

    // 6. 512-sample Block Mixer in D
    double d_bench_mixer(
        double* trackBufL,
        double* trackBufR,
        double* masterL,
        double* masterR,
        float* outInterleaved,
        int numTracks,
        int numBlocks,
        int blockSize
    ) @nogc nothrow {
        double peakAccum = 0.0;
        double[] mL = masterL[0 .. blockSize];
        double[] mR = masterR[0 .. blockSize];
        double[] tL = trackBufL[0 .. blockSize];
        double[] tR = trackBufR[0 .. blockSize];
        float[] outBuf = outInterleaved[0 .. blockSize * 2];

        foreach (b; 0 .. numBlocks) {
            mL[] = 0.0;
            mR[] = 0.0;

            foreach (t; 0 .. numTracks) {
                TrackMixStrip strip;
                strip.volume = 0.8f;
                strip.pan = (t % 2 == 0) ? -0.25f : 0.25f;
                strip.audible = true;

                tL[] = 0.1;
                tR[] = 0.1;

                applyTrackGainAndPan(tL, tR, strip);
                accumulateMix(tL, tR, mL, mR);
            }

            float pL = 0.0f, pR = 0.0f;
            processMasterOutput(mL, mR, outBuf, pL, pR);
            peakAccum += pL + pR;
        }
        return peakAccum;
    }

    // 7. Undo/Redo History Manager in D (64-byte state snapshot)
    ulong d_bench_history(int iterations) @safe {
        HistoryManagerD hist;
        hist.maxHistory = 50;

        ubyte[64] initData;
        foreach (k; 0 .. 64) {
            initData[k] = cast(ubyte)(k + 1);
        }
        ubyte[] stateBytes = initData.dup;
        ubyte[] temp;
        ulong checksum = 0;

        foreach (i; 0 .. iterations) {
            stateBytes[0] = cast(ubyte)(i & 0xFF);
            hist.pushState(stateBytes);
            if (i % 4 == 0) {
                if (hist.undo(stateBytes, temp)) {
                    checksum += temp[0];
                    if (hist.redo(temp, stateBytes)) {
                        checksum += stateBytes[0];
                    }
                }
            }
        }
        return checksum;
    }

    // 8. Fast Math Primitives in D
    double d_bench_math(int iterations) @nogc nothrow {
        double sum = 0.0;
        foreach (i; 0 .. iterations) {
            float pan = cast(float)(i % 200 - 100) / 100.0f;
            float gL = 0.0f, gR = 0.0f;
            constantPowerPan(pan, gL, gR);

            float db = cast(float)(i % 120 - 60);
            float lin = dbToLinear(db);
            sum += cast(double)(gL + gR + lin);
        }
        return sum;
    }
}
