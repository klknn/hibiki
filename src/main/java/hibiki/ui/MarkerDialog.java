package hibiki.ui;

import java.awt.*;
import javax.swing.*;

/** Modal dialog for creating/editing timeline markers. */
class MarkerDialog extends JDialog {
  private final JTextField nameField;
  private final JTextField positionField;
  private final JTextField bpmField;
  private final JTextField timeSigField;
  private boolean confirmed = false;

  // Output values
  String markerName;
  float positionSec;
  float bpm; // 0 = no override
  int beatsPerBar; // 0 = no override
  int beatDenominator; // 0 = no override

  MarkerDialog(
      Frame owner,
      String title,
      String name,
      float posSec,
      float bpm,
      int beatsPerBar,
      int beatDenominator,
      float currentBpm) {
    super(owner, title, true);
    this.markerName = name;
    this.positionSec = posSec;
    this.bpm = bpm;
    this.beatsPerBar = beatsPerBar;
    this.beatDenominator = beatDenominator;

    JPanel content = new JPanel(new GridBagLayout());
    content.setBorder(BorderFactory.createEmptyBorder(10, 10, 10, 10));
    content.setBackground(Theme.getInstance().BG_DARK);
    GridBagConstraints c = new GridBagConstraints();
    c.insets = new Insets(4, 4, 4, 4);
    c.anchor = GridBagConstraints.WEST;

    // Name
    c.gridx = 0;
    c.gridy = 0;
    JLabel nameLabel = new JLabel("Name:");
    nameLabel.setForeground(Theme.getInstance().TEXT_BRIGHT);
    content.add(nameLabel, c);
    c.gridx = 1;
    c.fill = GridBagConstraints.HORIZONTAL;
    c.weightx = 1;
    nameField = createField(name);
    content.add(nameField, c);

    // Position
    c.gridx = 0;
    c.gridy = 1;
    c.fill = GridBagConstraints.NONE;
    c.weightx = 0;
    JLabel posLabel = new JLabel("Position (sec):");
    posLabel.setForeground(Theme.getInstance().TEXT_BRIGHT);
    content.add(posLabel, c);
    c.gridx = 1;
    c.fill = GridBagConstraints.HORIZONTAL;
    c.weightx = 1;
    positionField = createField(String.format("%.2f", posSec));
    content.add(positionField, c);

    // BPM override
    c.gridx = 0;
    c.gridy = 2;
    c.fill = GridBagConstraints.NONE;
    c.weightx = 0;
    JLabel bpmLabel = new JLabel("BPM (empty=inherit):");
    bpmLabel.setForeground(Theme.getInstance().TEXT_BRIGHT);
    content.add(bpmLabel, c);
    c.gridx = 1;
    c.fill = GridBagConstraints.HORIZONTAL;
    c.weightx = 1;
    bpmField = createField(bpm > 0 ? String.format("%.1f", bpm) : "");
    content.add(bpmField, c);

    // Time sig override
    c.gridx = 0;
    c.gridy = 3;
    c.fill = GridBagConstraints.NONE;
    c.weightx = 0;
    JLabel tsLabel = new JLabel("Time Sig (e.g. 3/4):");
    tsLabel.setForeground(Theme.getInstance().TEXT_BRIGHT);
    content.add(tsLabel, c);
    c.gridx = 1;
    c.fill = GridBagConstraints.HORIZONTAL;
    c.weightx = 1;
    String tsText =
        (beatsPerBar > 0 && beatDenominator > 0) ? beatsPerBar + "/" + beatDenominator : "";
    timeSigField = createField(tsText);
    content.add(timeSigField, c);

    // Buttons
    JPanel buttons = new JPanel(new FlowLayout(FlowLayout.RIGHT));
    buttons.setBackground(Theme.getInstance().BG_DARK);
    JButton okBtn =
        Theme.getInstance()
            .createButton(
                "OK",
                e -> {
                  parseFields();
                  confirmed = true;
                  dispose();
                });
    JButton cancelBtn = Theme.getInstance().createButton("Cancel", e -> dispose());
    buttons.add(okBtn);
    buttons.add(cancelBtn);

    c.gridx = 0;
    c.gridy = 4;
    c.gridwidth = 2;
    c.fill = GridBagConstraints.HORIZONTAL;
    content.add(buttons, c);

    setContentPane(content);
    pack();
    setMinimumSize(new Dimension(300, 200));
    setLocationRelativeTo(owner);
    nameField.requestFocusInWindow();
  }

  private JTextField createField(String text) {
    JTextField field = new JTextField(text, 15);
    field.setBackground(Theme.getInstance().PANEL_BG_LIGHT);
    field.setForeground(Theme.getInstance().TEXT_BRIGHT);
    field.setCaretColor(Theme.getInstance().TEXT_BRIGHT);
    field.setBorder(BorderFactory.createLineBorder(Theme.getInstance().BORDER));
    return field;
  }

  private void parseFields() {
    markerName = nameField.getText().trim();
    if (markerName.isEmpty()) markerName = "Marker";

    try {
      positionSec = Float.parseFloat(positionField.getText().trim());
    } catch (NumberFormatException e) {
      /* keep existing */
    }

    String bpmText = bpmField.getText().trim();
    if (bpmText.isEmpty()) {
      bpm = 0;
    } else {
      try {
        bpm = Float.parseFloat(bpmText);
      } catch (NumberFormatException e) {
        bpm = 0;
      }
    }

    String tsText = timeSigField.getText().trim().replaceAll("\\s", "");
    if (tsText.isEmpty()) {
      beatsPerBar = 0;
      beatDenominator = 0;
    } else {
      String[] parts = tsText.split("/");
      if (parts.length == 2) {
        try {
          beatsPerBar = Integer.parseInt(parts[0]);
          beatDenominator = Integer.parseInt(parts[1]);
        } catch (NumberFormatException e) {
          beatsPerBar = 0;
          beatDenominator = 0;
        }
      }
    }
  }

  boolean isConfirmed() {
    return confirmed;
  }
}
