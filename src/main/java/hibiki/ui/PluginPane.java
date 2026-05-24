package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import hibiki.pb.notifications.*;
import hibiki.pb.notifications.ModulationSlotInfo;
import hibiki.pb.notifications.ParamInfo;
import hibiki.ui.panels.*;
import hibiki.ui.panels.devices.*;
import java.awt.*;
import java.awt.datatransfer.*;
import java.awt.dnd.*;
import java.awt.event.*;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import javax.swing.*;

public class PluginPane extends JPanel {
  /** Name → panel class for all built-in effects. All must have (int, int) constructor. */
  private static final Map<String, Class<? extends JPanel>> BUILTIN_DEVICE_PANELS =
      Map.ofEntries(
          Map.entry("EQ Eight", EqDevicePanel.class),
          Map.entry("Compressor", CompressorDevicePanel.class),
          Map.entry("Maxim", MaximDevicePanel.class),
          Map.entry("3xOsc", ThreeOscDevicePanel.class),
          Map.entry("Sampler", SamplerDevicePanel.class),
          Map.entry("Delay", DelayDevicePanel.class),
          Map.entry("Reverb", ReverbDevicePanel.class),
          Map.entry("Limiter", LimiterDevicePanel.class),
          Map.entry("Hott", HottDevicePanel.class),
          Map.entry("EnvShaper", EnvelopeShaperDevicePanel.class),
          Map.entry("Phaser", PhaserDevicePanel.class),
          Map.entry("Convolver", ConvolverDevicePanel.class),
          Map.entry("FilM", FilmDevicePanel.class),
          Map.entry("Aux", AuxDevicePanel.class),
          Map.entry("Drum Machine", DrumMachineDevicePanel.class),
          Map.entry("Vocodey", VocodeyDevicePanel.class),
          Map.entry("DR8 Kick", Dr8KickDevicePanel.class),
          Map.entry("DR8 Snare", Dr8SnareDevicePanel.class),
          Map.entry("DR8 Hat", Dr8HatDevicePanel.class),
          Map.entry("DR8 Tom", Dr8TomDevicePanel.class),
          Map.entry("DR8 Clap", Dr8ClapDevicePanel.class),
          Map.entry("DR8 Cowbell", Dr8CowbellDevicePanel.class),
          Map.entry("DR8 Crash", Dr8CrashDevicePanel.class),
          Map.entry("DR8 Rimshot", Dr8RimshotDevicePanel.class),
          Map.entry("DR8 Conga", Dr8CongaDevicePanel.class));

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
    deviceChainContent.setLayout(new BoxLayout(deviceChainContent, BoxLayout.X_AXIS));
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
                case PLUGIN_SPECTRUM:
                  {
                    var spec = notification.getPluginSpectrum();
                    handlePluginSpectrum(
                        spec.getTrackIndex(),
                        spec.getPluginIndex(),
                        spec.getInputMagnitudesList(),
                        spec.getOutputMagnitudesList());
                    break;
                  }
                case PLUGIN_METERING:
                  {
                    handlePluginMetering(notification.getPluginMetering());
                    break;
                  }
                case PLUGIN_SAMPLE_DATA:
                  {
                    var sd = notification.getPluginSampleData();
                    handlePluginSampleData(
                        sd.getTrackIndex(),
                        sd.getPluginIndex(),
                        sd.getWaveformList(),
                        sd.getSampleName());
                    break;
                  }
                case MODULATION_INFO:
                  {
                    var mi = notification.getModulationInfo();
                    handleModulationInfo(
                        mi.getTrackIndex(), mi.getPluginIndex(), mi.getSlotsList());
                    break;
                  }
                case PLUGIN_SCOPE_DATA:
                  {
                    var sd = notification.getPluginScopeData();
                    handlePluginScopeData(
                        sd.getTrackIndex(),
                        sd.getPluginIndex(),
                        sd.getLeftSamplesList(),
                        sd.getRightSamplesList());
                    break;
                  }
                case DRUM_PAD:
                  {
                    handleDrumPadNotification(notification.getDrumPad());
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
            } else if (bp instanceof MaximDevicePanel) {
              ((MaximDevicePanel) bp).updateParam(paramId, value);
              return;
            } else if (bp instanceof HottDevicePanel) {
              ((HottDevicePanel) bp).updateParam(paramId, value);
              return;
            } else if (bp instanceof ThreeOscDevicePanel) {
              ((ThreeOscDevicePanel) bp).handleParamChange(paramId, value);
              return;
            } else if (bp instanceof SamplerDevicePanel) {
              ((SamplerDevicePanel) bp).handleParamChange(paramId, value);
              return;
            } else if (bp instanceof DelayDevicePanel) {
              ((DelayDevicePanel) bp).updateParam(paramId, value);
              return;
            } else if (bp instanceof ReverbDevicePanel) {
              ((ReverbDevicePanel) bp).updateParam(paramId, value);
              return;
            } else if (bp instanceof EnvelopeShaperDevicePanel) {
              ((EnvelopeShaperDevicePanel) bp).updateParam(paramId, value);
              return;
            } else if (bp instanceof PhaserDevicePanel) {
              ((PhaserDevicePanel) bp).updateParam(paramId, value);
              return;
            } else if (bp instanceof FilmDevicePanel) {
              ((FilmDevicePanel) bp).handleParamChange(paramId, value);
              return;
            } else if (bp instanceof ConvolverDevicePanel) {
              ((ConvolverDevicePanel) bp).updateParam(paramId, value);
              return;
            } else if (bp instanceof AuxDevicePanel) {
              ((AuxDevicePanel) bp).updateParam(paramId, value);
              return;
            } else if (bp instanceof VocodeyDevicePanel) {
              ((VocodeyDevicePanel) bp).updateParam(paramId, value);
              return;
            } else if (bp instanceof Dr8DevicePanel) {
              ((Dr8DevicePanel) bp).updateParam(paramId, value);
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
  private void handlePluginSpectrum(
      int trackIdx, int pluginIdx, List<Float> inputMags, List<Float> outputMags) {
    SwingUtilities.invokeLater(
        () -> {
          Map<Integer, JPanel> builtins = builtinPanels.get(trackIdx);
          if (builtins == null) return;
          JPanel bp = builtins.get(pluginIdx);
          if (bp instanceof EqDevicePanel) {
            ((EqDevicePanel) bp).setSpectrumData(inputMags, outputMags);
          }
        });
  }

  /** Handle plugin metering data for compressor/limiter visualization. */
  private void handlePluginMetering(hibiki.pb.notifications.PluginMeteringData meter) {
    int trackIdx = meter.getTrackIndex();
    int pluginIdx = meter.getPluginIndex();
    SwingUtilities.invokeLater(
        () -> {
          Map<Integer, JPanel> builtins = builtinPanels.get(trackIdx);
          if (builtins == null) return;
          JPanel bp = builtins.get(pluginIdx);
          if (bp instanceof CompressorDevicePanel) {
            CompressorDevicePanel comp = (CompressorDevicePanel) bp;
            comp.setInputOutputLevel(meter.getInputDb(), meter.getOutputDb());
            comp.setGainReduction(meter.getGainReductionDb());
            comp.setSidechainLevel(meter.getSidechainDb());
          } else if (bp instanceof MaximDevicePanel) {
            MaximDevicePanel maxim = (MaximDevicePanel) bp;
            maxim.updateMetering(meter);
          } else if (bp instanceof HottDevicePanel) {
            HottDevicePanel hott = (HottDevicePanel) bp;
            hott.setInputOutputLevel(meter.getInputDb(), meter.getOutputDb());
            hott.setGainReduction(meter.getGainReductionDb());
          } else if (bp instanceof DelayDevicePanel) {
            ((DelayDevicePanel) bp).setInputOutputLevel(meter.getInputDb(), meter.getOutputDb());
          } else if (bp instanceof ReverbDevicePanel) {
            ((ReverbDevicePanel) bp).setInputOutputLevel(meter.getInputDb(), meter.getOutputDb());
          } else if (bp instanceof EnvelopeShaperDevicePanel) {
            ((EnvelopeShaperDevicePanel) bp)
                .setInputOutputLevel(meter.getInputDb(), meter.getOutputDb());
          } else if (bp instanceof PhaserDevicePanel) {
            ((PhaserDevicePanel) bp).setInputOutputLevel(meter.getInputDb(), meter.getOutputDb());
          }
        });
  }

  /** Handle scope data for Phaser Lissajous visualization. */
  private void handlePluginScopeData(
      int trackIdx, int pluginIdx, List<Float> leftSamples, List<Float> rightSamples) {
    SwingUtilities.invokeLater(
        () -> {
          Map<Integer, JPanel> builtins = builtinPanels.get(trackIdx);
          if (builtins == null) return;
          JPanel bp = builtins.get(pluginIdx);
          if (bp instanceof PhaserDevicePanel) {
            ((PhaserDevicePanel) bp).setScopeData(leftSamples, rightSamples);
          }
        });
  }

  /** Handle sample waveform data for sampler visualization. */
  private void handlePluginSampleData(
      int trackIdx, int pluginIdx, List<Float> waveform, String sampleName) {
    SwingUtilities.invokeLater(
        () -> {
          Map<Integer, JPanel> builtins = builtinPanels.get(trackIdx);
          if (builtins == null) return;
          JPanel bp = builtins.get(pluginIdx);
          if (bp instanceof SamplerDevicePanel) {
            ((SamplerDevicePanel) bp).updateWaveform(waveform, sampleName);
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
            if (bi != null) bi.remove(pIdx);
            if (trackIdx == selectedTrack) rebuildDeviceChain();
            return;
          }

          // Detect built-in devices and create specialized panels
          Class<? extends JPanel> builtinClass = BUILTIN_DEVICE_PANELS.get(pluginName);
          if (builtinClass != null) {
            // Remove any stale VST3 DevicePanel at this slot
            Map<Integer, DevicePanel> dp = trackDevicePanels.get(trackIdx);
            if (dp != null) dp.remove(pIdx);

            Map<Integer, JPanel> bi = builtinPanels.computeIfAbsent(trackIdx, k -> new TreeMap<>());
            if (!builtinClass.isInstance(bi.get(pIdx))) {
              try {
                bi.put(
                    pIdx,
                    builtinClass.getConstructor(int.class, int.class).newInstance(trackIdx, pIdx));
              } catch (Exception ex) {
                throw new RuntimeException("Failed to create " + pluginName + " panel", ex);
              }
            }
            if (trackIdx == selectedTrack) rebuildDeviceChain();
            return;
          }

          // Standard VST3 device panel — remove any stale built-in panel at this slot
          Map<Integer, JPanel> bi = builtinPanels.get(trackIdx);
          if (bi != null) bi.remove(pIdx);

          Map<Integer, DevicePanel> devicePanels =
              trackDevicePanels.computeIfAbsent(trackIdx, k -> new TreeMap<>());
          DevicePanel panel = devicePanels.get(pIdx);
          if (panel == null || !panel.pluginName.equals(pluginName)) {
            panel = new DevicePanel(trackIdx, pIdx, pluginName, paramList.getIsInstrument());
            devicePanels.put(pIdx, panel);
          }
          panel.setParams(paramList);

          if (trackIdx == selectedTrack) rebuildDeviceChain();
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

  /** Transferable data for plugin drag-and-drop reordering. */
  static final DataFlavor PLUGIN_FLAVOR;

  static {
    DataFlavor f = null;
    try {
      f = new DataFlavor(DataFlavor.javaJVMLocalObjectMimeType + ";class=\"[I\"");
    } catch (ClassNotFoundException e) {
      e.printStackTrace();
    }
    PLUGIN_FLAVOR = f;
  }

  /**
   * Wraps a device panel + a ModulationPanel side-by-side. The modPanel starts hidden and is
   * toggled by the Mod button. Supports drag-and-drop reordering.
   */
  private class DeviceWrapper extends JPanel {
    final JPanel device;
    final ModulationPanel modPanel;
    final int trackIndex;
    final int pluginIndex;
    private boolean dropLeft = false;
    private boolean dropRight = false;

    DeviceWrapper(JPanel device, int trackIndex, int pluginIndex) {
      this.device = device;
      this.trackIndex = trackIndex;
      this.pluginIndex = pluginIndex;
      this.modPanel = new ModulationPanel(trackIndex, pluginIndex);
      modPanel.setVisible(false);

      setLayout(new BoxLayout(this, BoxLayout.X_AXIS));
      setOpaque(false);
      add(device);
      add(Box.createHorizontalStrut(5));
      add(modPanel);

      // --- Drag support ---
      setTransferHandler(new DeviceTransferHandler());
      addMouseMotionListener(
          new MouseMotionAdapter() {
            @Override
            public void mouseDragged(MouseEvent e) {
              // Skip instruments (index 0 with isInstrument)
              if (device instanceof DevicePanel && ((DevicePanel) device).isInstrument) return;
              TransferHandler handler = getTransferHandler();
              handler.exportAsDrag(DeviceWrapper.this, e, TransferHandler.MOVE);
            }
          });

      // --- Drop support ---
      new DropTarget(
          this,
          DnDConstants.ACTION_MOVE,
          new DropTargetAdapter() {
            @Override
            public void dragOver(DropTargetDragEvent dtde) {
              if (!dtde.isDataFlavorSupported(PLUGIN_FLAVOR)) {
                dtde.rejectDrag();
                return;
              }
              dtde.acceptDrag(DnDConstants.ACTION_MOVE);
              // Determine drop side
              Point p = dtde.getLocation();
              boolean left = p.x < getWidth() / 2;
              if (left != dropLeft || !left != dropRight) {
                dropLeft = left;
                dropRight = !left;
                repaint();
              }
            }

            @Override
            public void dragExit(DropTargetEvent dte) {
              dropLeft = false;
              dropRight = false;
              repaint();
            }

            @Override
            public void drop(DropTargetDropEvent dtde) {
              dropLeft = false;
              dropRight = false;
              repaint();
              try {
                if (!dtde.isDataFlavorSupported(PLUGIN_FLAVOR)) {
                  dtde.rejectDrop();
                  return;
                }
                dtde.acceptDrop(DnDConstants.ACTION_MOVE);
                int[] data = (int[]) dtde.getTransferable().getTransferData(PLUGIN_FLAVOR);
                int srcTrack = data[0];
                int srcPlugin = data[1];
                if (srcTrack != trackIndex) {
                  dtde.dropComplete(false);
                  return;
                }
                // Determine target index based on drop side
                Point p = dtde.getLocation();
                int targetIdx = (p.x < getWidth() / 2) ? pluginIndex : pluginIndex + 1;
                // Adjust if dragging from before the target
                if (srcPlugin < targetIdx) targetIdx--;
                if (srcPlugin != targetIdx) {
                  sendReorderCommand(srcTrack, srcPlugin, targetIdx);
                }
                dtde.dropComplete(true);
              } catch (Exception ex) {
                ex.printStackTrace();
                dtde.dropComplete(false);
              }
            }
          });
    }

    @Override
    protected void paintChildren(Graphics g) {
      super.paintChildren(g);
      // Paint drop indicator
      Graphics2D g2 = (Graphics2D) g;
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
      g2.setStroke(new BasicStroke(3));
      g2.setColor(Theme.getInstance().ACCENT_BLUE);
      if (dropLeft) {
        g2.drawLine(1, 0, 1, getHeight());
      } else if (dropRight) {
        g2.drawLine(getWidth() - 2, 0, getWidth() - 2, getHeight());
      }
    }

    void toggleMod() {
      modPanel.setVisible(!modPanel.isVisible());
      revalidate();
      repaint();
    }
  }

  /** TransferHandler for drag-and-drop reordering of device wrappers. */
  private static class DeviceTransferHandler extends TransferHandler {
    @Override
    public int getSourceActions(JComponent c) {
      return MOVE;
    }

    @Override
    protected Transferable createTransferable(JComponent c) {
      if (c instanceof DeviceWrapper) {
        DeviceWrapper w = (DeviceWrapper) c;
        int[] data = {w.trackIndex, w.pluginIndex};
        return new Transferable() {
          @Override
          public DataFlavor[] getTransferDataFlavors() {
            return new DataFlavor[] {PLUGIN_FLAVOR};
          }

          @Override
          public boolean isDataFlavorSupported(DataFlavor flavor) {
            return PLUGIN_FLAVOR.equals(flavor);
          }

          @Override
          public Object getTransferData(DataFlavor flavor) {
            return data;
          }
        };
      }
      return null;
    }

    @Override
    protected void exportDone(JComponent source, Transferable data, int action) {
      // Rebuild happens via param list notifications from engine
    }
  }

  /** Send ACTION_REORDER_PLUGIN command to backend. */
  private static void sendReorderCommand(int trackIndex, int fromIndex, int toIndex) {
    PluginCmd cmd =
        PluginCmd.newBuilder()
            .setAction(PluginCmd.Action.ACTION_REORDER_PLUGIN)
            .setTarget(EntityRef.newBuilder().setTrackIndex(trackIndex).setPluginIndex(fromIndex))
            .setTargetPluginIndex(toIndex)
            .build();
    Request request = Request.newBuilder().setPlugin(cmd).build();
    BackendManager.getInstance().sendRequest(request);
  }

  /** Find the DeviceWrapper for a given track+plugin in the current device chain. */
  private DeviceWrapper findWrapper(int trackIndex, int pluginIndex) {
    for (Component c : deviceChainContent.getComponents()) {
      if (c instanceof DeviceWrapper) {
        DeviceWrapper w = (DeviceWrapper) c;
        if (w.trackIndex == trackIndex && w.pluginIndex == pluginIndex) return w;
      }
    }
    return null;
  }

  /**
   * Static helper to show sidechain source popup menu for any device panel. Called by built-in
   * device panels (Compressor, EQ, Hott, etc.) and DevicePanel.
   */
  public static void showSidechainPopup(JButton source, int trackIndex, int pluginIndex) {
    PluginPane pane = getInstance();
    if (pane == null) return;

    JPopupMenu menu = new JPopupMenu("Sidechain Source");
    menu.setBackground(Theme.getInstance().BG_DARK);

    JMenuItem noneItem = new JMenuItem("None");
    noneItem.addActionListener(e -> sendSidechainCmd(trackIndex, pluginIndex, -1));
    menu.add(noneItem);
    menu.addSeparator();

    java.util.Set<Integer> knownTracks = new java.util.TreeSet<>();
    for (var tk : pane.trackDevicePanels.keySet()) knownTracks.add(tk);
    for (var tk : pane.builtinPanels.keySet()) knownTracks.add(tk);
    // Include all tracks from session view (tracks without effects were missing)
    SessionView sv = SessionView.getInstance();
    if (sv != null) {
      for (int i = 0; i < sv.getTrackCount(); i++) knownTracks.add(i);
    }

    for (int tidx : knownTracks) {
      if (tidx == trackIndex) continue;
      String label = "Track " + tidx;
      TimelineView tv = TimelineView.getInstance();
      if (tv != null && tidx < tv.tracks.size()) {
        label = tidx + " " + tv.tracks.get(tidx).getDisplayName();
      }
      JMenuItem item = new JMenuItem(label);
      int srcIdx = tidx;
      item.addActionListener(e -> sendSidechainCmd(trackIndex, pluginIndex, srcIdx));
      menu.add(item);
    }

    if (knownTracks.isEmpty() || (knownTracks.size() == 1 && knownTracks.contains(trackIndex))) {
      JMenuItem noTracks = new JMenuItem("(no other tracks)");
      noTracks.setEnabled(false);
      menu.add(noTracks);
    }

    menu.show(source, 0, source.getHeight());
  }

  /** Send ACTION_SET_SIDECHAIN command to backend. */
  private static void sendSidechainCmd(int trackIndex, int pluginIndex, int sourceTrackIndex) {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setPlugin(
                    PluginCmd.newBuilder()
                        .setAction(PluginCmd.Action.ACTION_SET_SIDECHAIN)
                        .setTarget(
                            EntityRef.newBuilder()
                                .setTrackIndex(trackIndex)
                                .setPluginIndex(pluginIndex))
                        .setSidechainTrackIndex(sourceTrackIndex))
                .build());
  }

  private JPanel wrapDevice(JPanel device, int trackIndex, int pluginIndex) {
    DeviceWrapper wrapper = new DeviceWrapper(device, trackIndex, pluginIndex);

    // Wire the Mod button callback
    if (device instanceof DevicePanel) {
      ((DevicePanel) device).modToggleCallback = wrapper::toggleMod;
    } else if (device instanceof EqDevicePanel) {
      ((EqDevicePanel) device).modToggleCallback = wrapper::toggleMod;
    } else if (device instanceof CompressorDevicePanel) {
      ((CompressorDevicePanel) device).modToggleCallback = wrapper::toggleMod;
    } else if (device instanceof MaximDevicePanel) {
      ((MaximDevicePanel) device).modToggleCallback = wrapper::toggleMod;
    } else if (device instanceof ThreeOscDevicePanel) {
      ((ThreeOscDevicePanel) device).modToggleCallback = wrapper::toggleMod;
    } else if (device instanceof SamplerDevicePanel) {
      ((SamplerDevicePanel) device).modToggleCallback = wrapper::toggleMod;
    } else if (device instanceof DelayDevicePanel) {
      ((DelayDevicePanel) device).modToggleCallback = wrapper::toggleMod;
    } else if (device instanceof ReverbDevicePanel) {
      ((ReverbDevicePanel) device).modToggleCallback = wrapper::toggleMod;
    } else if (device instanceof LimiterDevicePanel) {
      ((LimiterDevicePanel) device).modToggleCallback = wrapper::toggleMod;
    } else if (device instanceof HottDevicePanel) {
      ((HottDevicePanel) device).modToggleCallback = wrapper::toggleMod;
    } else if (device instanceof EnvelopeShaperDevicePanel) {
      ((EnvelopeShaperDevicePanel) device).modToggleCallback = wrapper::toggleMod;
    } else if (device instanceof PhaserDevicePanel) {
      ((PhaserDevicePanel) device).modToggleCallback = wrapper::toggleMod;
    } else if (device instanceof ConvolverDevicePanel) {
      ((ConvolverDevicePanel) device).modToggleCallback = wrapper::toggleMod;
    } else if (device instanceof AuxDevicePanel) {
      ((AuxDevicePanel) device).modToggleCallback = wrapper::toggleMod;
    }

    return wrapper;
  }

  private void rebuildDeviceChain() {
    deviceChainContent.removeAll();

    // Get device panels for the selected track
    Map<Integer, DevicePanel> devicePanels =
        trackDevicePanels.getOrDefault(selectedTrack, java.util.Collections.emptyMap());
    Map<Integer, JPanel> builtins =
        builtinPanels.getOrDefault(selectedTrack, java.util.Collections.emptyMap());

    // Merge all panels by plugin index order
    TreeMap<Integer, JPanel> allPanels = new TreeMap<>();
    for (var e : devicePanels.entrySet()) allPanels.put(e.getKey(), e.getValue());
    for (var e : builtins.entrySet()) allPanels.put(e.getKey(), e.getValue());

    // Separate instrument from effects
    JPanel instrument = null;
    int instrumentIdx = -1;
    List<java.util.Map.Entry<Integer, JPanel>> effects = new ArrayList<>();
    for (var entry : allPanels.entrySet()) {
      JPanel p = entry.getValue();
      if (p instanceof DevicePanel && ((DevicePanel) p).isInstrument) {
        instrument = p;
        instrumentIdx = entry.getKey();
      } else {
        effects.add(entry);
      }
    }

    // Add waveform if any
    if (waveformPanel.hasData()) {
      deviceChainContent.add(waveformPanel);
    }

    // Add instrument if any
    if (instrument != null) {
      deviceChainContent.add(wrapDevice(instrument, selectedTrack, instrumentIdx));
    }

    // Add effects (VST3 + built-in) in plugin index order
    for (var entry : effects) {
      deviceChainContent.add(wrapDevice(entry.getValue(), selectedTrack, entry.getKey()));
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
    Runnable modToggleCallback;
    private boolean updatingFromBackend = false;

    DevicePanel(int trackIndex, int pluginIndex, String pluginName, boolean isInstrument) {
      this.trackIndex = trackIndex;
      this.pluginIndex = pluginIndex;
      this.pluginName = pluginName;
      this.isInstrument = isInstrument;

      setLayout(new BorderLayout());
      setPreferredSize(
          new Dimension(Theme.getInstance().scale(300), Theme.getInstance().scale(220)));
      setMaximumSize(new Dimension(Theme.getInstance().scale(300), Short.MAX_VALUE));
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

      JToggleButton onBtn = new JToggleButton("On", true);
      onBtn.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(9.0f)));
      onBtn.setFocusPainted(false);
      onBtn.setToolTipText("Enable/bypass this plugin");
      onBtn.addActionListener(
          e -> {
            boolean on = onBtn.isSelected();
            BackendManager.getInstance()
                .sendRequest(
                    Request.newBuilder()
                        .setPlugin(
                            PluginCmd.newBuilder()
                                .setAction(PluginCmd.Action.ACTION_SET_BYPASS)
                                .setTarget(
                                    EntityRef.newBuilder()
                                        .setTrackIndex(trackIndex)
                                        .setPluginIndex(pluginIndex))
                                .setFlag(on))
                        .build());
          });
      btnPanel.add(onBtn);

      JButton modBtn = new JButton("Mod");
      modBtn.addActionListener(
          e -> {
            if (modToggleCallback != null) modToggleCallback.run();
          });
      btnPanel.add(modBtn);

      // Sidechain source selector
      JButton scBtn = new JButton("SC");
      scBtn.setToolTipText("Sidechain Source");
      scBtn.addActionListener(e -> showSidechainMenu(scBtn));
      btnPanel.add(scBtn);

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

    private void showSidechainMenu(JButton source) {
      PluginPane.showSidechainPopup(source, trackIndex, pluginIndex);
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

      // If a ModulationPanel is in assign mode, complete the assignment instead
      DeviceWrapper wrapper = findWrapper(trackIndex, pluginIndex);
      if (wrapper != null && wrapper.modPanel.isAssigning()) {
        wrapper.modPanel.completeAssign(paramId, name);
        return;
      }

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

  /** Handle modulation info notification: update the wrapper's modPanel */
  private void handleModulationInfo(
      int trackIndex, int pluginIndex, java.util.List<ModulationSlotInfo> slots) {
    SwingUtilities.invokeLater(
        () -> {
          DeviceWrapper wrapper = findWrapper(trackIndex, pluginIndex);
          if (wrapper != null) {
            wrapper.modPanel.updateFromNotification(slots);
          }
        });
  }

  private void handleDrumPadNotification(hibiki.pb.notifications.DrumPadNotification notif) {
    SwingUtilities.invokeLater(
        () -> {
          int trackIdx = notif.getTrackIndex();
          int pluginIdx = notif.getPluginIndex();
          Map<Integer, JPanel> bi = builtinPanels.get(trackIdx);
          if (bi != null) {
            JPanel panel = bi.get(pluginIdx);
            if (panel instanceof DrumMachineDevicePanel) {
              ((DrumMachineDevicePanel) panel).handlePadNotification(notif);
            }
          }
        });
  }
}
