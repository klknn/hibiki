package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import hibiki.pb.notifications.*;
import hibiki.pb.notifications.ParamInfo;
import java.awt.*;
import java.awt.event.*;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import javax.swing.*;

public class PluginPane extends JPanel {
  private static PluginPane instance;
  private final JPanel deviceChainContent;
  // Per-track cache: trackIndex -> (pluginIndex -> DevicePanel)
  private final Map<Integer, Map<Integer, DevicePanel>> trackDevicePanels = new TreeMap<>();
  // Built-in device panels (EQ, Compressor) keyed by trackIndex -> pluginIndex ->
  // panel
  private final Map<Integer, Map<Integer, JPanel>> builtinPanels = new TreeMap<>();
  private final WaveformPanel waveformPanel = new WaveformPanel();
  private int selectedTrack = 0; // Currently selected track to display

  /** Records the last parameter the user touched via a slider. */
  public static class LastTouchedParam {
    public final int trackIndex;
    public final int pluginIndex;
    public final long paramId;
    public final String paramName;

    LastTouchedParam(int t, int p, long id, String name) {
      this.trackIndex = t;
      this.pluginIndex = p;
      this.paramId = id;
      this.paramName = name;
    }
  }

  private static volatile LastTouchedParam lastTouched = null;

  /** Get the last parameter the user adjusted (or null). */
  public static LastTouchedParam getLastTouchedParam() {
    return lastTouched;
  }

  public static PluginPane getInstance() {
    return instance;
  }

  public PluginPane() {
    instance = this;
    setLayout(new BorderLayout());
    setBackground(Theme.getInstance().BG_DARK);
    setBorder(BorderFactory.createMatteBorder(1, 0, 0, 0, Theme.getInstance().BORDER));
    setPreferredSize(new Dimension(0, Theme.getInstance().scale(200)));

    deviceChainContent = new JPanel();
    deviceChainContent.setLayout(new FlowLayout(FlowLayout.LEFT, 5, 5));
    deviceChainContent.setBackground(Theme.getInstance().BG_DARK);

    JScrollPane scrollPane = new JScrollPane(deviceChainContent);
    scrollPane.setBorder(null);
    scrollPane.setBackground(Theme.getInstance().BG_DARK);
    scrollPane.getViewport().setBackground(Theme.getInstance().BG_DARK);
    scrollPane.getHorizontalScrollBar().setUnitIncrement(Theme.getInstance().scale(16));
    add(scrollPane, BorderLayout.CENTER);

    BackendManager.getInstance()
        .addNotificationListener(
            notification -> {
              switch (notification.getResponseCase()) {
                case PARAM_LIST:
                  updateParams(notification.getParamList());
                  break;
                case CLEAR_PROJECT:
                  clearPanels();
                  break;
                case CLIP_WAVEFORM:
                  {
                    var cw = notification.getClipWaveform();
                    float[] wf = new float[cw.getWaveformCount()];
                    for (int i = 0; i < wf.length; i++) wf[i] = cw.getWaveform(i);
                    waveformPanel.setWaveform(cw.getTrackIndex(), cw.getSlotIndex(), wf);
                    rebuildDeviceChain();
                    break;
                  }
                case PARAM_VALUE_CHANGE:
                  {
                    var pvc = notification.getParamValueChange();
                    handleParamValueChange(
                        pvc.getTrackIndex(),
                        pvc.getPluginIndex(),
                        pvc.getParamId(),
                        pvc.getValue());
                    break;
                  }
                case PLUGIN_SPECTRUM: {
                  var spec = notification.getPluginSpectrum();
                  handlePluginSpectrum(
                      spec.getTrackIndex(),
                      spec.getPluginIndex(),
                      spec.getInputMagnitudesList(),
                      spec.getOutputMagnitudesList());
                  break;
                }
                case PLUGIN_METERING: {
                  var meter = notification.getPluginMetering();
                  handlePluginMetering(
                      meter.getTrackIndex(),
                      meter.getPluginIndex(),
                      meter.getInputDb(),
                      meter.getOutputDb(),
                      meter.getGainReductionDb());
                  break;
                }
                default:
                  break;
              }
            });
  }

  private void clearPanels() {
    SwingUtilities.invokeLater(
        () -> {
          trackDevicePanels.clear();
          deviceChainContent.removeAll();
          rebuildDeviceChain();
        });
  }

  /** Handle a live param value change notification from the backend. */
  private void handleParamValueChange(int trackIdx, int pluginIdx, int paramId, float value) {
    SwingUtilities.invokeLater(
        () -> {
          // Check built-in panels first
          Map<Integer, JPanel> builtins = builtinPanels.get(trackIdx);
          if (builtins != null) {
            JPanel bp = builtins.get(pluginIdx);
            if (bp instanceof EqDevicePanel) {
              ((EqDevicePanel) bp).updateParam(paramId, value);
              return;
            } else if (bp instanceof CompressorDevicePanel) {
              ((CompressorDevicePanel) bp).updateParam(paramId, value);
              return;
            }
          }
          Map<Integer, DevicePanel> panels = trackDevicePanels.get(trackIdx);
          if (panels == null) return;
          DevicePanel dp = panels.get(pluginIdx);
          if (dp == null) return;
          dp.updateSlider(paramId, value);
        });
  }

  /** Handle plugin spectrum data for EQ visualization. */
  private void handlePluginSpectrum(int trackIdx, int pluginIdx,
      List<Float> inputMags, List<Float> outputMags) {
    SwingUtilities.invokeLater(() -> {
      Map<Integer, JPanel> builtins = builtinPanels.get(trackIdx);
      if (builtins == null)
        return;
      JPanel bp = builtins.get(pluginIdx);
      if (bp instanceof EqDevicePanel) {
        ((EqDevicePanel) bp).setSpectrumData(inputMags, outputMags);
      }
    });
  }

  /** Handle plugin metering data for compressor visualization. */
  private void handlePluginMetering(int trackIdx, int pluginIdx,
      float inputDb, float outputDb, float gainReductionDb) {
    SwingUtilities.invokeLater(() -> {
      Map<Integer, JPanel> builtins = builtinPanels.get(trackIdx);
      if (builtins == null)
        return;
      JPanel bp = builtins.get(pluginIdx);
      if (bp instanceof CompressorDevicePanel) {
        CompressorDevicePanel comp = (CompressorDevicePanel) bp;
        comp.setInputOutputLevel(inputDb, outputDb);
        comp.setGainReduction(gainReductionDb);
      }
    });
  }

  public void updateParams(ParamList paramList) {
    SwingUtilities.invokeLater(
        () -> {
          int trackIdx = paramList.getTrackIndex();
          int pIdx = paramList.getPluginIndex();
          String pluginName = paramList.getPluginName();

          // Handle removal
          if (pluginName.isEmpty()) {
            trackDevicePanels.computeIfAbsent(trackIdx, k -> new TreeMap<>()).remove(pIdx);
            Map<Integer, JPanel> bi = builtinPanels.get(trackIdx);
            if (bi != null)
              bi.remove(pIdx);
            if (trackIdx == selectedTrack)
              rebuildDeviceChain();
            return;
          }

          // Detect built-in devices and create specialized panels
          if ("EQ Eight".equals(pluginName)) {
            Map<Integer, JPanel> bi = builtinPanels.computeIfAbsent(trackIdx, k -> new TreeMap<>());
            if (!(bi.get(pIdx) instanceof EqDevicePanel)) {
              bi.put(pIdx, new EqDevicePanel(trackIdx, pIdx));
            }
            if (trackIdx == selectedTrack)
              rebuildDeviceChain();
            return;
          }
          if ("Compressor".equals(pluginName)) {
            Map<Integer, JPanel> bi = builtinPanels.computeIfAbsent(trackIdx, k -> new TreeMap<>());
            if (!(bi.get(pIdx) instanceof CompressorDevicePanel)) {
              bi.put(pIdx, new CompressorDevicePanel(trackIdx, pIdx));
            }
            if (trackIdx == selectedTrack)
              rebuildDeviceChain();
            return;
          }

          // Standard VST3 device panel
          Map<Integer, DevicePanel> devicePanels = trackDevicePanels.computeIfAbsent(trackIdx, k -> new TreeMap<>());
          DevicePanel panel = devicePanels.get(pIdx);
          if (panel == null || !panel.pluginName.equals(pluginName)) {
            panel = new DevicePanel(trackIdx, pIdx, pluginName, paramList.getIsInstrument());
            devicePanels.put(pIdx, panel);
          }
          panel.setParams(paramList);

          if (trackIdx == selectedTrack)
            rebuildDeviceChain();
        });
  }

  /**
   * Set the selected track for filtering devices. Shows devices from the selected track (cached).
   */
  public void setSelectedTrack(int trackIdx) {
    if (this.selectedTrack != trackIdx) {
      this.selectedTrack = trackIdx;
      rebuildDeviceChain();
    }
  }

  private void rebuildDeviceChain() {
    deviceChainContent.removeAll();

    // Get device panels for the selected track
    Map<Integer, DevicePanel> devicePanels =
        trackDevicePanels.getOrDefault(selectedTrack, java.util.Collections.emptyMap());
    Map<Integer, JPanel> builtins = builtinPanels.getOrDefault(selectedTrack, java.util.Collections.emptyMap());

    // Merge all panels by plugin index order
    TreeMap<Integer, JPanel> allPanels = new TreeMap<>();
    for (var e : devicePanels.entrySet())
      allPanels.put(e.getKey(), e.getValue());
    for (var e : builtins.entrySet())
      allPanels.put(e.getKey(), e.getValue());

    // Separate instrument from effects
    JPanel instrument = null;
    List<JPanel> effects = new ArrayList<>();
    for (var entry : allPanels.entrySet()) {
      JPanel p = entry.getValue();
      if (p instanceof DevicePanel && ((DevicePanel) p).isInstrument) {
        instrument = p;
      } else {
        effects.add(p);
      }
    }

    // Add waveform if any
    if (waveformPanel.hasData()) {
      deviceChainContent.add(waveformPanel);
    }

    // Add instrument if any
    if (instrument != null) {
      deviceChainContent.add(instrument);
    }

    // Add effects (VST3 + built-in) in plugin index order
    for (JPanel p : effects) {
      deviceChainContent.add(p);
    }

    deviceChainContent.revalidate();
    deviceChainContent.repaint();
  }

  private class DevicePanel extends JPanel {
    private final int trackIndex;
    private final int pluginIndex;
    private final String pluginName;
    private final boolean isInstrument;
    private final JPanel paramListPanel;
    private final JTextField searchField;
    private final List<ParamPanel> allParams = new ArrayList<>();
    private final Map<Integer, JSlider> paramSliders = new TreeMap<>();
    private boolean updatingFromBackend = false;

    DevicePanel(int trackIndex, int pluginIndex, String pluginName, boolean isInstrument) {
      this.trackIndex = trackIndex;
      this.pluginIndex = pluginIndex;
      this.pluginName = pluginName;
      this.isInstrument = isInstrument;

      setLayout(new BorderLayout());
      setPreferredSize(
          new Dimension(Theme.getInstance().scale(250), Theme.getInstance().scale(220)));
      setBackground(Theme.getInstance().BG_MEDIUM);
      setBorder(BorderFactory.createLineBorder(Theme.getInstance().BORDER));

      // Header
      JPanel header = new JPanel(new BorderLayout());
      header.setBackground(Theme.getInstance().TRACK_HEADER);
      header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));

      JLabel nameLabel = new JLabel(pluginName);
      nameLabel.setForeground(Theme.getInstance().TEXT_BRIGHT);
      nameLabel.setFont(Theme.getInstance().FONT_UI_BOLD);
      header.add(nameLabel, BorderLayout.CENTER);

      JPanel btnPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT, 2, 0));
      btnPanel.setOpaque(false);

      JButton editBtn = new JButton("Edit");
      editBtn.addActionListener(e -> sendShowGui());
      btnPanel.add(editBtn);

      JButton delBtn = new JButton("❌");
      delBtn.addActionListener(e -> sendRemovePlugin());
      btnPanel.add(delBtn);

      header.add(btnPanel, BorderLayout.EAST);
      add(header, BorderLayout.NORTH);

      // Search and Params
      JPanel body = new JPanel(new BorderLayout());
      body.setBackground(Theme.getInstance().BG_MEDIUM);

      searchField =
          new JTextField() {
            @Override
            protected void paintComponent(Graphics g) {
              super.paintComponent(g);
              if (getText().isEmpty() && !isFocusOwner()) {
                Graphics2D g2 = (Graphics2D) g.create();
                g2.setRenderingHint(
                    RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
                g2.setColor(Theme.getInstance().ACCENT_BLUE);
                g2.setFont(getFont().deriveFont(Font.ITALIC));
                int x = getInsets().left;
                int y =
                    (getHeight() - g2.getFontMetrics().getHeight()) / 2
                        + g2.getFontMetrics().getAscent();
                g2.drawString("search...", x, y);
                g2.dispose();
              }
            }
          };
      searchField.setBackground(Theme.getInstance().BG_DARK);
      searchField.setForeground(Theme.getInstance().TEXT_LIGHT);
      searchField.setCaretColor(Theme.getInstance().TEXT_LIGHT);
      searchField.setBorder(
          BorderFactory.createCompoundBorder(
              BorderFactory.createMatteBorder(0, 0, 1, 0, Theme.getInstance().BORDER),
              BorderFactory.createEmptyBorder(2, 5, 2, 5)));
      // Repaint when focus changes to show/hide placeholder
      searchField.addFocusListener(
          new FocusAdapter() {
            @Override
            public void focusGained(FocusEvent e) {
              searchField.repaint();
            }

            @Override
            public void focusLost(FocusEvent e) {
              searchField.repaint();
            }
          });
      searchField
          .getDocument()
          .addDocumentListener(
              new javax.swing.event.DocumentListener() {
                public void insertUpdate(javax.swing.event.DocumentEvent e) {
                  filterParams();
                }

                public void removeUpdate(javax.swing.event.DocumentEvent e) {
                  filterParams();
                }

                public void changedUpdate(javax.swing.event.DocumentEvent e) {
                  filterParams();
                }
              });
      body.add(searchField, BorderLayout.NORTH);

      paramListPanel = new JPanel();
      paramListPanel.setLayout(new BoxLayout(paramListPanel, BoxLayout.Y_AXIS));
      paramListPanel.setBackground(Theme.getInstance().BG_MEDIUM);

      JScrollPane scroll = new JScrollPane(paramListPanel);
      scroll.setBorder(null);
      scroll.getVerticalScrollBar().setUnitIncrement(10);
      body.add(scroll, BorderLayout.CENTER);

      add(body, BorderLayout.CENTER);
    }

    void setParams(ParamList paramList) {
      paramListPanel.removeAll();
      allParams.clear();
      paramSliders.clear();
      for (int i = 0; i < paramList.getParamsCount(); i++) {
        ParamInfo info = paramList.getParams(i);
        ParamPanel pp = new ParamPanel(trackIndex, pluginIndex, info);
        allParams.add(pp);
        paramSliders.put(info.getId(), pp.slider);
      }
      filterParams();
    }

    void updateSlider(int paramId, float value) {
      JSlider slider = paramSliders.get(paramId);
      if (slider != null) {
        updatingFromBackend = true;
        slider.setValue((int) (value * 1000));
        updatingFromBackend = false;
      }
    }

    private void filterParams() {
      String query = searchField.getText().toLowerCase();
      paramListPanel.removeAll();
      for (ParamPanel pp : allParams) {
        if (pp.name.toLowerCase().contains(query)) {
          paramListPanel.add(pp);
        }
      }
      paramListPanel.revalidate();
      paramListPanel.repaint();
    }

    private void sendShowGui() {
      BackendManager.getInstance()
          .sendRequest(
              Request.newBuilder()
                  .setPlugin(
                      PluginCmd.newBuilder()
                          .setAction(PluginCmd.Action.ACTION_SHOW_GUI)
                          .setTarget(
                              EntityRef.newBuilder()
                                  .setTrackIndex(trackIndex)
                                  .setPluginIndex(pluginIndex)))
                  .build());
    }

    private void sendRemovePlugin() {
      BackendManager.getInstance()
          .sendRequest(
              Request.newBuilder()
                  .setPlugin(
                      PluginCmd.newBuilder()
                          .setAction(PluginCmd.Action.ACTION_REMOVE)
                          .setTarget(
                              EntityRef.newBuilder()
                                  .setTrackIndex(trackIndex)
                                  .setPluginIndex(pluginIndex)))
                  .build());

      // Immediate local feedback
      Map<Integer, DevicePanel> panels = trackDevicePanels.get(trackIndex);
      if (panels != null) {
        panels.remove(pluginIndex);
      }
      rebuildDeviceChain();
    }
  }

  private class ParamPanel extends JPanel {
    final String name;
    final int paramId;
    final JSlider slider;

    ParamPanel(int trackIndex, int pluginIndex, ParamInfo info) {
      this.name = info.getName();
      this.paramId = info.getId();
      setLayout(new BorderLayout());
      setBackground(Theme.getInstance().BG_MEDIUM);
      setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));
      setMaximumSize(new Dimension(Integer.MAX_VALUE, Theme.getInstance().scale(45)));

      JLabel label = new JLabel(name);
      label.setForeground(Theme.getInstance().TEXT_LIGHT);
      label.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(10.0f)));
      add(label, BorderLayout.NORTH);

      slider = new JSlider(0, 1000, (int) (info.getDefaultValue() * 1000));
      slider.setBackground(Theme.getInstance().BG_MEDIUM);
      slider.setPreferredSize(
          new Dimension(Theme.getInstance().scale(150), Theme.getInstance().scale(20)));
      slider.addChangeListener(
          e -> {
            if (!slider.getValueIsAdjusting()) {
              // Skip sending if this update came from the backend
              DevicePanel parent = findParentDevicePanel();
              if (parent != null && parent.updatingFromBackend) return;
              float val = slider.getValue() / 1000.0f;
              sendParamChange(trackIndex, pluginIndex, info.getId(), val);
            }
          });
      add(slider, BorderLayout.CENTER);
    }

    private DevicePanel findParentDevicePanel() {
      java.awt.Container c = getParent();
      while (c != null) {
        if (c instanceof DevicePanel) return (DevicePanel) c;
        c = c.getParent();
      }
      return null;
    }

    private void sendParamChange(int trackIndex, int pluginIndex, int paramId, float value) {
      lastTouched = new LastTouchedParam(trackIndex, pluginIndex, paramId, name);
      BackendManager.getInstance()
          .sendRequest(
              Request.newBuilder()
                  .setPlugin(
                      PluginCmd.newBuilder()
                          .setAction(PluginCmd.Action.ACTION_SET_PARAM)
                          .setTarget(
                              EntityRef.newBuilder()
                                  .setTrackIndex(trackIndex)
                                  .setPluginIndex(pluginIndex))
                          .setParamId(paramId)
                          .setParamValue(value))
                  .build());
    }
  }
}
