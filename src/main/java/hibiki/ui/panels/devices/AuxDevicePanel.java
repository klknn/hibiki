package hibiki.ui.panels.devices;

import hibiki.ui.PluginPane;
import hibiki.ui.Theme;
import hibiki.ui.panels.KnobPanel;
import java.awt.*;
import java.awt.event.*;
import javax.swing.*;

/** Aux return device panel with Gain and Pan arc-knobs. */
public class AuxDevicePanel extends AbstractDevicePanel {
  private static final int PARAM_GAIN = 0;
  private static final int PARAM_PAN = 1;
  private static final int TOTAL_PARAMS = 2;

  private static final Color ACCENT = new Color(0x66BB6A);

  private final KnobPanel[] knobs = new KnobPanel[2];

  public AuxDevicePanel(int trackIndex, int pluginIndex) {
    super(trackIndex, pluginIndex, TOTAL_PARAMS);

    params[PARAM_GAIN] = 0.75;
    params[PARAM_PAN] = 0.5;

    Theme theme = Theme.getInstance();
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(theme.scale(160), theme.scale(180)));
    setMaximumSize(new Dimension(theme.scale(160), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Header
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x2E7D32));
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));

    JLabel nameLabel = new JLabel("Aux Return");
    nameLabel.setForeground(Color.WHITE);
    nameLabel.setFont(theme.FONT_UI_BOLD);
    header.add(nameLabel, BorderLayout.CENTER);

    JPanel btnPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT, 2, 0));
    btnPanel.setOpaque(false);
    JButton modBtn = new JButton("Mod");
    modBtn.addActionListener(
        e -> {
          if (modToggleCallback != null) modToggleCallback.run();
        });
    btnPanel.add(modBtn);

    JButton scBtn = new JButton("SC");
    scBtn.setToolTipText("Sidechain Source");
    scBtn.addActionListener(e -> PluginPane.showSidechainPopup(scBtn, trackIndex, pluginIndex));
    btnPanel.add(scBtn);
    JButton delBtn = new JButton("\u274C");
    delBtn.addActionListener(e -> sendRemove());
    btnPanel.add(delBtn);
    header.add(btnPanel, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // Knobs
    JPanel knobRow = new JPanel(new GridLayout(1, 2, theme.scale(2), 0));
    knobRow.setBackground(theme.BG_DARK);
    knobRow.setPreferredSize(new Dimension(0, theme.scale(68)));
    knobRow.setBorder(
        BorderFactory.createEmptyBorder(
            theme.scale(4), theme.scale(6), theme.scale(4), theme.scale(6)));

    knobs[0] = new KnobPanel("Gain", params[PARAM_GAIN], makeFormatter(PARAM_GAIN), ACCENT);
    knobs[0].addChangeListener(
        e -> {
          if (updatingFromBackend) return;
          params[PARAM_GAIN] = knobs[0].getValue();
          sendParam(PARAM_GAIN, params[PARAM_GAIN]);
        });
    knobRow.add(knobs[0]);

    knobs[1] = new KnobPanel("Pan", params[PARAM_PAN], makeFormatter(PARAM_PAN), ACCENT);
    knobs[1].addChangeListener(
        e -> {
          if (updatingFromBackend) return;
          params[PARAM_PAN] = knobs[1].getValue();
          sendParam(PARAM_PAN, params[PARAM_PAN]);
        });
    knobRow.add(knobs[1]);

    add(knobRow, BorderLayout.CENTER);
  }

  private ValueFormatter makeFormatter(int paramId) {
    return norm -> {
      if (paramId == PARAM_GAIN) {
        float db = (float) (norm * 48.0 - 48.0);
        return db <= -47.9 ? "-∞ dB" : String.format("%.1f dB", db);
      }
      if (paramId == PARAM_PAN) {
        if (Math.abs(norm - 0.5) < 0.01) return "C";
        return norm < 0.5
            ? String.format("%.0fL", (0.5 - norm) * 200)
            : String.format("%.0fR", (norm - 0.5) * 200);
      }
      return String.format("%.2f", norm);
    };
  }

  public void updateParam(int paramId, double value) {
    if (paramId < 0 || paramId >= TOTAL_PARAMS) return;
    updatingFromBackend = true;
    params[paramId] = value;
    if (paramId < knobs.length && knobs[paramId] != null) {
      knobs[paramId].setValue(value);
    }
    updatingFromBackend = false;
  }
}
