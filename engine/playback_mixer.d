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
        int samplePos = startSample + cast(int)i;
        if (samplePos < 0) continue;
        if (isLoop && loopLen > 0) {
            int rel = (samplePos - trimSamples) % loopLen;
            if (rel < 0) rel += loopLen;
            samplePos = trimSamples + rel;
        }

        double fadeGain = 1.0;
        double sampleTimeInClip = clipLocalTime + cast(double)i * invSr;
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
