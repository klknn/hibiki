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
    size_t len = trackL.length;
    if (trackR.length < len) len = trackR.length;
    if (masterL.length < len) len = masterL.length;
    if (masterR.length < len) len = masterR.length;
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
    size_t numFrames = masterL.length < masterR.length ? masterL.length : masterR.length;
    if (outInterleaved.length / 2 < numFrames) {
        numFrames = outInterleaved.length / 2;
    }

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

/// Renders a mono or interleaved-stereo audio clip block into stereo track buffers,
/// applying optional loop wrapping and exponential dB fade-in / fade-out.
void renderAudioClipBlock(
    const(float)[] audioData,
    int numChannels,
    bool isLoop,
    int trimSamples,
    int startSample,
    int loopLen,
    double clipLocalTime,
    double clipDuration,
    double fadeInSec,
    double fadeOutSec,
    double sampleRate,
    double[] bufferL,
    double[] bufferR
) @nogc nothrow @safe {
    import core.stdc.math : pow;

    if (audioData.length == 0 || sampleRate <= 0.0) return;
    size_t blockSize = bufferL.length < bufferR.length ? bufferL.length : bufferR.length;
    enum double fadeMinDb = -30.0;
    const double invSr = 1.0 / sampleRate;

    foreach (i; 0 .. blockSize) {
        const double sampleTimeInClip = clipLocalTime + cast(double)i * invSr;
        if (sampleTimeInClip < 0.0) continue;
        if (clipDuration > 0.0 && sampleTimeInClip >= clipDuration) break;
        int samplePos = startSample + cast(int)i;
        if (samplePos < 0) continue;
        if (isLoop && loopLen > 0) {
            int rel = (samplePos - trimSamples) % loopLen;
            if (rel < 0) rel += loopLen;
            samplePos = trimSamples + rel;
        }

        double fadeGain = 1.0;
        if (fadeInSec > 0.0 && sampleTimeInClip < fadeInSec) {
            double t = sampleTimeInClip / fadeInSec;
            double db = fadeMinDb * (1.0 - t);
            fadeGain *= pow(10.0, db / 20.0);
        }
        if (fadeOutSec > 0.0 && sampleTimeInClip > clipDuration - fadeOutSec) {
            double t = (clipDuration - sampleTimeInClip) / fadeOutSec;
            if (t < 0.0) t = 0.0;
            double db = fadeMinDb * (1.0 - t);
            fadeGain *= pow(10.0, db / 20.0);
        }

        if (numChannels == 2) {
            size_t idx = cast(size_t)samplePos * 2;
            if (idx + 1 < audioData.length) {
                bufferL[i] += audioData[idx] * fadeGain;
                bufferR[i] += audioData[idx + 1] * fadeGain;
            }
        } else if (numChannels == 1) {
            size_t idx = cast(size_t)samplePos;
            if (idx < audioData.length) {
                double s = audioData[idx] * fadeGain;
                bufferL[i] += s;
                bufferR[i] += s;
            }
        }
    }
}

/// Accumulates a scaled pre-fader stereo signal into an aux bus buffer.
void accumulateAuxSend(
    const(double)[] srcL,
    const(double)[] srcR,
    double[] dstL,
    double[] dstR,
    float level
) @nogc nothrow pure @safe {
    size_t len = srcL.length;
    if (srcR.length < len) len = srcR.length;
    if (dstL.length < len) len = dstL.length;
    if (dstR.length < len) len = dstR.length;
    const double lvl = level;
    foreach (i; 0 .. len) {
        dstL[i] += srcL[i] * lvl;
        dstR[i] += srcR[i] * lvl;
    }
}

/// Renders a session clip audio block (mono or stereo, optional looping) into stereo track buffers.
void renderSessionAudioClipBlock(
    const(float)[] audioData,
    int numChannels,
    bool isLoop,
    int startSample,
    double[] bufferL,
    double[] bufferR
) @nogc nothrow pure @safe {
    if (audioData.length == 0 || numChannels <= 0) return;
    const int totalSamples = cast(int)(audioData.length / numChannels);
    if (totalSamples <= 0) return;
    const size_t blockSize = bufferL.length < bufferR.length ? bufferL.length : bufferR.length;

    foreach (i; 0 .. blockSize) {
        int samplePos = startSample + cast(int)i;
        if (samplePos < 0) continue;
        if (isLoop) {
            samplePos = samplePos % totalSamples;
        }
        if (numChannels == 2) {
            size_t idx = cast(size_t)samplePos * 2;
            if (idx + 1 < audioData.length) {
                bufferL[i] += audioData[idx];
                bufferR[i] += audioData[idx + 1];
            }
        } else if (numChannels == 1) {
            size_t idx = cast(size_t)samplePos;
            if (idx < audioData.length) {
                double s = audioData[idx];
                bufferL[i] += s;
                bufferR[i] += s;
            }
        }
    }
}

/// Downsamples recorded audio into a peak-amplitude waveform summary buffer.
void computeWaveformSummary(
    const(float)[] recordBuffer,
    int numChannels,
    float[] outSummary
) @nogc nothrow pure @safe {
    if (outSummary.length == 0) return;
    outSummary[] = 0.0f;
    if (recordBuffer.length == 0 || numChannels <= 0) return;

    const size_t totalSamples = recordBuffer.length;
    const size_t summarySize = outSummary.length;
    size_t samplesPerBucket = totalSamples / (cast(size_t)numChannels * summarySize);
    if (samplesPerBucket < 1) samplesPerBucket = 1;

    foreach (b; 0 .. summarySize) {
        float peak = 0.0f;
        foreach (s; 0 .. samplesPerBucket) {
            size_t idx = (b * samplesPerBucket + s) * cast(size_t)numChannels;
            if (idx < totalSamples) {
                float v = recordBuffer[idx];
                float av = v >= 0.0f ? v : -v;
                if (av > peak) peak = av;
            }
        }
        outSummary[b] = peak;
    }
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

    void d_render_audio_clip_block(
        const(float)* audioData,
        int audioLen,
        int numChannels,
        bool isLoop,
        int trimSamples,
        int startSample,
        int loopLen,
        double clipLocalTime,
        double clipDuration,
        double fadeInSec,
        double fadeOutSec,
        double sampleRate,
        double* bufferL,
        double* bufferR,
        int blockSize
    ) @nogc nothrow {
        if (!audioData || audioLen <= 0 || !bufferL || !bufferR || blockSize <= 0) return;
        renderAudioClipBlock(
            audioData[0 .. audioLen], numChannels, isLoop, trimSamples,
            startSample, loopLen, clipLocalTime, clipDuration, fadeInSec,
            fadeOutSec, sampleRate, bufferL[0 .. blockSize], bufferR[0 .. blockSize]);
    }

    void d_accumulate_aux_send(
        const(double)* srcL,
        const(double)* srcR,
        double* dstL,
        double* dstR,
        int blockSize,
        float level
    ) @nogc nothrow {
        if (!srcL || !srcR || !dstL || !dstR || blockSize <= 0) return;
        accumulateAuxSend(srcL[0 .. blockSize], srcR[0 .. blockSize],
                          dstL[0 .. blockSize], dstR[0 .. blockSize], level);
    }

    void d_render_session_audio_clip_block(
        const(float)* audioData,
        int audioLen,
        int numChannels,
        bool isLoop,
        int startSample,
        double* bufferL,
        double* bufferR,
        int blockSize
    ) @nogc nothrow {
        if (!audioData || audioLen <= 0 || !bufferL || !bufferR || blockSize <= 0) return;
        renderSessionAudioClipBlock(
            audioData[0 .. audioLen], numChannels, isLoop, startSample,
            bufferL[0 .. blockSize], bufferR[0 .. blockSize]);
    }

    void d_compute_waveform_summary(
        const(float)* recordBuffer,
        int totalSamples,
        int numChannels,
        float* outSummary,
        int summarySize
    ) @nogc nothrow {
        if (!recordBuffer || totalSamples <= 0 || !outSummary || summarySize <= 0) return;
        computeWaveformSummary(
            recordBuffer[0 .. totalSamples], numChannels, outSummary[0 .. summarySize]);
    }

    void d_double_to_float_stereo(
        const(double)* srcL,
        const(double)* srcR,
        float* dstL,
        float* dstR,
        int blockSize
    ) @nogc nothrow {
        if (!srcL || !srcR || !dstL || !dstR || blockSize <= 0) return;
        foreach (i; 0 .. blockSize) {
            dstL[i] = cast(float)srcL[i];
            dstR[i] = cast(float)srcR[i];
        }
    }

    void d_float_to_double_stereo(
        const(float)* srcL,
        const(float)* srcR,
        double* dstL,
        double* dstR,
        int blockSize
    ) @nogc nothrow {
        if (!srcL || !srcR || !dstL || !dstR || blockSize <= 0) return;
        foreach (i; 0 .. blockSize) {
            dstL[i] = srcL[i];
            dstR[i] = srcR[i];
        }
    }
}

/// 3-band mixer EQ filter state layout: 3 bands x 2 channels x 4 state vars (x1, x2, y1, y2) = 24 doubles.
enum EQ_NUM_BANDS = 3;
enum EQ_STATE_DOUBLES = 24;

private struct EqCoeffs {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
}

private EqCoeffs computeLowShelfCoeffs(double freqHz, double q, double gainDb, double sampleRate) @nogc nothrow {
    import core.stdc.math : cos, pow, sin, sqrt;
    EqCoeffs c;
    if (sampleRate <= 0.0 || freqHz <= 0.0 || freqHz >= 0.49 * sampleRate) return c;
    const double a = pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
    const double cosW0 = cos(w0);
    const double sinW0 = sin(w0);
    const double alpha = sinW0 / (2.0 * q);
    const double twoSqrtAAlpha = 2.0 * sqrt(a) * alpha;

    const double a0 = (a + 1.0) + (a - 1.0) * cosW0 + twoSqrtAAlpha;
    if (fabs(a0) < 1e-18) return c;
    c.b0 = (a * ((a + 1.0) - (a - 1.0) * cosW0 + twoSqrtAAlpha)) / a0;
    c.b1 = (2.0 * a * ((a - 1.0) - (a + 1.0) * cosW0)) / a0;
    c.b2 = (a * ((a + 1.0) - (a - 1.0) * cosW0 - twoSqrtAAlpha)) / a0;
    c.a1 = (-2.0 * ((a - 1.0) + (a + 1.0) * cosW0)) / a0;
    c.a2 = ((a + 1.0) + (a - 1.0) * cosW0 - twoSqrtAAlpha) / a0;
    return c;
}

private EqCoeffs computeHighShelfCoeffs(double freqHz, double q, double gainDb, double sampleRate) @nogc nothrow {
    import core.stdc.math : cos, pow, sin, sqrt;
    EqCoeffs c;
    if (sampleRate <= 0.0 || freqHz <= 0.0 || freqHz >= 0.49 * sampleRate) return c;
    const double a = pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
    const double cosW0 = cos(w0);
    const double sinW0 = sin(w0);
    const double alpha = sinW0 / (2.0 * q);
    const double twoSqrtAAlpha = 2.0 * sqrt(a) * alpha;

    const double a0 = (a + 1.0) - (a - 1.0) * cosW0 + twoSqrtAAlpha;
    if (fabs(a0) < 1e-18) return c;
    c.b0 = (a * ((a + 1.0) + (a - 1.0) * cosW0 + twoSqrtAAlpha)) / a0;
    c.b1 = (-2.0 * a * ((a - 1.0) + (a + 1.0) * cosW0)) / a0;
    c.b2 = (a * ((a + 1.0) + (a - 1.0) * cosW0 - twoSqrtAAlpha)) / a0;
    c.a1 = (2.0 * ((a - 1.0) - (a + 1.0) * cosW0)) / a0;
    c.a2 = ((a + 1.0) - (a - 1.0) * cosW0 - twoSqrtAAlpha) / a0;
    return c;
}

private EqCoeffs computeBellCoeffs(double freqHz, double q, double gainDb, double sampleRate) @nogc nothrow {
    import core.stdc.math : cos, pow, sin;
    EqCoeffs c;
    if (sampleRate <= 0.0 || freqHz <= 0.0 || freqHz >= 0.49 * sampleRate) return c;
    const double a = pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
    const double cosW0 = cos(w0);
    const double sinW0 = sin(w0);
    const double alpha = sinW0 / (2.0 * q);

    const double a0 = 1.0 + alpha / a;
    if (fabs(a0) < 1e-18) return c;
    c.b0 = (1.0 + alpha * a) / a0;
    c.b1 = (-2.0 * cosW0) / a0;
    c.b2 = (1.0 - alpha * a) / a0;
    c.a1 = (-2.0 * cosW0) / a0;
    c.a2 = (1.0 - alpha / a) / a0;
    return c;
}

private void runBiquadStereo(
    ref const(EqCoeffs) c,
    double[] bufL,
    double[] bufR,
    double* stL4,
    double* stR4
) @nogc nothrow {
    double lx1 = stL4[0], lx2 = stL4[1], ly1 = stL4[2], ly2 = stL4[3];
    double rx1 = stR4[0], rx2 = stR4[1], ry1 = stR4[2], ry2 = stR4[3];
    foreach (i; 0 .. bufL.length) {
        const double xl = bufL[i];
        const double yl = c.b0 * xl + c.b1 * lx1 + c.b2 * lx2 - c.a1 * ly1 - c.a2 * ly2;
        lx2 = lx1; lx1 = xl; ly2 = ly1; ly1 = fabs(yl) < 1e-24 ? 0.0 : yl;
        bufL[i] = yl;

        const double xr = bufR[i];
        const double yr = c.b0 * xr + c.b1 * rx1 + c.b2 * rx2 - c.a1 * ry1 - c.a2 * ry2;
        rx2 = rx1; rx1 = xr; ry2 = ry1; ry1 = fabs(yr) < 1e-24 ? 0.0 : yr;
        bufR[i] = yr;
    }
    stL4[0] = lx1; stL4[1] = lx2; stL4[2] = ly1; stL4[3] = ly2;
    stR4[0] = rx1; stR4[1] = rx2; stR4[2] = ry1; stR4[3] = ry2;
}

/// Applies 3-band mixer EQ (`[0..1]` centered at `0.5` -> `[-15..+15] dB`)
/// with 120 Hz low shelf, 1 kHz peaking bell, and 6.5 kHz high shelf.
void applyTrackEq(
    double[] bufL,
    double[] bufR,
    double sampleRate,
    float eqLow,
    float eqMid,
    float eqHigh,
    double* eqState24
) @nogc nothrow {
    if (bufL.length == 0 || bufL.length != bufR.length || !eqState24) return;
    if (sampleRate <= 0.0) return;

    const float lowNorm = clampVal(eqLow, 0.0f, 1.0f);
    const float midNorm = clampVal(eqMid, 0.0f, 1.0f);
    const float highNorm = clampVal(eqHigh, 0.0f, 1.0f);

    if (fabs(lowNorm - 0.5f) > 1e-4f) {
        const double db = (lowNorm - 0.5) * 30.0;
        const auto c = computeLowShelfCoeffs(120.0, 0.7071, db, sampleRate);
        runBiquadStereo(c, bufL, bufR, eqState24 + 0, eqState24 + 4);
    } else {
        foreach (k; 0 .. 8) eqState24[k] = 0.0;
    }
    if (fabs(midNorm - 0.5f) > 1e-4f) {
        const double db = (midNorm - 0.5) * 30.0;
        const auto c = computeBellCoeffs(1000.0, 0.9, db, sampleRate);
        runBiquadStereo(c, bufL, bufR, eqState24 + 8, eqState24 + 12);
    } else {
        foreach (k; 8 .. 16) eqState24[k] = 0.0;
    }
    if (fabs(highNorm - 0.5f) > 1e-4f) {
        const double db = (highNorm - 0.5) * 30.0;
        const auto c = computeHighShelfCoeffs(6500.0, 0.7071, db, sampleRate);
        runBiquadStereo(c, bufL, bufR, eqState24 + 16, eqState24 + 20);
    } else {
        foreach (k; 16 .. 24) eqState24[k] = 0.0;
    }
}

private void storeEqCoeffs(double* dst5, const ref EqCoeffs c) @nogc nothrow {
    dst5[0] = c.b0;
    dst5[1] = c.b1;
    dst5[2] = c.b2;
    dst5[3] = c.a1;
    dst5[4] = c.a2;
}

private EqCoeffs loadEqCoeffs(const(double)* src5) @nogc nothrow {
    EqCoeffs c;
    c.b0 = src5[0];
    c.b1 = src5[1];
    c.b2 = src5[2];
    c.a1 = src5[3];
    c.a2 = src5[4];
    return c;
}

/// Applies 3-band mixer EQ while caching Biquad coefficients inside `eqState48[24..42]`
/// so static EQ curves avoid trigonometric recomputation on every audio block.
void applyTrackEqCached(
    double[] bufL,
    double[] bufR,
    double sampleRate,
    float eqLow,
    float eqMid,
    float eqHigh,
    double* eqState48
) @nogc nothrow {
    if (bufL.length == 0 || bufL.length != bufR.length || !eqState48) return;
    if (sampleRate <= 0.0) return;

    const float lowNorm = clampVal(eqLow, 0.0f, 1.0f);
    const float midNorm = clampVal(eqMid, 0.0f, 1.0f);
    const float highNorm = clampVal(eqHigh, 0.0f, 1.0f);

    const bool srChanged = (eqState48[24] != sampleRate);
    if (srChanged || eqState48[25] != cast(double)lowNorm) {
        if (fabs(lowNorm - 0.5f) > 1e-4f) {
            const double db = (lowNorm - 0.5) * 30.0;
            const auto c = computeLowShelfCoeffs(120.0, 0.7071, db, sampleRate);
            storeEqCoeffs(eqState48 + 28, c);
        }
        eqState48[25] = cast(double)lowNorm;
    }
    if (srChanged || eqState48[26] != cast(double)midNorm) {
        if (fabs(midNorm - 0.5f) > 1e-4f) {
            const double db = (midNorm - 0.5) * 30.0;
            const auto c = computeBellCoeffs(1000.0, 0.9, db, sampleRate);
            storeEqCoeffs(eqState48 + 33, c);
        }
        eqState48[26] = cast(double)midNorm;
    }
    if (srChanged || eqState48[27] != cast(double)highNorm) {
        if (fabs(highNorm - 0.5f) > 1e-4f) {
            const double db = (highNorm - 0.5) * 30.0;
            const auto c = computeHighShelfCoeffs(6500.0, 0.7071, db, sampleRate);
            storeEqCoeffs(eqState48 + 38, c);
        }
        eqState48[27] = cast(double)highNorm;
    }
    eqState48[24] = sampleRate;

    if (fabs(lowNorm - 0.5f) > 1e-4f) {
        const auto c = loadEqCoeffs(eqState48 + 28);
        runBiquadStereo(c, bufL, bufR, eqState48 + 0, eqState48 + 4);
    } else {
        foreach (k; 0 .. 8) eqState48[k] = 0.0;
    }
    if (fabs(midNorm - 0.5f) > 1e-4f) {
        const auto c = loadEqCoeffs(eqState48 + 33);
        runBiquadStereo(c, bufL, bufR, eqState48 + 8, eqState48 + 12);
    } else {
        foreach (k; 8 .. 16) eqState48[k] = 0.0;
    }
    if (fabs(highNorm - 0.5f) > 1e-4f) {
        const auto c = loadEqCoeffs(eqState48 + 38);
        runBiquadStereo(c, bufL, bufR, eqState48 + 16, eqState48 + 20);
    } else {
        foreach (k; 16 .. 24) eqState48[k] = 0.0;
    }
}

/// Applies M/S stereo separation (`-1.0` = mono merge, `0.0` = default, `+1.0` = widened stereo).
void applyStereoSeparation(double[] bufL, double[] bufR, float stereoSep) @nogc nothrow {
    if (bufL.length == 0 || bufL.length != bufR.length) return;
    const float s = clampVal(stereoSep, -1.0f, 1.0f);
    if (fabs(s) < 1e-5f) return;
    const double sideScale = 1.0 + cast(double)s;
    foreach (i; 0 .. bufL.length) {
        const double mid = 0.5 * (bufL[i] + bufR[i]);
        const double side = 0.5 * (bufL[i] - bufR[i]);
        bufL[i] = mid + side * sideScale;
        bufR[i] = mid - side * sideScale;
    }
}

/// Blends `dry` into `wet` in-place on `wet` (`mix = 0.0` -> pure dry, `1.0` -> pure wet).
void blendDryWet(
    const(double)[] dryL,
    const(double)[] dryR,
    double[] wetL,
    double[] wetR,
    float mix
) @nogc nothrow {
    if (wetL.length == 0 || dryL.length != wetL.length || dryR.length != wetR.length ||
        wetL.length != wetR.length) return;
    const float m = clampVal(mix, 0.0f, 1.0f);
    if (m >= 0.9999f) return;
    if (m <= 0.0001f) {
        foreach (i; 0 .. wetL.length) {
            wetL[i] = dryL[i];
            wetR[i] = dryR[i];
        }
        return;
    }
    const double wetGain = cast(double)m;
    const double dryGain = 1.0 - wetGain;
    foreach (i; 0 .. wetL.length) {
        wetL[i] = dryL[i] * dryGain + wetL[i] * wetGain;
        wetR[i] = dryR[i] * dryGain + wetR[i] * wetGain;
    }
}

extern (C) {
    void d_apply_track_eq_and_stereo_sep(
        double* bufL,
        double* bufR,
        int blockSize,
        double sampleRate,
        float eqLow,
        float eqMid,
        float eqHigh,
        float stereoSep,
        double* eqState48
    ) @nogc nothrow {
        if (!bufL || !bufR || blockSize <= 0) return;
        applyTrackEqCached(bufL[0 .. blockSize], bufR[0 .. blockSize], sampleRate,
                           eqLow, eqMid, eqHigh, eqState48);
        applyStereoSeparation(bufL[0 .. blockSize], bufR[0 .. blockSize], stereoSep);
    }

    void d_apply_track_eq_and_stereo_sep_f32(
        float* bufL,
        float* bufR,
        int blockSize,
        double sampleRate,
        float eqLow,
        float eqMid,
        float eqHigh,
        float stereoSep,
        double* eqState48
    ) @nogc nothrow {
        if (!bufL || !bufR || blockSize <= 0) return;
        // Stack-scratch in chunks of up to 512 to avoid any heap allocation.
        double[512] tmpL = void;
        double[512] tmpR = void;
        int offset = 0;
        while (offset < blockSize) {
            int n = blockSize - offset;
            if (n > 512) n = 512;
            foreach (i; 0 .. n) {
                tmpL[i] = bufL[offset + i];
                tmpR[i] = bufR[offset + i];
            }
            applyTrackEqCached(tmpL[0 .. n], tmpR[0 .. n], sampleRate,
                               eqLow, eqMid, eqHigh, eqState48);
            applyStereoSeparation(tmpL[0 .. n], tmpR[0 .. n], stereoSep);
            foreach (i; 0 .. n) {
                bufL[offset + i] = cast(float)tmpL[i];
                bufR[offset + i] = cast(float)tmpR[i];
            }
            offset += n;
        }
    }

    void d_blend_dry_wet(
        const(double)* dryL,
        const(double)* dryR,
        double* wetL,
        double* wetR,
        int blockSize,
        float mix
    ) @nogc nothrow {
        if (!dryL || !dryR || !wetL || !wetR || blockSize <= 0) return;
        blendDryWet(dryL[0 .. blockSize], dryR[0 .. blockSize],
                    wetL[0 .. blockSize], wetR[0 .. blockSize], mix);
    }

    void d_blend_dry_wet_f32(
        const(float)* dryL,
        const(float)* dryR,
        float* wetL,
        float* wetR,
        int blockSize,
        float mix
    ) @nogc nothrow {
        if (!dryL || !dryR || !wetL || !wetR || blockSize <= 0) return;
        const float m = clampVal(mix, 0.0f, 1.0f);
        if (m >= 0.9999f) return;
        if (m <= 0.0001f) {
            foreach (i; 0 .. blockSize) {
                wetL[i] = dryL[i];
                wetR[i] = dryR[i];
            }
            return;
        }
        const float dryGain = 1.0f - m;
        foreach (i; 0 .. blockSize) {
            wetL[i] = dryL[i] * dryGain + wetL[i] * m;
            wetR[i] = dryR[i] * dryGain + wetR[i] * m;
        }
    }
}

