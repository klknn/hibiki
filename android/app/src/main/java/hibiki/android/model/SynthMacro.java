package hibiki.android.model;

/**
 * Macro parameter for real-time touch tweaking.
 */
public final class SynthMacro {
    private final String id;
    private final String name;
    private final float value;
    private final float min;
    private final float max;
    private final String unit;

    public SynthMacro(String id, String name, float value, float min, float max, String unit) {
        this.id = id != null ? id : "";
        this.name = name != null ? name : "";
        this.value = value;
        this.min = min;
        this.max = max;
        this.unit = unit != null ? unit : "";
    }

    public SynthMacro(String id, String name, float value) {
        this(id, name, value, 0.0f, 1.0f, "");
    }

    public String getId() {
        return id;
    }

    public String getName() {
        return name;
    }

    public float getValue() {
        return value;
    }

    public float getMin() {
        return min;
    }

    public float getMax() {
        return max;
    }

    public String getUnit() {
        return unit;
    }

    public SynthMacro withValue(float newValue) {
        return new SynthMacro(id, name, newValue, min, max, unit);
    }
}
