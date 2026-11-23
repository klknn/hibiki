package hibiki.android.model;

import java.util.Objects;

/**
 * Immutable musical note event at a specific step in a sequence.
 */
public final class MelodyStep {
    private final boolean active;
    private final int pitch; // MIDI note number 0..127 (e.g. 60 = Middle C / C4)
    private final float velocity; // 0.0f to 1.0f
    private final float gate; // Duration ratio of step [0.1f, 1.0f]

    public MelodyStep(boolean active, int pitch, float velocity, float gate) {
        this.active = active;
        this.pitch = Math.max(0, Math.min(127, pitch));
        float vel = Float.isNaN(velocity) ? 0.8f : velocity;
        float g = Float.isNaN(gate) ? 0.8f : gate;
        this.velocity = Math.max(0.0f, Math.min(1.0f, vel));
        this.gate = Math.max(0.05f, Math.min(1.0f, g));
    }

    public static MelodyStep empty() {
        return new MelodyStep(false, 60, 0.8f, 0.8f);
    }

    public static MelodyStep of(int pitch, float velocity) {
        return new MelodyStep(true, pitch, velocity, 0.8f);
    }

    public boolean isActive() {
        return active;
    }

    public int getPitch() {
        return pitch;
    }

    public float getVelocity() {
        return velocity;
    }

    public float getGate() {
        return gate;
    }

    public MelodyStep withActive(boolean newActive) {
        return new MelodyStep(newActive, pitch, velocity, gate);
    }

    public MelodyStep withPitch(int newPitch) {
        return new MelodyStep(active, newPitch, velocity, gate);
    }

    public MelodyStep withVelocity(float newVelocity) {
        return new MelodyStep(active, pitch, newVelocity, gate);
    }

    public MelodyStep withGate(float newGate) {
        return new MelodyStep(active, pitch, velocity, newGate);
    }

    /**
     * Converts MIDI note number to musical note name (e.g. 60 -> "C4", 61 -> "C#4").
     */
    public String getNoteName() {
        return pitchToNoteName(pitch);
    }

    private static final String[] NOTE_NAMES = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    public static String pitchToNoteName(int midiPitch) {
        int noteIndex = midiPitch % 12;
        if (noteIndex < 0) noteIndex += 12;
        return NOTE_NAMES[noteIndex] + ((midiPitch / 12) - 1);
    }

    /**
     * Converts musical octave and semitone offset (0=C, 1=C#, ..., 11=B) to standard MIDI note number.
     * e.g., octave 4, offset 0 (C) -> 60 (Middle C / C4).
     */
    public static int octaveAndOffsetToPitch(int octave, int semitoneOffset) {
        return Math.max(0, Math.min(127, (octave + 1) * 12 + semitoneOffset));
    }

    /**
     * Calculates fundamental frequency in Hertz for this note's pitch.
     */
    public double getFrequencyHz() {
        return pitchToFrequencyHz(pitch);
    }

    public static double pitchToFrequencyHz(int midiPitch) {
        return 440.0 * Math.pow(2.0, (midiPitch - 69) / 12.0);
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof MelodyStep)) return false;
        MelodyStep that = (MelodyStep) o;
        return active == that.active
                && pitch == that.pitch
                && Float.compare(that.velocity, velocity) == 0
                && Float.compare(that.gate, gate) == 0;
    }

    @Override
    public int hashCode() {
        return Objects.hash(active, pitch, velocity, gate);
    }

    @Override
    public String toString() {
        return "MelodyStep{" + (active ? getNoteName() : "REST") + ", vel=" + velocity + "}";
    }
}
