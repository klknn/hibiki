package hibiki.android.model;

/**
 * Drum pad definition for Touch Instrument performance.
 */
public final class DrumPadItem {
    private final int index;
    private final String name;
    private final int midiNote;
    private final int color;

    public DrumPadItem(int index, String name, int midiNote, int color) {
        this.index = index;
        this.name = name != null ? name : "";
        this.midiNote = midiNote;
        this.color = color;
    }

    public int getIndex() {
        return index;
    }

    public String getName() {
        return name;
    }

    public int getMidiNote() {
        return midiNote;
    }

    public int getColor() {
        return color;
    }
}
