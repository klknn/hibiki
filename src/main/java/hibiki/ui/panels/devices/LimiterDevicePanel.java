package hibiki.ui.panels.devices;

import hibiki.ui.PluginPane;
import hibiki.ui.Theme;
import hibiki.ui.panels.KnobPanel;
import java.awt.*;
import java.awt.event.*;
import javax.swing.*;

/** Ableton Live-style Limiter device panel with arc-knobs and GR meter. */
public class LimiterDevicePanel extends AbstractDevicePanel {
  private static final int PARAM_CEILING = 0;
  private static final int PARAM_RELEASE = 1;
  private static final int PARAM_LOOKAHEAD = 2;
  private static final int PARAM_GAIN = 3;
  private static final int PARAM_ENABLE = 4;
  private static final int TOTAL_PARAMS = 5;

  private static final Color ACCENT = new Color(0xFF6633);

  private boolean enabled = true;
  private float gainReductionDb = 0;
  private final GrMeterPanel grMeter;
  private final KnobPanel[] knobs;

  public LimiterDevicePanel(int trackIndex, int pluginIndex) {
    super(trackIndex, pluginIndex, TOTAL_PARAMS);

    // Defaults
    params[PARAM_CEILING] = 1.0;
    params[PARAM_RELEASE] = 0.3;
    params[PARAM_LOOKAHEAD] = 0.5;
    params[PARAM_GAIN] = 0.0;
    params[PARAM_ENABLE] = 1.0;

    Theme theme = Theme.getInstance();
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(theme.scale(280), theme.scale(200)));
    setMaximumSize(new Dimension(theme.scale(280), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Header
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x8B2500));
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));

    JLabel nameLabel = new JLabel("Limiter");
    nameLabel.setForeground(Color.WHITE);
    nameLabel.setFont(theme.FONT_UI_BOLD);
    header.add(nameLabel, BorderLayout.CENTER);

    grMeter = new GrMeterPanel();

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

    JToggleButton enableBtn = new JToggleButton("On", enabled);
    enableBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    enableBtn.setFocusPainted(false);
    enableBtn.addActionListener(
        e -> {
          enabled = enableBtn.isSelected();
          sendParam(PARAM_ENABLE, enabled ? 1.0 : 0.0);
        });
    btnPanel.add(enableBtn);

    JButton delBtn = new JButton("\u274C");
    delBtn.addActionListener(e -> sendRemove());
    btnPanel.add(delBtn);
    header.add(btnPanel, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // Center: GR meter
    grMeter.setPreferredSize(new Dimension(0, theme.scale(80)));
    add(grMeter, BorderLayout.CENTER);

    // Bottom: knobs
    JPanel knobRow = new JPanel(new GridLayout(1, 4, theme.scale(2), 0));
    knobRow.setBackground(theme.BG_DARK);
    knobRow.setPreferredSize(new Dimension(0, theme.scale(68)));
    knobRow.setBorder(
        BorderFactory.createEmptyBorder(
            theme.scale(4), theme.scale(6), theme.scale(4), theme.scale(6)));

    String[] names = {"Ceiling", "Release", "Look", "Gain"};
    int[] paramIds = {PARAM_CEILING, PARAM_RELEASE, PARAM_LOOKAHEAD, PARAM_GAIN};
    knobs = new KnobPanel[4];
    for (int k = 0; k < 4; k++) {
      final int pid = paramIds[k];
      knobs[k] = new KnobPanel(names[k], params[pid], makeFormatter(pid), ACCENT);
      knobs[k].addChangeListener(
          e -> {
            if (updatingFromBackend) return;
            params[pid] = knobs[findKnobIndex(pid)].getValue();
            sendParam(pid, params[pid]);
          });
      knobRow.add(knobs[k]);
    }
    add(knobRow, BorderLayout.SOUTH);
  }

  private int findKnobIndex(int paramId) {
    int[] ids = {PARAM_CEILING, PARAM_RELEASE, PARAM_LOOKAHEAD, PARAM_GAIN};
    for (int i = 0; i < ids.length; i++) {
      if (ids[i] == paramId) return i;
    }
    return 0;
  }

  private ValueFormatter makeFormatter(int paramId) {
    return norm -> {
      if (paramId == PARAM_CEILING) return String.format("%.1fdB", norm * 12.0 - 12.0);
      if (paramId == PARAM_RELEASE) {
        float ms = 10.0f * (float) Math.pow(100.0f, norm);
        return ms < 100 ? String.format("%.1fms", ms) : String.format("%.0fms", ms);
      }
      if (paramId == PARAM_LOOKAHEAD)
        return String.format("%.1fms", 0.1f * (float) Math.pow(50.0f, norm));
      if (paramId == PARAM_GAIN) return String.format("%.1fdB", norm * 24.0);
      return String.format("%.2f", norm);
    };
  }

  public void updateParam(int paramId, double value) {
    if (paramId < 0 || paramId >= TOTAL_PARAMS) return;
    updatingFromBackend = true;
    params[paramId] = value;
    if (paramId == PARAM_ENABLE) {
      enabled = value >= 0.5;
    } else {
      int ki = findKnobIndex(paramId);
      if (ki >= 0 && ki < knobs.length && knobs[ki] != null) {
        knobs[ki].setValue(value);
      }
    }
    updatingFromBackend = false;
  }

  public void updateMetering(float grDb) {
    gainReductionDb = grDb;
    grMeter.repaint();
  }

  private class GrMeterPanel extends JPanel {
    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      int w = getWidth(), h = getHeight();
      int pad = 4;
      float grNorm = Math.min(1, Math.abs(gainReductionDb) / 30.0f);
      int barH = (int) (grNorm * (h - pad * 2));
      if (barH > 0) {
        g2.setColor(ACCENT);
        g2.fillRect(pad, pad, w - pad * 2, barH);
      }
      g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(7.0f)));
      g2.setColor(new Color(255, 255, 255, 80));
      g2.drawString(String.format("%.1f", gainReductionDb), pad, h - pad);
      g2.drawString("GR", pad, pad + 8);
      g2.dispose();
    }
  }
}
