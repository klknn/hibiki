package hibiki.ui;

import hibiki.BackendManager;
import java.awt.KeyEventDispatcher;
import java.awt.KeyboardFocusManager;
import java.awt.event.KeyEvent;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * Virtual MIDI keyboard that maps PC keyboard keys to MIDI notes.
 *
 * <p>Layout (1 octave, DAW-style):
 *
 * <pre>
 *   White keys: A=C, S=D, D=E, F=F, G=G, H=A, J=B
 *   Black keys: W=C#, E=D#, T=F#, Y=G#, U=A#
 * </pre>
 *
 * Special keys: Z=octave−1, X=octave+1, C=velocity−10, V=velocity+10
 */
public class VirtualKeyboard implements KeyEventDispatcher {
  private static final int DEFAULT_VELOCITY = 100;
  private static final int DEFAULT_OCTAVE = 3; // C3 = MIDI 48

  // Key → semitone offset (0=C, 1=C#, ..., 11=B)
  private static final Map<Integer, Integer> NOTE_KEYS = new HashMap<>();

  static {
    NOTE_KEYS.put(KeyEvent.VK_A, 0); // C
    NOTE_KEYS.put(KeyEvent.VK_W, 1); // C#
    NOTE_KEYS.put(KeyEvent.VK_S, 2); // D
    NOTE_KEYS.put(KeyEvent.VK_E, 3); // D#
    NOTE_KEYS.put(KeyEvent.VK_D, 4); // E
    NOTE_KEYS.put(KeyEvent.VK_F, 5); // F
    NOTE_KEYS.put(KeyEvent.VK_T, 6); // F#
    NOTE_KEYS.put(KeyEvent.VK_G, 7); // G
    NOTE_KEYS.put(KeyEvent.VK_Y, 8); // G#
    NOTE_KEYS.put(KeyEvent.VK_H, 9); // A
    NOTE_KEYS.put(KeyEvent.VK_U, 10); // A#
    NOTE_KEYS.put(KeyEvent.VK_J, 11); // B
  }

  private boolean enabled = false;
  private int octave = DEFAULT_OCTAVE;
  private int velocity = DEFAULT_VELOCITY;
  private int targetTrackIndex = 0;
  private final Set<Integer> activeNotes = new HashSet<>();

  public VirtualKeyboard() {
    KeyboardFocusManager.getCurrentKeyboardFocusManager().addKeyEventDispatcher(this);
  }

  public void setEnabled(boolean enabled) {
    if (!enabled) {
      // Send note-off for all active notes
      for (int note : activeNotes) {
        BackendManager.getInstance().sendVirtualMidi(targetTrackIndex, note, 0, false);
      }
      activeNotes.clear();
    }
    this.enabled = enabled;
  }

  public boolean isEnabled() {
    return enabled;
  }

  public void setTargetTrackIndex(int trackIndex) {
    this.targetTrackIndex = trackIndex;
  }

  public int getOctave() {
    return octave;
  }

  public int getVelocity() {
    return velocity;
  }

  @Override
  public boolean dispatchKeyEvent(KeyEvent e) {
    if (!enabled) return false;
    if (e.getID() != KeyEvent.KEY_PRESSED && e.getID() != KeyEvent.KEY_RELEASED) return false;

    int keyCode = e.getKeyCode();

    // Special keys (only on press)
    if (e.getID() == KeyEvent.KEY_PRESSED) {
      switch (keyCode) {
        case KeyEvent.VK_Z:
          octave = Math.max(0, octave - 1);
          return true;
        case KeyEvent.VK_X:
          octave = Math.min(8, octave + 1);
          return true;
        case KeyEvent.VK_C:
          velocity = Math.max(10, velocity - 10);
          return true;
        case KeyEvent.VK_V:
          velocity = Math.min(127, velocity + 10);
          return true;
      }
    }

    // Map key to MIDI note
    Integer semitone = NOTE_KEYS.get(keyCode);
    if (semitone == null) return false;
    int midiNote = (octave * 12) + semitone;

    if (e.getID() == KeyEvent.KEY_PRESSED) {
      // Avoid key-repeat duplicates
      if (activeNotes.contains(midiNote)) return true;
      activeNotes.add(midiNote);
      BackendManager.getInstance().sendVirtualMidi(targetTrackIndex, midiNote, velocity, true);
      return true;
    } else if (e.getID() == KeyEvent.KEY_RELEASED) {
      if (!activeNotes.contains(midiNote)) return true;
      activeNotes.remove(midiNote);
      BackendManager.getInstance().sendVirtualMidi(targetTrackIndex, midiNote, 0, false);
      return true;
    }
    return false;
  }
}
