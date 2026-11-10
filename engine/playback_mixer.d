module engine.playback_mixer;

import core.math : fabs;
import engine.core.math : clampVal, constantPowerPan;

enum int BLOCK_SIZE = 512;

/// Mixer track strip parameters for a 512-sample block.
struct TrackMixStrip {
    float volume = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    bool soloed = false;
    bool audible = true;
    float peakL = 0.0f;
    float peakR = 0.0f;
}

/// Computes the playhead advancement and loop wrapping for a 512-sample block.
double advancePlayhead(
    double currentPos,
    double timePerBlock,
    bool loopEnabled,
    double loopStart,
    double loopEnd
) @nogc nothrow pure @safe {
    double nextPos = currentPos + timePerBlock;
    if (loopEnabled && loopEnd > loopStart && nextPos >= loopEnd) {
        double loopLen = loopEnd - loopStart;
        double excess = nextPos - loopStart;
        nextPos = loopStart + (excess % loopLen);
    }
    return nextPos;
}

/// Applies constant-power panning and volume fader to a stereo block in-place.
/// Updates peak level meters for the track.
void applyTrackGainAndPan(
    double[] inOutL,
    double[] inOutR,
    ref TrackMixStrip strip
) @nogc nothrow @trusted {
    float gainL = 0.0f, gainR = 0.0f;
    constantPowerPan(strip.pan, gainL, gainR);
    float vol = strip.audible ? strip.volume : 0.0f;
    float totalL = gainL * vol;
    float totalR = gainR * vol;

    float peakL = 0.0f;
    float peakR = 0.0f;

    const size_t len = inOutL.length < inOutR.length ? inOutL.length : inOutR.length;
    foreach (i; 0 .. len) {
        inOutL[i] *= totalL;
        inOutR[i] *= totalR;
        float absL = cast(float)fabs(inOutL[i]);
        float absR = cast(float)fabs(inOutR[i]);
        if (absL > peakL) peakL = absL;
        if (absR > peakR) peakR = absR;
    }
    strip.peakL = peakL;
    strip.peakR = peakR;
}

/// Accumulates track buffer into master mix buffer.
void accumulateMix(
    const(double)[] trackL,
    const(double)[] trackR,
    double[] masterL,
    double[] masterR
) @nogc nothrow pure @safe {
    const size_t len = trackL.length < masterL.length ? trackL.length : masterL.length;
    foreach (i; 0 .. len) {
        masterL[i] += trackL[i];
        masterR[i] += trackR[i];
    }
}

/// Computes master peak levels and converts to interleaved float output.
void processMasterOutput(
    const(double)[] masterL,
    const(double)[] masterR,
    float[] outInterleaved,
    out float peakL,
    out float peakR
) @nogc nothrow @trusted {
    float pL = 0.0f;
    float pR = 0.0f;
    const size_t numFrames = masterL.length < masterR.length ? masterL.length : masterR.length;

    foreach (i; 0 .. numFrames) {
        float l = cast(float)masterL[i];
        float r = cast(float)masterR[i];
        outInterleaved[i * 2] = l;
        outInterleaved[i * 2 + 1] = r;

        float absL = cast(float)fabs(l);
        float absR = cast(float)fabs(r);
        if (absL > pL) pL = absL;
        if (absR > pR) pR = absR;
    }
    peakL = pL;
    peakR = pR;
}

// C++ interop externs for calling from playback_thread.cpp
extern (C++) {
    void d_advance_playhead(
        double currentPos,
        double timePerBlock,
        bool loopEnabled,
        double loopStart,
        double loopEnd,
        double* outPos
    ) @nogc nothrow {
        if (outPos) {
            *outPos = advancePlayhead(currentPos, timePerBlock, loopEnabled, loopStart, loopEnd);
        }
    }

    void d_apply_track_gain_and_pan(
        double* bufferL,
        double* bufferR,
        int blockSize,
        float volume,
        float pan,
        bool audible,
        float* outPeakL,
        float* outPeakR
    ) @nogc nothrow {
        if (!bufferL || !bufferR || blockSize <= 0) return;
        TrackMixStrip strip;
        strip.volume = volume;
        strip.pan = pan;
        strip.audible = audible;
        applyTrackGainAndPan(bufferL[0 .. blockSize], bufferR[0 .. blockSize], strip);
        if (outPeakL) *outPeakL = strip.peakL;
        if (outPeakR) *outPeakR = strip.peakR;
    }

    void d_accumulate_mix(
        const(double)* trackL,
        const(double)* trackR,
        double* masterL,
        double* masterR,
        int blockSize
    ) @nogc nothrow {
        if (!trackL || !trackR || !masterL || !masterR || blockSize <= 0) return;
        accumulateMix(trackL[0 .. blockSize], trackR[0 .. blockSize], masterL[0 .. blockSize], masterR[0 .. blockSize]);
    }

    void d_process_master_output(
        const(double)* masterL,
        const(double)* masterR,
        float* outInterleaved,
        int blockSize,
        float* outPeakL,
        float* outPeakR
    ) @nogc nothrow {
        if (!masterL || !masterR || !outInterleaved || blockSize <= 0) return;
        float pL = 0.0f, pR = 0.0f;
        processMasterOutput(masterL[0 .. blockSize], masterR[0 .. blockSize], outInterleaved[0 .. blockSize * 2], pL, pR);
        if (outPeakL) *outPeakL = pL;
        if (outPeakR) *outPeakR = pR;
    }
}
