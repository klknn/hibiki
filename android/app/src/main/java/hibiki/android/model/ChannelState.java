package hibiki.android.model;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Single channel in Tracker and Mixer.
 */
public final class ChannelState {
    private final int index;
    private final String name;
    private final int color;
    private final float volume;
    private final float pan;
    private final boolean isMuted;
    private final boolean isSoloed;
    private final List<TrackerCell> steps;

    public ChannelState(
            int index,
            String name,
            int color,
            float volume,
            float pan,
            boolean isMuted,
            boolean isSoloed,
            List<TrackerCell> steps) {
        this.index = index;
        this.name = name != null ? name : "";
        this.color = color;
        this.volume = volume;
        this.pan = pan;
        this.isMuted = isMuted;
        this.isSoloed = isSoloed;
        if (steps != null) {
            this.steps = Collections.unmodifiableList(new ArrayList<>(steps));
        } else {
            List<TrackerCell> defaultSteps = new ArrayList<>();
            for (int i = 0; i < 16; i++) {
                defaultSteps.add(new TrackerCell());
            }
            this.steps = Collections.unmodifiableList(defaultSteps);
        }
    }

    public ChannelState(int index, String name, int color) {
        this(index, name, color, 0.8f, 0.0f, false, false, null);
    }

    public int getIndex() {
        return index;
    }

    public String getName() {
        return name;
    }

    public int getColor() {
        return color;
    }

    public float getVolume() {
        return volume;
    }

    public float getPan() {
        return pan;
    }

    public boolean isMuted() {
        return isMuted;
    }

    public boolean isSoloed() {
        return isSoloed;
    }

    public List<TrackerCell> getSteps() {
        return steps;
    }

    public ChannelState withVolume(float newVolume) {
        return new ChannelState(index, name, color, newVolume, pan, isMuted, isSoloed, steps);
    }

    public ChannelState withPan(float newPan) {
        return new ChannelState(index, name, color, volume, newPan, isMuted, isSoloed, steps);
    }

    public ChannelState withMuted(boolean muted) {
        return new ChannelState(index, name, color, volume, pan, muted, isSoloed, steps);
    }

    public ChannelState withSoloed(boolean soloed) {
        return new ChannelState(index, name, color, volume, pan, isMuted, soloed, steps);
    }

    public ChannelState withSteps(List<TrackerCell> newSteps) {
        return new ChannelState(index, name, color, volume, pan, isMuted, isSoloed, newSteps);
    }

    public ChannelState withStep(int stepIndex, TrackerCell newCell) {
        List<TrackerCell> updated = new ArrayList<>(steps);
        while (updated.size() <= stepIndex) {
            updated.add(new TrackerCell());
        }
        updated.set(stepIndex, newCell);
        return new ChannelState(index, name, color, volume, pan, isMuted, isSoloed, updated);
    }
}
