package hibiki.ui;

import java.awt.*;
import java.awt.event.*;
import javax.swing.*;

/**
 * Non-modal dialog wrapper for AudioEditorPanel. Opened via Ctrl+E, the menu bar, or right-click
 * "Edit in Audio Editor" on audio clips.
 */
public class AudioEditorDialog extends JDialog {
  private final AudioEditorPanel editorPanel;

  /** Open a blank audio editor. */
  public AudioEditorDialog(Frame owner) {
    this(owner, null, -1, -1);
  }

  /**
   * Open the audio editor with a clip pre-loaded for in-place editing.
   *
   * @param owner parent frame
   * @param clipPath audio file path to load, or null
   * @param trackIdx source track index, or -1
   * @param clipIdx source clip/slot index, or -1
   */
  public AudioEditorDialog(Frame owner, String clipPath, int trackIdx, int clipIdx) {
    super(owner, "Audio Editor (Edison)", false); // non-modal
    editorPanel = new AudioEditorPanel(clipPath, trackIdx, clipIdx);
    setContentPane(editorPanel);
    setSize(900, 500);
    setLocationRelativeTo(owner);

    // Dispose cleanup
    setDefaultCloseOperation(DISPOSE_ON_CLOSE);
    addWindowListener(
        new WindowAdapter() {
          @Override
          public void windowClosing(WindowEvent e) {
            editorPanel.dispose();
          }
        });

    // Escape to close
    getRootPane()
        .registerKeyboardAction(
            e -> {
              editorPanel.dispose();
              dispose();
            },
            KeyStroke.getKeyStroke(KeyEvent.VK_ESCAPE, 0),
            JComponent.WHEN_IN_FOCUSED_WINDOW);
  }
}
