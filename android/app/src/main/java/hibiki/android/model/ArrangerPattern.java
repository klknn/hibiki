package hibiki.android.model;

/**
 * Arrangement pattern block in FL Mobile style Arranger timeline.
 */
public final class ArrangerPattern {
    private final String id;
    private final int trackIndex;
    private final float startBar;
    private final float lengthBars;
    private final String name;
    private final int color;

    public ArrangerPattern(
            String id, int trackIndex, float startBar, float lengthBars, String name, int color) {
        this.id = id != null ? id : "";
        this.trackIndex = trackIndex;
        this.startBar = startBar;
        this.lengthBars = lengthBars;
        this.name = name != null ? name : "";
        this.color = color;
    }

    public String getId() {
        return id;
    }

    public int getTrackIndex() {
        return trackIndex;
    }

    public float getStartBar() {
        return startBar;
    }

    public float getLengthBars() {
        return lengthBars;
    }

    public String getName() {
        return name;
    }

    public int getColor() {
        return color;
    }
}
