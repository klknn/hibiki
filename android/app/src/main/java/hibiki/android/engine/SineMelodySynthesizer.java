package hibiki.android.engine;

import hibiki.android.model.MelodySequence;
import hibiki.android.model.MelodyStep;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Pure, sample-accurate sine wave melody synthesizer and sequencer.
 * Completely free of Android dependencies; 100% testable on pure JVM.
 */
public final class SineMelodySynthesizer {
    public static final double DEFAULT_SAMPLE_RATE = 44100.0;
    public static final double DEFAULT_BPM = 128.0;

    private static final class Voice {
        double phase = 0.0;
        double freq = 440.0;
        float velocity = 0.8f;
        float envelope = 0.0f;

        void reset() {
            phase = 0.0;
            envelope = 0.0f;
        }

        float render(boolean active, float attackDelta, float releaseDelta, double sampleRate) {
            envelope = active
                    ? Math.min(1.0f, envelope + attackDelta)
                    : Math.max(0.0f, envelope - releaseDelta);
            if (envelope <= 0.0f) {
                return 0.0f;
            }
            float out = (float) (Math.sin(phase) * velocity * envelope * 0.7);
            phase += (2.0 * Math.PI * freq) / sampleRate;
            if (phase >= 2.0 * Math.PI) {
                phase %= (2.0 * Math.PI);
            }
            return out;
        }
    }

    private final double sampleRate;
    private volatile double bpm;
    private final AtomicReference<MelodySequence> sequenceRef = new AtomicReference<>(new MelodySequence());
    private final AtomicBoolean isPlaying = new AtomicBoolean(false);

    // Sample-accurate clock & sequence playback state
    private long totalSamplesPlayed = 0L;
    private volatile int currentStepIndex = 0;
    private int activeNoteStep = -1;
    private int lastStepIdx = -1;
    private long noteStartSample = 0L;
    private long noteDurationSamples = 0L;

    // Voices
    private final Voice seqVoice = new Voice();
    private final Voice audVoice = new Voice();
    private volatile boolean isAuditioning = false;

    public SineMelodySynthesizer() {
        this(DEFAULT_SAMPLE_RATE, DEFAULT_BPM);
    }

    public SineMelodySynthesizer(double sampleRate, double initialBpm) {
        this.sampleRate = sampleRate > 8000.0 ? sampleRate : DEFAULT_SAMPLE_RATE;
        this.bpm = initialBpm > 20.0 && initialBpm < 300.0 ? initialBpm : DEFAULT_BPM;
    }

    public double getSampleRate() {
        return sampleRate;
    }

    public double getBpm() {
        return bpm;
    }

    public void setBpm(double newBpm) {
        if (newBpm >= 20.0 && newBpm <= 300.0) {
            this.bpm = newBpm;
        }
    }

    public boolean isPlaying() {
        return isPlaying.get();
    }

    public void setPlaying(boolean play) {
        isPlaying.set(play);
        if (!play) {
            activeNoteStep = -1;
        }
    }

    public void resetPlayback() {
        totalSamplesPlayed = 0L;
        currentStepIndex = 0;
        lastStepIdx = -1;
        activeNoteStep = -1;
        seqVoice.reset();
    }

    public MelodySequence getSequence() {
        return sequenceRef.get();
    }

    public void setSequence(MelodySequence sequence) {
        if (sequence != null) {
            sequenceRef.set(sequence);
        }
    }

    public int getCurrentStepIndex() {
        return currentStepIndex;
    }

    public long getTotalSamplesPlayed() {
        return totalSamplesPlayed;
    }

    /**
     * Starts playing a live audition tone (e.g. user pressed a piano key).
     */
    public void triggerAudition(int pitch, float velocity) {
        int clampedPitch = Math.max(0, Math.min(127, pitch));
        audVoice.freq = MelodyStep.pitchToFrequencyHz(clampedPitch);
        audVoice.velocity = Float.isNaN(velocity) ? 0.8f : Math.max(0.1f, Math.min(1.0f, velocity));
        isAuditioning = true;
    }

    /**
     * Releases the live audition tone with smooth release.
     */
    public void releaseAudition() {
        isAuditioning = false;
    }

    /**
     * Renders stereo audio samples block-by-block.
     */
    public void render(float[] bufferL, float[] bufferR, int numFrames) {
        if (bufferL == null || bufferR == null || numFrames <= 0) {
            return;
        }
        int available = Math.min(numFrames, Math.min(bufferL.length, bufferR.length));

        MelodySequence seq = sequenceRef.get();
        int stepCount = seq.getLengthSteps();
        double currentBpm = this.bpm;

        double samplesPer16th = (sampleRate * 60.0) / (currentBpm * 4.0);
        long samplesPerLoop = Math.max(1, (long) (samplesPer16th * stepCount));

        // Attack: 4ms, Release: 15ms
        float attackDelta = (float) (1.0 / (sampleRate * 0.004));
        float releaseDelta = (float) (1.0 / (sampleRate * 0.015));

        boolean playing = isPlaying.get();

        for (int i = 0; i < available; i++) {
            boolean seqActive = false;

            if (playing) {
                long loopSamplePos = totalSamplesPlayed % samplesPerLoop;
                int stepIdx = (int) (loopSamplePos / samplesPer16th);
                this.currentStepIndex = Math.min(stepIdx, stepCount - 1);

                if (stepIdx != lastStepIdx) {
                    lastStepIdx = stepIdx;
                    MelodyStep step = seq.getStep(stepIdx);
                    if (step.isActive()) {
                        activeNoteStep = stepIdx;
                        seqVoice.freq = step.getFrequencyHz();
                        seqVoice.velocity = step.getVelocity();
                        noteStartSample = totalSamplesPlayed;
                        noteDurationSamples = (long) (samplesPer16th * step.getGate());
                    } else {
                        activeNoteStep = -1;
                    }
                }

                if (activeNoteStep >= 0) {
                    seqActive = (totalSamplesPlayed - noteStartSample) < noteDurationSamples;
                }

                totalSamplesPlayed++;
                long nextLoopPos = totalSamplesPlayed % samplesPerLoop;
                int nextStepIdx = (int) (nextLoopPos / samplesPer16th);
                this.currentStepIndex = nextStepIdx >= stepCount ? 0 : nextStepIdx;
            }

            float sampleOut = seqVoice.render(seqActive, attackDelta, releaseDelta, sampleRate)
                    + audVoice.render(isAuditioning, attackDelta, releaseDelta, sampleRate);

            float mixedSample = softClip(sampleOut);
            bufferL[i] = mixedSample;
            bufferR[i] = mixedSample;
        }
    }

    private static float softClip(float in) {
        if (Float.isNaN(in)) return 0.0f;
        if (in > 1.0f) return 1.0f;
        if (in < -1.0f) return -1.0f;
        return (float) (1.5 * in - 0.5 * in * in * in);
    }
}
