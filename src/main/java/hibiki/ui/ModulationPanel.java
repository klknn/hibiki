package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.notifications.ModulationSlotInfo;
import java.awt.*;
import java.awt.event.*;
import java.util.List;
import javax.swing.*;

/**
 * Modulation panel shown below the device chain in PluginPane. Provides 3 LFO modulator slots per
 * plugin, each with waveform selection, rate, depth (±1.0), and click-to-assign target parameter
 * workflow.
 */
public class ModulationPanel extends JPanel {
  private static final int MAX_SLOTS = 3;
  private static final String[] WAVEFORM_NAMES = {"Sin", "Saw", "Sqr", "Rnd"};

  private int trackIndex;
  private int pluginIndex;
  private final ModSlot[] slots = new ModSlot[MAX_SLOTS];

  // Assign-mode state: when >= 0, we are waiting for a param touch to bind
  private int assigningSlot = -1;

  public ModulationPanel(int trackIndex, int pluginIndex) {
    this.trackIndex = trackIndex;
    this.pluginIndex = pluginIndex;
    Theme theme = Theme.getInstance();
    setLayout(new BoxLayout(this, BoxLayout.Y_AXIS));
    setBackground(theme.BG_DARK);
    setBorder(
        BorderFactory.createCompoundBorder(
            BorderFactory.createMatteBorder(1, 0, 0, 0, theme.BORDER),
            BorderFactory.createEmptyBorder(4, 6, 4, 6)));

    // Header
    JLabel header = new JLabel("⬡ Modulation");
    header.setFont(theme.FONT_UI.deriveFont(Font.BOLD, 11f));
    header.setForeground(theme.TEXT_NORMAL);
    header.setAlignmentX(LEFT_ALIGNMENT);
    add(header);
    add(Box.createVerticalStrut(4));

    for (int i = 0; i < MAX_SLOTS; i++) {
      slots[i] = new ModSlot(i);
      slots[i].setAlignmentX(LEFT_ALIGNMENT);
      add(slots[i]);
      if (i < MAX_SLOTS - 1) add(Box.createVerticalStrut(2));
    }
  }

  /** Called when the user is in assign mode and touches a param slider. */
  public boolean isAssigning() {
    return assigningSlot >= 0;
  }

  /** Complete assignment: bind the assigning slot to the given param. */
  public void completeAssign(long paramId, String paramName) {
    if (assigningSlot < 0) return;
    int slot = assigningSlot;
    assigningSlot = -1;
    slots[slot].setAssignMode(false);

    BackendManager.getInstance().assignModulator(trackIndex, pluginIndex, slot, (int) paramId);
    // Optimistically update label
    slots[slot].targetLabel.setText(paramName);
  }

  /** Cancel any pending assign */
  public void cancelAssign() {
    if (assigningSlot >= 0) {
      slots[assigningSlot].setAssignMode(false);
      assigningSlot = -1;
    }
  }

  /** Update from a ModulationInfo notification */
  public void updateFromNotification(List<ModulationSlotInfo> slotInfos) {
    for (ModulationSlotInfo info : slotInfos) {
      int idx = info.getSlotIndex();
      if (idx < 0 || idx >= MAX_SLOTS) continue;
      ModSlot s = slots[idx];
      s.updateFromInfo(info);
    }
  }

  public void setTrackAndPlugin(int trackIndex, int pluginIndex) {
    this.trackIndex = trackIndex;
    this.pluginIndex = pluginIndex;
  }

  // ─────────────────────────── Per-slot UI ───────────────────────────

  private class ModSlot extends JPanel {
    final int slotIndex;
    final JComboBox<String> waveformBox;
    final JSlider rateSlider;
    final JSlider depthSlider;
    final JLabel rateLabel;
    final JLabel depthLabel;
    final JLabel targetLabel;
    final JButton assignBtn;
    final JButton removeBtn;
    boolean active = false;

    ModSlot(int slotIndex) {
      this.slotIndex = slotIndex;
      Theme theme = Theme.getInstance();
      setOpaque(false);
      setLayout(new FlowLayout(FlowLayout.LEFT, 4, 1));
      setMaximumSize(new Dimension(Integer.MAX_VALUE, 28));

      // Slot label
      JLabel label = new JLabel((slotIndex + 1) + ":");
      label.setFont(theme.FONT_UI.deriveFont(10f));
      label.setForeground(theme.TEXT_DIM);
      add(label);

      // Waveform selector
      waveformBox = new JComboBox<>(WAVEFORM_NAMES);
      waveformBox.setFont(theme.FONT_UI.deriveFont(10f));
      waveformBox.setPreferredSize(new Dimension(52, 20));
      waveformBox.addActionListener(e -> sendConfigure());
      add(waveformBox);

      // Rate slider (0.01 – 20 Hz, mapped logarithmically)
      rateLabel = new JLabel("1.0Hz");
      rateLabel.setFont(theme.FONT_UI.deriveFont(9f));
      rateLabel.setForeground(theme.TEXT_DIM);
      rateLabel.setPreferredSize(new Dimension(42, 16));

      rateSlider = new JSlider(1, 2000, 100); // 0.01 to 20.00
      rateSlider.setPreferredSize(new Dimension(80, 18));
      rateSlider.setOpaque(false);
      rateSlider.addChangeListener(
          e -> {
            rateLabel.setText(String.format("%.1fHz", rateSlider.getValue() / 100f));
            if (!rateSlider.getValueIsAdjusting()) sendConfigure();
          });
      add(rateSlider);
      add(rateLabel);

      // Depth slider (-100 to +100, representing -1.0 to +1.0)
      depthLabel = new JLabel("0.00");
      depthLabel.setFont(theme.FONT_UI.deriveFont(9f));
      depthLabel.setForeground(theme.TEXT_DIM);
      depthLabel.setPreferredSize(new Dimension(32, 16));

      depthSlider = new JSlider(-100, 100, 0);
      depthSlider.setPreferredSize(new Dimension(80, 18));
      depthSlider.setOpaque(false);
      depthSlider.addChangeListener(
          e -> {
            depthLabel.setText(String.format("%.2f", depthSlider.getValue() / 100f));
            if (!depthSlider.getValueIsAdjusting()) sendConfigure();
          });
      add(depthSlider);
      add(depthLabel);

      // Target label
      targetLabel = new JLabel("—");
      targetLabel.setFont(theme.FONT_UI.deriveFont(Font.ITALIC, 10f));
      targetLabel.setForeground(theme.ACCENT_ORANGE);
      targetLabel.setPreferredSize(new Dimension(90, 16));
      add(targetLabel);

      // Assign button
      assignBtn = new JButton("⊕");
      assignBtn.setFont(theme.FONT_UI.deriveFont(12f));
      assignBtn.setMargin(new Insets(0, 4, 0, 4));
      assignBtn.setPreferredSize(new Dimension(28, 20));
      assignBtn.setToolTipText("Assign to parameter");
      assignBtn.addActionListener(
          e -> {
            if (assigningSlot == slotIndex) {
              cancelAssign();
            } else {
              cancelAssign(); // cancel any previous
              if (!active) {
                // Auto-add the modulator first
                sendAdd();
                active = true;
              }
              assigningSlot = slotIndex;
              setAssignMode(true);
            }
          });
      add(assignBtn);

      // Remove button
      removeBtn = new JButton("×");
      removeBtn.setFont(theme.FONT_UI.deriveFont(12f));
      removeBtn.setMargin(new Insets(0, 2, 0, 2));
      removeBtn.setPreferredSize(new Dimension(22, 20));
      removeBtn.setToolTipText("Remove modulator");
      removeBtn.addActionListener(
          e -> {
            BackendManager.getInstance().removeModulator(trackIndex, pluginIndex, slotIndex);
            active = false;
            targetLabel.setText("—");
            depthSlider.setValue(0);
            rateSlider.setValue(100);
            waveformBox.setSelectedIndex(0);
          });
      add(removeBtn);
    }

    void setAssignMode(boolean on) {
      Theme theme = Theme.getInstance();
      assignBtn.setForeground(on ? theme.ACCENT_ORANGE : UIManager.getColor("Button.foreground"));
      assignBtn.setToolTipText(on ? "Click a parameter to assign..." : "Assign to parameter");
    }

    void sendAdd() {
      int wf = waveformBox.getSelectedIndex();
      float rate = rateSlider.getValue() / 100f;
      float depth = depthSlider.getValue() / 100f;
      BackendManager.getInstance()
          .addModulator(trackIndex, pluginIndex, slotIndex, wf, rate, depth, false);
    }

    void sendConfigure() {
      if (!active) return;
      int wf = waveformBox.getSelectedIndex();
      float rate = rateSlider.getValue() / 100f;
      float depth = depthSlider.getValue() / 100f;
      BackendManager.getInstance()
          .configureModulator(trackIndex, pluginIndex, slotIndex, wf, rate, depth, false);
    }

    void updateFromInfo(ModulationSlotInfo info) {
      active = info.getAssigned();
      if (info.getWaveform() >= 0 && info.getWaveform() < WAVEFORM_NAMES.length) {
        waveformBox.setSelectedIndex(info.getWaveform());
      }
      rateSlider.setValue((int) (info.getRateHz() * 100));
      rateLabel.setText(String.format("%.1fHz", info.getRateHz()));
      depthSlider.setValue((int) (info.getDepth() * 100));
      depthLabel.setText(String.format("%.2f", info.getDepth()));
      if (info.getAssigned() && !info.getTargetParamName().isEmpty()) {
        targetLabel.setText(info.getTargetParamName());
      } else {
        targetLabel.setText("—");
      }
    }
  }

  /** Support loading modulator from browser DnD (modulation:waveform) */
  void handleModulationDrop(int slotIndex, String waveformName) {
    if (slotIndex < 0 || slotIndex >= MAX_SLOTS) return;
    int wf = 0;
    switch (waveformName.toLowerCase()) {
      case "sine":
        wf = 0;
        break;
      case "saw":
      case "sawtooth":
        wf = 1;
        break;
      case "square":
        wf = 2;
        break;
      case "random":
        wf = 3;
        break;
    }
    slots[slotIndex].waveformBox.setSelectedIndex(wf);
    slots[slotIndex].sendAdd();
    slots[slotIndex].active = true;
  }
}
