module engine.benchmark.benchmark_d;

import core.runtime : rt_init, rt_term;
import dproto.proto : Arena, decodeProto, encodeProto;
import engine.core.biquad_filter : BiquadFilter;
import engine.core.history : HistoryManagerD;
import engine.core.math : constantPowerPan, dbToLinear;
import engine.effects.builtin_compressor : CompressorDspState, d_compressor_process;
import engine.playback_mixer : accumulateMix, applyTrackGainAndPan, processMasterOutput, TrackMixStrip;
import pb.commands : AutomationCmd, Request, TrackCmd, TransportCmd;
import pb.core : Clip, ClipType, MidiEvent, Project, TimelineClip, Track;
import pb.notifications : Acknowledge, Notification, PlayheadInfo;

extern (C) {
    int d_bench_init() {
        return rt_init();
    }

    int d_bench_term() {
        return rt_term();
    }

    // 1. Protobuf Request encode / decode in D
    int d_bench_encode_request(ubyte* outBuf, int capacity) @nogc nothrow {
        Request req;
        TransportCmd t;
        t.action = TransportCmd.Action.ACTION_PLAY;
        req.transport.set(t);

        ubyte[4096] scratch;
        Arena arena = Arena(scratch[]);
        size_t written = encodeProto(req, outBuf[0 .. capacity]);
        return cast(int)written;
    }

    int d_bench_decode_request(const(ubyte)* inData, int len) @nogc nothrow {
        if (!inData || len <= 0) return 0;
        Request req;
        ubyte[4096] scratch;
        Arena arena = Arena(scratch[]);
        bool ok = decodeProto(inData[0 .. len], req, arena);
        return ok ? 1 : 0;
    }

    // 2. Protobuf Notification encode / decode in D
    int d_bench_encode_notification(ubyte* outBuf, int capacity) @nogc nothrow {
        Notification notif;
        PlayheadInfo phi;
        phi.position_sec = 12.5f;
        phi.bpm = 128.0f;
        phi.loop_enabled = true;
        phi.loop_start = 1.0f;
        phi.loop_end = 5.0f;
        notif.playhead_info.set(phi);

        size_t written = encodeProto(notif, outBuf[0 .. capacity]);
        return cast(int)written;
    }

    int d_bench_decode_notification(const(ubyte)* inData, int len) @nogc nothrow {
        if (!inData || len <= 0) return 0;
        Notification notif;
        ubyte[4096] scratch;
        Arena arena = Arena(scratch[]);
        bool ok = decodeProto(inData[0 .. len], notif, arena);
        return ok ? 1 : 0;
    }

    // 3. DSP Biquad Filter in D
    void d_bench_biquad(float* inOut, int numSamples) @nogc nothrow {
        BiquadFilter filter;
        filter.setParams(BiquadFilter.Type.LOWPASS, 1000.0f, 0.707f, 0.0f, 44100.0f);
        foreach (i; 0 .. numSamples) {
            inOut[i] = filter.process(inOut[i]);
        }
    }

    // 4. DSP Compressor in D
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
                inL ? inL + offset : null,
                inR ? inR + offset : null,
                outL + offset,
                outR + offset,
                blockSize,
                44100.0,
                0.7, // threshold (-18 dB)
                0.75, // ratio (4:1)
                0.2, // attack
                0.3, // release
                0.0, // knee
                0.2, // makeup
                0.0, // up_threshold
                0.0, // up_ratio
                false, // rms_mode
                true, // enabled
                null, // scL
                null  // scR
            );
        }
    }

    // 5. 512-sample Block Mixer in D
    void d_bench_mixer(
        double* trackBufL,
        double* trackBufR,
        double* masterL,
        double* masterR,
        float* outInterleaved,
        int numTracks,
        int numBlocks,
        int blockSize
    ) @nogc nothrow {
        foreach (b; 0 .. numBlocks) {
            masterL[0 .. blockSize] = 0.0;
            masterR[0 .. blockSize] = 0.0;

            foreach (t; 0 .. numTracks) {
                TrackMixStrip strip;
                strip.volume = 0.8f;
                strip.pan = (t % 2 == 0) ? -0.25f : 0.25f;
                strip.audible = true;

                // Fill synthetic signal
                trackBufL[0 .. blockSize] = 0.1;
                trackBufR[0 .. blockSize] = 0.1;

                applyTrackGainAndPan(trackBufL[0 .. blockSize], trackBufR[0 .. blockSize], strip);
                accumulateMix(trackBufL[0 .. blockSize], trackBufR[0 .. blockSize], masterL[0 .. blockSize], masterR[0 .. blockSize]);
            }

            float pL = 0.0f, pR = 0.0f;
            processMasterOutput(masterL[0 .. blockSize], masterR[0 .. blockSize], outInterleaved[0 .. blockSize * 2], pL, pR);
        }
    }

    // 6. History Manager in D
    void d_bench_history(int iterations) @safe {
        HistoryManagerD hist;
        hist.maxHistory = 50;

        ubyte[] stateBytes = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
        ubyte[] temp;

        foreach (i; 0 .. iterations) {
            hist.pushState(stateBytes);
            if (i % 4 == 0) {
                hist.undo(stateBytes, temp);
                hist.redo(temp, stateBytes);
            }
        }
    }

    // 7. Fast Math Primitives in D
    float d_bench_math(int iterations) @nogc nothrow {
        float sum = 0.0f;
        foreach (i; 0 .. iterations) {
            float pan = cast(float)(i % 200 - 100) / 100.0f;
            float gL = 0.0f, gR = 0.0f;
            constantPowerPan(pan, gL, gR);

            float db = cast(float)(i % 120 - 60);
            float lin = dbToLinear(db);
            sum += gL + gR + lin;
        }
        return sum;
    }

    // 8. Protobuf Full Project Session (8 tracks, 32 clips, 512 MIDI events)
    int d_bench_encode_project(ubyte* outBuf, int capacity) {
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

        size_t written = encodeProto(proj, outBuf[0 .. capacity]);
        return cast(int)written;
    }

    int d_bench_decode_project(const(ubyte)* inData, int len) nothrow {
        if (!inData || len <= 0) return 0;
        Project proj;
        bool ok = decodeProto(inData[0 .. len], proj);
        return ok ? 1 : 0;
    }
}
