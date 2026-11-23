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

    /**
     * Calculates MIDI note number for this tracker cell.
     * Returns -1 if cell is inactive, note-off, or unparsed.
     */
    public int getMidiNote() {
        if (!isActive || "---".equals(note) || "OFF".equals(note) || "===".equals(note)) {
            return -1;
        }

        // Drum pad instrument mapping (Kick=36, Snare=38, ClpHat=42, OpnHat=46, etc.)
        if (octave == 3 && instrumentId >= 0 && instrumentId <= 7) {
            switch (instrumentId) {
                case 0: return 36; // Kick
                case 1: return 38; // Snare
                case 2: return 42; // Clp-Hat
                case 3: return 46; // Opn-Hat
                case 4: return 39; // Clap
                case 5: return 45; // Tom
                case 6: return 56; // Perc
                case 7: return 49; // FX Hit
                default: break;
            }
        }

        int base = -1;
        String n = note.toUpperCase(Locale.US);
        if (n.startsWith("C#") || n.startsWith("DB")) base = 1;
        else if (n.startsWith("C")) base = 0;
        else if (n.startsWith("D#") || n.startsWith("EB")) base = 3;
        else if (n.startsWith("D")) base = 2;
        else if (n.startsWith("E")) base = 4;
        else if (n.startsWith("F#") || n.startsWith("GB")) base = 6;
        else if (n.startsWith("F")) base = 5;
        else if (n.startsWith("G#") || n.startsWith("AB")) base = 8;
        else if (n.startsWith("G")) base = 7;
        else if (n.startsWith("A#") || n.startsWith("BB")) base = 10;
        else if (n.startsWith("A")) base = 9;
        else if (n.startsWith("B")) base = 11;

        if (base < 0) return -1;
        int result = (octave + 1) * 12 + base;
        return Math.max(0, Math.min(127, result));
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
