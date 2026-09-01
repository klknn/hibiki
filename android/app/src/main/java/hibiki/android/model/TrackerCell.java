package hibiki.android.model;

import java.util.Locale;

/**
 * Tracker step event (single cell in Naive Tracker / vertical sequencer matrix).
 */
public final class TrackerCell {
    private final String note;
    private final int octave;
    private final int velocity;
    private final int instrumentId;
    private final String effectCmd;
    private final boolean isActive;

    public TrackerCell(
            String note, int octave, int velocity, int instrumentId, String effectCmd, boolean isActive) {
        this.note = note != null ? note : "---";
        this.octave = octave;
        this.velocity = velocity;
        this.instrumentId = instrumentId;
        this.effectCmd = effectCmd != null ? effectCmd : "00";
        this.isActive = isActive;
    }

    public TrackerCell() {
        this("---", 4, 100, 0, "00", false);
    }

    public String getNote() {
        return note;
    }

    public int getOctave() {
        return octave;
    }

    public int getVelocity() {
        return velocity;
    }

    public int getInstrumentId() {
        return instrumentId;
    }

    public String getEffectCmd() {
        return effectCmd;
    }

    public boolean isActive() {
        return isActive;
    }

    public String getDisplayNote() {
        if ("---".equals(note)) {
            return "···";
        }
        if ("===".equals(note) || "OFF".equals(note)) {
            return "===";
        }
        return note + octave;
    }

    public String getDisplayVel() {
        if (!isActive) {
            return "··";
        }
        return String.format(Locale.US, "%02X", velocity);
    }

    public String getDisplayInst() {
        if (!isActive) {
            return "··";
        }
        return String.format(Locale.US, "%02X", instrumentId);
    }

    public String getDisplayFx() {
        if (!isActive) {
            return "··";
        }
        return effectCmd;
    }
}
