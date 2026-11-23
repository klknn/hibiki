package hibiki.android.model;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

/**
 * A sequence of musical steps (typically 16 steps = 1 bar of 16th notes).
 * Thread-safe for atomic reads and modifications.
 */
public final class MelodySequence {
    public static final int DEFAULT_STEP_COUNT = 16;
    private final List<MelodyStep> steps;

    public MelodySequence() {
        this(DEFAULT_STEP_COUNT);
    }

    public MelodySequence(int stepCount) {
        this(createEmptySteps(Math.max(1, stepCount)));
    }

    public MelodySequence(List<MelodyStep> steps) {
        this.steps = Collections.unmodifiableList(
                (steps == null || steps.isEmpty())
                        ? createEmptySteps(DEFAULT_STEP_COUNT)
                        : new ArrayList<>(steps));
    }

    private static List<MelodyStep> createEmptySteps(int count) {
        List<MelodyStep> list = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            list.add(MelodyStep.empty());
        }
        return list;
    }

    public int getLengthSteps() {
        return steps.size();
    }

    public MelodyStep getStep(int index) {
        if (index < 0 || index >= steps.size()) {
            return MelodyStep.empty();
        }
        return steps.get(index);
    }

    public List<MelodyStep> getSteps() {
        return steps;
    }

    /**
     * Returns a new sequence with the step at index updated.
     */
    public MelodySequence withStep(int index, MelodyStep step) {
        if (index < 0 || index >= steps.size()) {
            return this;
        }
        List<MelodyStep> updated = new ArrayList<>(steps);
        updated.set(index, step != null ? step : MelodyStep.empty());
        return new MelodySequence(updated);
    }

    /**
     * Toggles step active state, initializing with defaultPitch if turning on.
     */
    public MelodySequence toggleStep(int index, int defaultPitch) {
        if (index < 0 || index >= steps.size()) {
            return this;
        }
        MelodyStep current = steps.get(index);
        MelodyStep updated = current.isActive()
                ? current.withActive(false)
                : current.withActive(true).withPitch(defaultPitch);
        return withStep(index, updated);
    }

    /**
     * Clears all steps (turning them off).
     */
    public MelodySequence clear() {
        return new MelodySequence(steps.size());
    }

    /**
     * Creates a demo sequence (e.g. C major pentatonic groove).
     */
    public static MelodySequence createDemoMelody() {
        MelodySequence seq = new MelodySequence(DEFAULT_STEP_COUNT);
        int[] pitches = {60, 62, 64, 67, 69, 67, 64, 62};
        float[] vels = {0.9f, 0.75f, 0.85f, 0.95f, 0.9f, 0.8f, 0.85f, 0.7f};
        for (int i = 0; i < pitches.length; i++) {
            seq = seq.withStep(i * 2, MelodyStep.of(pitches[i], vels[i]));
        }
        return seq;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof MelodySequence)) return false;
        MelodySequence that = (MelodySequence) o;
        return Objects.equals(steps, that.steps);
    }

    @Override
    public int hashCode() {
        return Objects.hash(steps);
    }
}
