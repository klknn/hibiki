package hibiki.ui.panels.devices;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.ParamInfo;
import hibiki.pb.notifications.DrumPadNotification;
import hibiki.ui.Theme;
import java.awt.*;
import java.awt.datatransfer.DataFlavor;
import java.awt.dnd.*;
import java.awt.event.*;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import javax.swing.*;
import javax.swing.filechooser.FileNameExtensionFilter;

public class DrumMachineDevicePanel extends AbstractDevicePanel {
  private static final int TOTAL_PARAMS = 3; // Master Vol, Pan, Enable
  private static final int NUM_PADS = 64;

  public static class PadEffectUIState {
    public String effectPath = "";
    public List<ParamInfo> effectParams = new ArrayList<>();
  }

  public static class PadUIState {
    public String pluginPath = "";
    public String effectPath = "";
    public float volume = 1.0f;
    public float pan = 0.0f;
    public boolean mute = false;
    public boolean solo = false;
    public String sampleName = "";
    public List<ParamInfo> params = new ArrayList<>();
    public List<ParamInfo> effectParams = new ArrayList<>();
    public int triggerNote = 60;
    public List<PadEffectUIState> effects = new ArrayList<>();
  }

  public int currentBank = 0; // 0=A, 1=B, 2=C, 3=D
  public int selectedPad = 0; // 0..63
  public final PadUIState[] pads = new PadUIState[NUM_PADS];
  final JButton[] padButtons = new JButton[16];
  final boolean[] flashing = new boolean[16];
  public boolean globalSelected = false;
  final JToggleButton[] bankButtons = new JToggleButton[5];

  final JLabel selectedPadLabel;
  final JComboBox<String> pluginCombo;
  final JSlider padVolSlider;
  final JSlider padPanSlider;
  final JToggleButton padMuteBtn;
  final JToggleButton padSoloBtn;
  final JSpinner padTriggerNoteSpinner;
  final JLabel padTriggerNoteLabel;

  final JPanel samplerLoadPanel;
  final JLabel sampleNameLabel;
  final JPanel paramListPanel;
  final JScrollPane paramScrollPane;
  JToggleButton editBtn;

  private AbstractDevicePanel childUiPanel = null;
  private JPanel childUiContainer = null;
  private boolean showingChildUi = false;
  private boolean childUiIsEffect = false;
  private int childUiEffectIndex = -1;
  private int childUiPadIndex = -1;

  private boolean updatingUi = false;

  private static final String[] NOTE_NAMES = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
  };

  public DrumMachineDevicePanel(int trackIndex, int pluginIndex) {
    super(trackIndex, pluginIndex, TOTAL_PARAMS);

    for (int i = 0; i < NUM_PADS; ++i) {
      pads[i] = new PadUIState();
    }

    setLayout(new BorderLayout());
    Theme theme = Theme.getInstance();
    setPreferredSize(new Dimension(theme.scale(640), theme.scale(330)));
    setMaximumSize(new Dimension(theme.scale(640), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Header
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x3B3B6D));
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));
    JLabel nameLabel = new JLabel("Drum Machine");
    nameLabel.setForeground(Color.WHITE);
    nameLabel.setFont(theme.FONT_UI_BOLD);
    header.add(nameLabel, BorderLayout.CENTER);

    JPanel headerRight = new JPanel(new FlowLayout(FlowLayout.RIGHT, 5, 0));
    headerRight.setOpaque(false);

    JButton delBtn = new JButton("\u274C");
    delBtn.addActionListener(e -> sendRemove());
    headerRight.add(delBtn);
    header.add(headerRight, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // Left Panel: Grid and Banks
    JPanel leftPanel = new JPanel(new BorderLayout());
    leftPanel.setBackground(theme.BG_MEDIUM);
    leftPanel.setBorder(BorderFactory.createEmptyBorder(5, 5, 5, 5));

    // Bank Selector Tabs (A, B, C, D, Global)
    JPanel bankPanel = new JPanel(new GridLayout(1, 5, 2, 0));
    bankPanel.setOpaque(false);
    ButtonGroup bankGroup = new ButtonGroup();
    String[] bankNames = {"Bank A", "Bank B", "Bank C", "Bank D", "Global"};
    for (int b = 0; b < 5; ++b) {
      final int bankIdx = b;
      JToggleButton bankBtn = new JToggleButton(bankNames[b], b == 0);
      bankBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
      bankBtn.setFocusPainted(false);
      bankBtn.addActionListener(
          e -> {
            if (bankIdx == 4) {
              globalSelected = true;
            } else {
              globalSelected = false;
              currentBank = bankIdx;
              refreshGrid();
            }
            refreshDetailEditor();
          });
      bankGroup.add(bankBtn);
      bankPanel.add(bankBtn);
      bankButtons[b] = bankBtn;
    }
    leftPanel.add(bankPanel, BorderLayout.NORTH);

    // Grid Panel (4x4)
    JPanel gridPanel = new JPanel(new GridLayout(4, 4, 4, 4));
    gridPanel.setBackground(theme.BG_MEDIUM);
    gridPanel.setBorder(BorderFactory.createEmptyBorder(5, 0, 0, 0));

    for (int i = 0; i < 16; ++i) {
      final int gridIdx = i;
      padButtons[i] =
          new JButton() {
            @Override
            protected void paintComponent(Graphics g) {
              int indexInPads = currentBank * 16 + gridIdx;
              if (flashing[gridIdx]) {
                g.setColor(new Color(0x33B5E5));
              } else if (!globalSelected && indexInPads == selectedPad) {
                g.setColor(new Color(0x4A4A7A));
              } else {
                g.setColor(
                    pads[indexInPads].pluginPath.isEmpty()
                        ? new Color(0x2E2E2E)
                        : new Color(0x424242));
              }
              g.fillRect(0, 0, getWidth(), getHeight());
              super.paintComponent(g);
            }
          };
      padButtons[i].setOpaque(false);
      padButtons[i].setContentAreaFilled(false);
      padButtons[i].setFocusPainted(false);
      padButtons[i].setBorder(BorderFactory.createLineBorder(theme.BORDER));
      padButtons[i].setForeground(Color.WHITE);
      padButtons[i].setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));

      padButtons[i].addActionListener(
          e -> {
            selectedPad = currentBank * 16 + gridIdx;
            globalSelected = false;
            bankButtons[currentBank].setSelected(true);
            // Preview note locally or trigger backend note-on
            triggerPadNote(selectedPad);
            refreshGrid();
            refreshDetailEditor();
          });

      // Setup Drag & Drop for loading samples or plugins on pads
      if (!java.awt.GraphicsEnvironment.isHeadless()) {
        final int targetPadIndex = gridIdx;
        new DropTarget(
            padButtons[i],
            new DropTargetAdapter() {
              @Override
              public void dragEnter(DropTargetDragEvent dtde) {
                if (dtde.isDataFlavorSupported(DataFlavor.stringFlavor)
                    || dtde.isDataFlavorSupported(DataFlavor.javaFileListFlavor)) {
                  dtde.acceptDrag(DnDConstants.ACTION_COPY);
                } else {
                  dtde.rejectDrag();
                }
              }

              @Override
              public void drop(DropTargetDropEvent dtde) {
                try {
                  int finalPadIdx = currentBank * 16 + targetPadIndex;
                  if (dtde.isDataFlavorSupported(DataFlavor.stringFlavor)) {
                    dtde.acceptDrop(DnDConstants.ACTION_COPY);
                    String data =
                        (String) dtde.getTransferable().getTransferData(DataFlavor.stringFlavor);
                    dtde.dropComplete(true);
                    handleStringDrop(finalPadIdx, data);
                  } else if (dtde.isDataFlavorSupported(DataFlavor.javaFileListFlavor)) {
                    dtde.acceptDrop(DnDConstants.ACTION_COPY);
                    @SuppressWarnings("unchecked")
                    java.util.List<File> files =
                        (java.util.List<File>)
                            dtde.getTransferable().getTransferData(DataFlavor.javaFileListFlavor);
                    dtde.dropComplete(true);
                    handleFileListDrop(finalPadIdx, files);
                  }
                } catch (Exception ex) {
                  dtde.rejectDrop();
                }
              }
            });
      }

      gridPanel.add(padButtons[i]);
    }
    leftPanel.add(gridPanel, BorderLayout.CENTER);
    add(leftPanel, BorderLayout.CENTER);

    // Right Panel: Detail Editor split from Grid
    JPanel rightPanel = new JPanel(new BorderLayout());
    rightPanel.setBackground(theme.BG_MEDIUM);
    rightPanel.setPreferredSize(new Dimension(theme.scale(260), 0));
    rightPanel.setBorder(BorderFactory.createMatteBorder(0, 1, 0, 0, theme.BORDER));

    // Pad Selection details (Top)
    JPanel detailTop = new JPanel(new GridLayout(3, 1, 2, 2));
    detailTop.setBackground(theme.BG_MEDIUM);
    detailTop.setBorder(BorderFactory.createEmptyBorder(5, 5, 5, 5));

    selectedPadLabel = new JLabel("Selected: Pad 1 (C1)");
    selectedPadLabel.setForeground(Color.WHITE);
    selectedPadLabel.setFont(theme.FONT_UI_BOLD);
    detailTop.add(selectedPadLabel);

    JPanel pluginSelRow = new JPanel(new BorderLayout(5, 0));
    pluginSelRow.setOpaque(false);
    JLabel pluginLabel = new JLabel("Instrument:");
    pluginLabel.setForeground(new Color(0xCCCCCC));
    pluginLabel.setFont(theme.FONT_UI);
    pluginSelRow.add(pluginLabel, BorderLayout.WEST);

    pluginCombo =
        new JComboBox<>(
            new String[] {
              "Empty",
              "Sampler",
              "3xOsc",
              "Acid Bass",
              "DR8 Kick",
              "DR8 Snare",
              "DR8 Hat",
              "DR8 Tom",
              "DR8 Clap",
              "DR8 Cowbell",
              "DR8 Crash",
              "DR8 Rimshot",
              "DR8 Conga",
              "Organ",
              "Film",
              "Load VST3..."
            });
    pluginCombo.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    pluginCombo.addActionListener(
        e -> {
          if (updatingUi) return;
          String sel = (String) pluginCombo.getSelectedItem();
          if ("Load VST3...".equals(sel)) {
            if (GraphicsEnvironment.isHeadless()) {
              return;
            }
            JFileChooser chooser = new JFileChooser();
            chooser.setFileSelectionMode(JFileChooser.FILES_AND_DIRECTORIES);
            chooser.setFileFilter(new FileNameExtensionFilter("VST3 Plugins", "vst3"));
            int res = chooser.showOpenDialog(DrumMachineDevicePanel.this);
            if (res == JFileChooser.APPROVE_OPTION) {
              String vst3Path = chooser.getSelectedFile().getAbsolutePath();
              sendDrumPadCmd(
                  DrumPadCmd.Action.ACTION_LOAD_PLUGIN, selectedPad, vst3Path, 0, 0, "", false);
            } else {
              refreshDetailEditor(); // Reset combo selection to actual value
            }
            return;
          }

          String path = "";
          if ("Sampler".equals(sel)) path = "builtin://sampler";
          else if ("3xOsc".equals(sel)) path = "builtin://3xosc";
          else if ("Acid Bass".equals(sel)) path = "builtin://acid_bass";
          else if ("DR8 Kick".equals(sel)) path = "builtin://dr8_kick";
          else if ("DR8 Snare".equals(sel)) path = "builtin://dr8_snare";
          else if ("DR8 Hat".equals(sel)) path = "builtin://dr8_hat";
          else if ("DR8 Tom".equals(sel)) path = "builtin://dr8_tom";
          else if ("DR8 Clap".equals(sel)) path = "builtin://dr8_clap";
          else if ("DR8 Cowbell".equals(sel)) path = "builtin://dr8_cowbell";
          else if ("DR8 Crash".equals(sel)) path = "builtin://dr8_crash";
          else if ("DR8 Rimshot".equals(sel)) path = "builtin://dr8_rim";
          else if ("DR8 Conga".equals(sel)) path = "builtin://dr8_conga";
          else if ("Organ".equals(sel)) path = "builtin://organ";
          else if ("Film".equals(sel)) path = "builtin://film";
          else if (!"Empty".equals(sel)) {
            path = pads[selectedPad].pluginPath;
          }

          if (path.isEmpty()) {
            sendDrumPadCmd(
                DrumPadCmd.Action.ACTION_REMOVE_PLUGIN, selectedPad, "", 0, 0, "", false);
          } else {
            sendDrumPadCmd(
                DrumPadCmd.Action.ACTION_LOAD_PLUGIN, selectedPad, path, 0, 0, "", false);
          }
        });
    pluginSelRow.add(pluginCombo, BorderLayout.CENTER);

    editBtn = new JToggleButton("Edit");
    editBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    editBtn.setFocusPainted(false);
    editBtn.addActionListener(
        e -> {
          PadUIState state = pads[selectedPad];
          Class<? extends AbstractDevicePanel> panelClass = getChildPanelClass(state.pluginPath);
          if (panelClass == null && !state.pluginPath.isEmpty()) {
            editBtn.setSelected(false);
            sendDrumPadCmd(DrumPadCmd.Action.ACTION_SHOW_GUI, selectedPad, "", 0, 0, "", 0, false);
          } else {
            showingChildUi = editBtn.isSelected();
            childUiIsEffect = false;
            childUiEffectIndex = -1;
            if (showingChildUi) {
              showChildUi();
            } else {
              hideChildUi();
            }
          }
        });
    pluginSelRow.add(editBtn, BorderLayout.EAST);
    detailTop.add(pluginSelRow);

    JPanel noteRow = new JPanel(new BorderLayout(5, 0));
    noteRow.setOpaque(false);
    JLabel noteLabel = new JLabel("Trigger Note:");
    noteLabel.setForeground(new Color(0xCCCCCC));
    noteLabel.setFont(theme.FONT_UI);
    noteRow.add(noteLabel, BorderLayout.WEST);

    padTriggerNoteLabel = new JLabel("C4");
    padTriggerNoteLabel.setForeground(Color.WHITE);
    padTriggerNoteLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    padTriggerNoteLabel.setPreferredSize(new Dimension(theme.scale(35), 0));

    SpinnerModel noteModel = new SpinnerNumberModel(60, 0, 127, 1);
    padTriggerNoteSpinner = new JSpinner(noteModel);
    padTriggerNoteSpinner.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    padTriggerNoteSpinner.addChangeListener(
        e -> {
          if (updatingUi) return;
          int noteVal = (Integer) padTriggerNoteSpinner.getValue();
          padTriggerNoteLabel.setText(getNoteName(noteVal));
          sendDrumPadCmd(
              DrumPadCmd.Action.ACTION_SET_TRIGGER_NOTE, selectedPad, "", 0, 0, "", noteVal);
        });

    JPanel spinnerContainer = new JPanel(new BorderLayout(5, 0));
    spinnerContainer.setOpaque(false);
    spinnerContainer.add(padTriggerNoteSpinner, BorderLayout.CENTER);
    spinnerContainer.add(padTriggerNoteLabel, BorderLayout.EAST);

    noteRow.add(spinnerContainer, BorderLayout.CENTER);
    detailTop.add(noteRow);

    rightPanel.add(detailTop, BorderLayout.NORTH);

    // Mix and parameters container
    JPanel detailCenter = new JPanel();
    detailCenter.setLayout(new BoxLayout(detailCenter, BoxLayout.Y_AXIS));
    detailCenter.setBackground(theme.BG_MEDIUM);

    // Mixer section
    JPanel mixPanel = new JPanel(new GridLayout(2, 2, 5, 2));
    mixPanel.setBackground(theme.BG_MEDIUM);
    mixPanel.setBorder(
        BorderFactory.createTitledBorder(BorderFactory.createLineBorder(theme.BORDER), "Mixer"));
    ((javax.swing.border.TitledBorder) mixPanel.getBorder()).setTitleColor(Color.LIGHT_GRAY);
    ((javax.swing.border.TitledBorder) mixPanel.getBorder())
        .setTitleFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));

    padVolSlider = new JSlider(0, 100, 100);
    padVolSlider.setOpaque(false);
    padVolSlider.addChangeListener(
        e -> {
          if (updatingUi) return;
          if (globalSelected) {
            sendParam(0, padVolSlider.getValue() / 100.0);
          } else {
            sendDrumPadCmd(
                DrumPadCmd.Action.ACTION_SET_VOLUME,
                selectedPad,
                "",
                0,
                padVolSlider.getValue() / 100.0,
                "");
          }
        });
    JPanel volRow = new JPanel(new BorderLayout());
    volRow.setOpaque(false);
    JLabel volLbl = new JLabel("Vol");
    volLbl.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    volLbl.setForeground(Color.LIGHT_GRAY);
    volRow.add(volLbl, BorderLayout.WEST);
    volRow.add(padVolSlider, BorderLayout.CENTER);
    mixPanel.add(volRow);

    padPanSlider = new JSlider(-50, 50, 0);
    padPanSlider.setOpaque(false);
    padPanSlider.addChangeListener(
        e -> {
          if (updatingUi) return;
          if (globalSelected) {
            sendParam(1, (padPanSlider.getValue() + 50) / 100.0);
          } else {
            sendDrumPadCmd(
                DrumPadCmd.Action.ACTION_SET_PAN,
                selectedPad,
                "",
                0,
                padPanSlider.getValue() / 50.0,
                "");
          }
        });
    JPanel panRow = new JPanel(new BorderLayout());
    panRow.setOpaque(false);
    JLabel panLbl = new JLabel("Pan");
    panLbl.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    panLbl.setForeground(Color.LIGHT_GRAY);
    panRow.add(panLbl, BorderLayout.WEST);
    panRow.add(padPanSlider, BorderLayout.CENTER);
    mixPanel.add(panRow);

    padMuteBtn = new JToggleButton("Mute");
    padMuteBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    padMuteBtn.setFocusPainted(false);
    padMuteBtn.addActionListener(
        e -> {
          if (updatingUi) return;
          if (globalSelected) {
            sendParam(2, padMuteBtn.isSelected() ? 1.0 : 0.0);
          } else {
            sendDrumPadCmd(
                DrumPadCmd.Action.ACTION_SET_MUTE,
                selectedPad,
                "",
                0,
                padMuteBtn.isSelected() ? 1.0 : 0.0,
                "");
          }
        });
    mixPanel.add(padMuteBtn);

    padSoloBtn = new JToggleButton("Solo");
    padSoloBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    padSoloBtn.setFocusPainted(false);
    padSoloBtn.addActionListener(
        e -> {
          if (updatingUi) return;
          if (globalSelected) {
            // Solo is not mapped globally
          } else {
            sendDrumPadCmd(
                DrumPadCmd.Action.ACTION_SET_SOLO,
                selectedPad,
                "",
                0,
                padSoloBtn.isSelected() ? 1.0 : 0.0,
                "");
          }
        });
    mixPanel.add(padSoloBtn);

    detailCenter.add(mixPanel);

    // Sampler Load panel (Loads WAV sample if Sampler is chosen)
    samplerLoadPanel = new JPanel(new BorderLayout(5, 0));
    samplerLoadPanel.setBackground(theme.BG_MEDIUM);
    samplerLoadPanel.setBorder(BorderFactory.createEmptyBorder(5, 5, 5, 5));
    sampleNameLabel = new JLabel("(no sample)");
    sampleNameLabel.setForeground(Color.LIGHT_GRAY);
    sampleNameLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    samplerLoadPanel.add(sampleNameLabel, BorderLayout.CENTER);

    JButton padLoadBtn = new JButton("Load Wav");
    padLoadBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    padLoadBtn.addActionListener(
        e -> {
          JFileChooser fc = new JFileChooser(".");
          fc.setFileFilter(new FileNameExtensionFilter("WAV Audio", "wav"));
          if (fc.showOpenDialog(this) == JFileChooser.APPROVE_OPTION) {
            sendDrumPadCmd(
                DrumPadCmd.Action.ACTION_LOAD_SAMPLE,
                selectedPad,
                "",
                0,
                0,
                fc.getSelectedFile().getAbsolutePath());
          }
        });
    samplerLoadPanel.add(padLoadBtn, BorderLayout.EAST);
    detailCenter.add(samplerLoadPanel);

    // Parameter sliders panel (Shows parameters of selected pad plugin)
    paramListPanel = new JPanel();
    paramListPanel.setLayout(new BoxLayout(paramListPanel, BoxLayout.Y_AXIS));
    paramListPanel.setBackground(theme.BG_DARK);

    paramScrollPane = new JScrollPane(paramListPanel);
    paramScrollPane.setBorder(
        BorderFactory.createTitledBorder(
            BorderFactory.createLineBorder(theme.BORDER), "Effect Chain"));
    ((javax.swing.border.TitledBorder) paramScrollPane.getBorder()).setTitleColor(Color.LIGHT_GRAY);
    ((javax.swing.border.TitledBorder) paramScrollPane.getBorder())
        .setTitleFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    paramScrollPane.setBackground(theme.BG_DARK);
    detailCenter.add(paramScrollPane);

    // Drag & drop support for the Effect Chain
    if (!java.awt.GraphicsEnvironment.isHeadless()) {
      DropTargetListener dtl =
          new DropTargetAdapter() {
            @Override
            public void dragEnter(DropTargetDragEvent dtde) {
              if (dtde.isDataFlavorSupported(DataFlavor.stringFlavor)
                  || dtde.isDataFlavorSupported(DataFlavor.javaFileListFlavor)) {
                dtde.acceptDrag(DnDConstants.ACTION_COPY);
              } else {
                dtde.rejectDrag();
              }
            }

            @Override
            public void drop(DropTargetDropEvent dtde) {
              try {
                if (dtde.isDataFlavorSupported(DataFlavor.stringFlavor)) {
                  dtde.acceptDrop(DnDConstants.ACTION_COPY);
                  String data =
                      (String) dtde.getTransferable().getTransferData(DataFlavor.stringFlavor);
                  dtde.dropComplete(true);
                  handleEffectDrop(selectedPad, data);
                } else if (dtde.isDataFlavorSupported(DataFlavor.javaFileListFlavor)) {
                  dtde.acceptDrop(DnDConstants.ACTION_COPY);
                  @SuppressWarnings("unchecked")
                  java.util.List<File> files =
                      (java.util.List<File>)
                          dtde.getTransferable().getTransferData(DataFlavor.javaFileListFlavor);
                  dtde.dropComplete(true);
                  handleEffectFileListDrop(selectedPad, files);
                }
              } catch (Exception ex) {
                dtde.rejectDrop();
              }
            }
          };
      new DropTarget(paramScrollPane, dtl);
      new DropTarget(paramListPanel, dtl);
    }

    rightPanel.add(detailCenter, BorderLayout.CENTER);

    JPanel eastWrapper = new JPanel(new BorderLayout());
    eastWrapper.setOpaque(false);
    eastWrapper.add(rightPanel, BorderLayout.CENTER);

    childUiContainer = new JPanel(new BorderLayout());
    childUiContainer.setOpaque(false);
    childUiContainer.setVisible(false);
    eastWrapper.add(childUiContainer, BorderLayout.EAST);

    add(eastWrapper, BorderLayout.EAST);

    // Initial state
    refreshGrid();
    refreshDetailEditor();
  }

  void handleStringDrop(int padIdx, String data) {
    String[] parts = data.split(":", 2);
    if (parts.length == 2) {
      if ("audio".equals(parts[0])) {
        String path = parts[1];
        loadAudioSample(padIdx, path);
      } else if ("builtin".equals(parts[0])) {
        String pluginPath = parts[1];
        if (pluginPath.startsWith("builtin://")) {
          sendDrumPadCmd(DrumPadCmd.Action.ACTION_LOAD_PLUGIN, padIdx, pluginPath, 0, 0, "");
        }
      } else if ("vst".equals(parts[0])
          || "remote-vst".equals(parts[0])
          || "plugin".equals(parts[0])) {
        String pluginPath = parts[1];
        sendDrumPadCmd(DrumPadCmd.Action.ACTION_LOAD_PLUGIN, padIdx, pluginPath, 0, 0, "");
      }
    }
  }

  public void loadAudioSample(int padIdx, String path) {
    // First load Sampler if not already loaded, then load sample
    if (!"builtin://sampler".equals(pads[padIdx].pluginPath)) {
      sendDrumPadCmd(DrumPadCmd.Action.ACTION_LOAD_PLUGIN, padIdx, "builtin://sampler", 0, 0, "");
    }
    sendDrumPadCmd(DrumPadCmd.Action.ACTION_LOAD_SAMPLE, padIdx, "", 0, 0, path);
  }

  void handleFileListDrop(int padIdx, java.util.List<File> files) {
    for (File f : files) {
      String name = f.getName().toLowerCase();
      if (name.endsWith(".vst3")) {
        sendDrumPadCmd(DrumPadCmd.Action.ACTION_LOAD_PLUGIN, padIdx, f.getAbsolutePath(), 0, 0, "");
        break;
      } else if (name.endsWith(".wav") || name.endsWith(".aiff") || name.endsWith(".flac")) {
        loadAudioSample(padIdx, f.getAbsolutePath());
        break;
      }
    }
  }

  void handleEffectDrop(int padIdx, String data) {
    String[] parts = data.split(":", 2);
    if (parts.length == 2) {
      String type = parts[0];
      String path = parts[1];
      if ("builtin".equals(type)
          || "vst".equals(type)
          || "remote-vst".equals(type)
          || "plugin".equals(type)) {
        int effectIdx = pads[padIdx].effects.size();
        sendDrumPadCmd(
            DrumPadCmd.Action.ACTION_LOAD_PLUGIN, padIdx, path, 0, 0, "", effectIdx, true);
      }
    }
  }

  void handleEffectFileListDrop(int padIdx, java.util.List<File> files) {
    for (File f : files) {
      String name = f.getName().toLowerCase();
      if (name.endsWith(".vst3")) {
        int effectIdx = pads[padIdx].effects.size();
        sendDrumPadCmd(
            DrumPadCmd.Action.ACTION_LOAD_PLUGIN,
            padIdx,
            f.getAbsolutePath(),
            0,
            0,
            "",
            effectIdx,
            true);
        break;
      }
    }
  }

  private String getPluginDisplayName(String path) {
    if (path == null || path.isEmpty()) return "Empty";
    if ("builtin://sampler".equals(path)) return "Sampler";
    if ("builtin://3xosc".equals(path)) return "3xOsc";
    if ("builtin://acid_bass".equals(path)) return "Acid Bass";
    if ("builtin://dr8_kick".equals(path)) return "DR8 Kick";
    if ("builtin://dr8_snare".equals(path)) return "DR8 Snare";
    if ("builtin://dr8_hat".equals(path)) return "DR8 Hat";
    if ("builtin://dr8_tom".equals(path)) return "DR8 Tom";
    if ("builtin://dr8_clap".equals(path)) return "DR8 Clap";
    if ("builtin://dr8_cowbell".equals(path)) return "DR8 Cowbell";
    if ("builtin://dr8_crash".equals(path)) return "DR8 Crash";
    if ("builtin://dr8_rim".equals(path)) return "DR8 Rimshot";
    if ("builtin://dr8_conga".equals(path)) return "DR8 Conga";
    if ("builtin://organ".equals(path)) return "Organ";
    if ("builtin://film".equals(path)) return "Film";
    if ("builtin://eq".equals(path)) return "EQ Eight";
    if ("builtin://compressor".equals(path)) return "Compressor";
    if ("builtin://delay".equals(path)) return "Delay";
    if ("builtin://reverb".equals(path)) return "Reverb";
    if ("builtin://limiter".equals(path)) return "Limiter";
    if ("builtin://maxim".equals(path)) return "Maxim";
    if ("builtin://hott".equals(path)) return "Hott";
    if ("builtin://envelope_shaper".equals(path)) return "EnvShaper";
    if ("builtin://phaser".equals(path)) return "Phaser";
    if ("builtin://convolver".equals(path)) return "Convolver";
    if ("builtin://bitcrusher".equals(path)) return "Bitcrusher";
    if ("builtin://chorus".equals(path)) return "Chorus";
    if ("builtin://stereo_width".equals(path)) return "Stereo Width";
    if ("builtin://vocodey".equals(path)) return "Vocodey";

    int slash = path.lastIndexOf('/');
    if (slash == -1) slash = path.lastIndexOf('\\');
    String name = (slash != -1) ? path.substring(slash + 1) : path;
    if (name.endsWith(".vst3")) {
      name = name.substring(0, name.length() - 5);
    }
    return name;
  }

  public void sendDrumPadCmd(
      DrumPadCmd.Action action,
      int padIdx,
      String pluginPath,
      int paramId,
      double paramValue,
      String samplePath) {
    sendDrumPadCmd(action, padIdx, pluginPath, paramId, paramValue, samplePath, -1, 0, false);
  }

  public void sendDrumPadCmd(
      DrumPadCmd.Action action,
      int padIdx,
      String pluginPath,
      int paramId,
      double paramValue,
      String samplePath,
      boolean targetEffect) {
    sendDrumPadCmd(
        action, padIdx, pluginPath, paramId, paramValue, samplePath, -1, 0, targetEffect);
  }

  public void sendDrumPadCmd(
      DrumPadCmd.Action action,
      int padIdx,
      String pluginPath,
      int paramId,
      double paramValue,
      String samplePath,
      int effectIdx,
      boolean targetEffect) {
    sendDrumPadCmd(
        action, padIdx, pluginPath, paramId, paramValue, samplePath, -1, effectIdx, targetEffect);
  }

  public void sendDrumPadCmd(
      DrumPadCmd.Action action,
      int padIdx,
      String pluginPath,
      int paramId,
      double paramValue,
      String samplePath,
      int triggerNote) {
    sendDrumPadCmd(
        action, padIdx, pluginPath, paramId, paramValue, samplePath, triggerNote, 0, false);
  }

  public void sendDrumPadCmd(
      DrumPadCmd.Action action,
      int padIdx,
      String pluginPath,
      int paramId,
      double paramValue,
      String samplePath,
      int triggerNote,
      int effectIdx,
      boolean targetEffect) {
    DrumPadCmd.Builder builder =
        DrumPadCmd.newBuilder()
            .setAction(action)
            .setTrackIndex(trackIndex)
            .setPluginIndex(pluginIndex)
            .setPadIndex(padIdx)
            .setPluginPath(pluginPath != null ? pluginPath : "")
            .setParamId(paramId)
            .setParamValue((float) paramValue)
            .setSamplePath(samplePath != null ? samplePath : "")
            .setTargetEffect(targetEffect)
            .setEffectIndex(effectIdx);
    if (triggerNote >= 0) {
      builder.setTriggerNote(triggerNote);
    }
    BackendManager.getInstance().sendRequest(Request.newBuilder().setDrumPad(builder).build());
  }

  private void triggerPadNote(int padIdx) {
    // Send a temporary MIDI event note-on and note-off to preview the pad
    int pitch = 36 + padIdx;
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setSendVirtualMidi(
                    SendVirtualMidi.newBuilder()
                        .setTrackIndex(trackIndex)
                        .setNote(pitch)
                        .setVelocity(100)
                        .setNoteOn(true))
                .build());

    // Quick schedule a note-off after 100ms
    new javax.swing.Timer(
            100,
            e -> {
              BackendManager.getInstance()
                  .sendRequest(
                      Request.newBuilder()
                          .setSendVirtualMidi(
                              SendVirtualMidi.newBuilder()
                                  .setTrackIndex(trackIndex)
                                  .setNote(pitch)
                                  .setVelocity(0)
                                  .setNoteOn(false))
                          .build());
            })
        .start();
  }

  public void handlePadNotification(DrumPadNotification notif) {
    int padIdx = notif.getPadIndex();
    if (padIdx < 0 || padIdx >= NUM_PADS) return;

    if (notif.getType() == DrumPadNotification.Type.TYPE_PAD_TRIGGER) {
      // Trigger flash visual feedback
      int gridIdx = padIdx - (currentBank * 16);
      if (gridIdx >= 0 && gridIdx < 16) {
        flashing[gridIdx] = true;
        padButtons[gridIdx].repaint();

        Timer t =
            new Timer(
                80,
                ev -> {
                  flashing[gridIdx] = false;
                  padButtons[gridIdx].repaint();
                });
        t.setRepeats(false);
        t.start();
      }
    } else if (notif.getType() == DrumPadNotification.Type.TYPE_PAD_STATE) {
      // Update local pad state cache
      PadUIState state = pads[padIdx];
      state.pluginPath = notif.getPluginPath();
      state.effectPath = notif.getEffectPath();
      state.volume = notif.getVolume();
      state.pan = notif.getPan();
      state.mute = notif.getMute();
      state.solo = notif.getSolo();
      state.sampleName = notif.getSampleName().isEmpty() ? "(no sample)" : notif.getSampleName();
      state.params = new ArrayList<>(notif.getParamsList());
      state.effectParams = new ArrayList<>(notif.getEffectParamsList());
      state.triggerNote = notif.hasTriggerNote() ? notif.getTriggerNote() : 60;

      state.effects.clear();
      for (int i = 0; i < notif.getEffectsCount(); ++i) {
        var eff = notif.getEffects(i);
        PadEffectUIState effState = new PadEffectUIState();
        effState.effectPath = eff.getEffectPath();
        effState.effectParams = new ArrayList<>(eff.getParamsList());
        state.effects.add(effState);
      }

      // If visible grid cell changed, refresh label
      int gridIdx = padIdx - (currentBank * 16);
      if (gridIdx >= 0 && gridIdx < 16) {
        updateGridButtonText(gridIdx, padIdx);
        padButtons[gridIdx].repaint();
      }

      // If currently selected pad updated, refresh the detail editor
      if (padIdx == selectedPad) {
        refreshDetailEditor();
        if (showingChildUi && childUiPanel != null) {
          if (childUiIsEffect) {
            if (childUiEffectIndex >= 0 && childUiEffectIndex < state.effects.size()) {
              List<ParamInfo> activeParamsList = state.effects.get(childUiEffectIndex).effectParams;
              for (ParamInfo info : activeParamsList) {
                childUiPanel.handleParamChange(info.getId(), info.getCurrentValue());
              }
            }
          } else {
            List<ParamInfo> activeParamsList = state.params;
            for (ParamInfo info : activeParamsList) {
              childUiPanel.handleParamChange(info.getId(), info.getCurrentValue());
            }
          }
        }
      }
    }
  }

  private void refreshGrid() {
    for (int i = 0; i < 16; ++i) {
      int padIdx = currentBank * 16 + i;
      updateGridButtonText(i, padIdx);
    }
  }

  private void updateGridButtonText(int gridIdx, int padIdx) {
    String pPath = pads[padIdx].pluginPath;
    String noteName = getNoteName(36 + padIdx);
    String dispName = getPluginDisplayName(pPath);

    padButtons[gridIdx].setText(
        "<html><center>Pad "
            + (padIdx + 1)
            + "<br><b>"
            + dispName
            + "</b><br><font color='#777777'>"
            + noteName
            + "</font></center></html>");
  }

  private void refreshDetailEditor() {
    if (updatingUi) return;
    updatingUi = true;

    if (globalSelected) {
      selectedPadLabel.setText("Selected: Global Controls");
      pluginCombo.setEnabled(false);
      pluginCombo.setSelectedItem("Drum Machine");
      editBtn.setEnabled(false);
      editBtn.setSelected(false);
      padTriggerNoteSpinner.setEnabled(false);
      padTriggerNoteLabel.setText("-");

      padVolSlider.setValue((int) (params[0] * 100));
      padPanSlider.setValue((int) (params[1] * 100 - 50));
      padMuteBtn.setText("Active");
      padMuteBtn.setSelected(params[2] > 0.5);
      padSoloBtn.setVisible(false);

      samplerLoadPanel.setVisible(false);

      paramListPanel.removeAll();
      Theme theme = Theme.getInstance();

      JPanel infoPanel = new JPanel();
      infoPanel.setLayout(new BoxLayout(infoPanel, BoxLayout.Y_AXIS));
      infoPanel.setOpaque(false);
      infoPanel.setBorder(BorderFactory.createEmptyBorder(15, 15, 15, 15));

      JLabel infoTitle = new JLabel("Global Controls Active");
      infoTitle.setForeground(theme.TEXT_BRIGHT);
      infoTitle.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(11.0f)));
      infoTitle.setAlignmentX(Component.CENTER_ALIGNMENT);

      JTextArea infoText =
          new JTextArea(
              "Double-click plugins in the browser to load them as global insert effects on this"
                  + " track.\n\n"
                  + "Master parameters (Volume, Pan, and Active state) are controlled above.");
      infoText.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
      infoText.setForeground(theme.TEXT_DIM);
      infoText.setLineWrap(true);
      infoText.setWrapStyleWord(true);
      infoText.setEditable(false);
      infoText.setOpaque(false);
      infoText.setAlignmentX(Component.CENTER_ALIGNMENT);

      infoPanel.add(infoTitle);
      infoPanel.add(Box.createVerticalStrut(10));
      infoPanel.add(infoText);

      paramListPanel.add(Box.createVerticalGlue());
      paramListPanel.add(infoPanel);
      paramListPanel.add(Box.createVerticalGlue());

      paramListPanel.revalidate();
      paramListPanel.repaint();
      paramScrollPane.revalidate();
      paramScrollPane.repaint();

      updatingUi = false;
      return;
    }

    pluginCombo.setEnabled(true);
    padTriggerNoteSpinner.setEnabled(true);
    padSoloBtn.setVisible(true);
    padMuteBtn.setText("Mute");

    PadUIState state = pads[selectedPad];
    selectedPadLabel.setText(
        "Selected: Pad " + (selectedPad + 1) + " (" + getNoteName(state.triggerNote) + ")");

    // Plugin dropdown
    if (state.pluginPath.isEmpty()) {
      pluginCombo.setSelectedItem("Empty");
    } else if ("builtin://sampler".equals(state.pluginPath)) {
      pluginCombo.setSelectedItem("Sampler");
    } else if ("builtin://3xosc".equals(state.pluginPath)) {
      pluginCombo.setSelectedItem("3xOsc");
    } else if ("builtin://acid_bass".equals(state.pluginPath)) {
      pluginCombo.setSelectedItem("Acid Bass");
    } else if ("builtin://dr8_kick".equals(state.pluginPath)) {
      pluginCombo.setSelectedItem("DR8 Kick");
    } else if ("builtin://dr8_snare".equals(state.pluginPath)) {
      pluginCombo.setSelectedItem("DR8 Snare");
    } else if ("builtin://dr8_hat".equals(state.pluginPath)) {
      pluginCombo.setSelectedItem("DR8 Hat");
    } else if ("builtin://dr8_tom".equals(state.pluginPath)) {
      pluginCombo.setSelectedItem("DR8 Tom");
    } else if ("builtin://dr8_clap".equals(state.pluginPath)) {
      pluginCombo.setSelectedItem("DR8 Clap");
    } else if ("builtin://dr8_cowbell".equals(state.pluginPath)) {
      pluginCombo.setSelectedItem("DR8 Cowbell");
    } else if ("builtin://dr8_crash".equals(state.pluginPath)) {
      pluginCombo.setSelectedItem("DR8 Crash");
    } else if ("builtin://dr8_rim".equals(state.pluginPath)) {
      pluginCombo.setSelectedItem("DR8 Rimshot");
    } else if ("builtin://dr8_conga".equals(state.pluginPath)) {
      pluginCombo.setSelectedItem("DR8 Conga");
    } else if ("builtin://organ".equals(state.pluginPath)) {
      pluginCombo.setSelectedItem("Organ");
    } else if (state.pluginPath.startsWith("builtin://film")) {
      pluginCombo.setSelectedItem("Film");
    } else {
      DefaultComboBoxModel<String> model = (DefaultComboBoxModel<String>) pluginCombo.getModel();
      String dispName = state.pluginPath;
      int slash = dispName.lastIndexOf('/');
      if (slash == -1) slash = dispName.lastIndexOf('\\');
      dispName = (slash != -1) ? dispName.substring(slash + 1) : dispName;
      if (dispName.endsWith(".vst3")) {
        dispName = dispName.substring(0, dispName.length() - 5);
      }
      boolean found = false;
      for (int i = 0; i < model.getSize(); ++i) {
        if (model.getElementAt(i).equals(dispName)) {
          found = true;
          break;
        }
      }
      if (!found) {
        model.insertElementAt(dispName, model.getSize() - 1);
      }
      pluginCombo.setSelectedItem(dispName);
    }

    // Mixer sliders
    padVolSlider.setValue((int) (state.volume * 100));
    padPanSlider.setValue((int) (state.pan * 50));
    padMuteBtn.setSelected(state.mute);
    padSoloBtn.setSelected(state.solo);

    // Trigger note
    padTriggerNoteSpinner.setValue(state.triggerNote);
    padTriggerNoteLabel.setText(getNoteName(state.triggerNote));

    // Sampler controls visibility
    boolean isSampler = "builtin://sampler".equals(state.pluginPath);
    samplerLoadPanel.setVisible(isSampler);
    sampleNameLabel.setText(state.sampleName);

    // Edit button and child UI state
    boolean hasPlugin = !state.pluginPath.isEmpty();

    if (childUiIsEffect) {
      if (childUiEffectIndex < 0 || childUiEffectIndex >= state.effects.size()) {
        showingChildUi = false;
        childUiIsEffect = false;
        childUiEffectIndex = -1;
      }
    }

    String childPath = "";
    if (showingChildUi) {
      if (childUiIsEffect) {
        childPath = state.effects.get(childUiEffectIndex).effectPath;
      } else {
        childPath = state.pluginPath;
      }
    }

    Class<? extends AbstractDevicePanel> clz = getChildPanelClass(childPath);
    if (clz == null) {
      showingChildUi = false;
    }
    editBtn.setEnabled(hasPlugin);
    editBtn.setSelected(showingChildUi && !childUiIsEffect && hasPlugin);

    boolean shouldShowChild =
        showingChildUi
            && (childUiIsEffect
                ? (childUiEffectIndex >= 0 && childUiEffectIndex < state.effects.size())
                : hasPlugin);
    if (shouldShowChild) {
      if (childUiPanel == null || childUiPadIndex != selectedPad || !clz.isInstance(childUiPanel)) {
        showChildUi();
      } else {
        List<ParamInfo> paramsList =
            childUiIsEffect ? state.effects.get(childUiEffectIndex).effectParams : state.params;
        for (ParamInfo info : paramsList) {
          childUiPanel.handleParamChange(info.getId(), info.getCurrentValue());
        }
      }
    } else {
      hideChildUi();
    }

    // Rebuild parameters list / effect chain
    paramListPanel.removeAll();
    Theme theme = Theme.getInstance();

    if (state.effects.isEmpty()) {
      // Show dashed placeholder with hover interactions
      JPanel placeholder =
          new JPanel() {
            private boolean hovered = false;

            {
              setOpaque(false);
              addMouseListener(
                  new MouseAdapter() {
                    public void mouseEntered(MouseEvent e) {
                      hovered = true;
                      repaint();
                    }

                    public void mouseExited(MouseEvent e) {
                      hovered = false;
                      repaint();
                    }
                  });
            }

            @Override
            protected void paintComponent(Graphics g) {
              super.paintComponent(g);
              Graphics2D g2 = (Graphics2D) g.create();
              g2.setRenderingHint(
                  RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
              if (hovered) {
                g2.setColor(theme.PANEL_BG_LIGHT);
                g2.fillRoundRect(5, 5, getWidth() - 10, getHeight() - 10, 8, 8);
                g2.setColor(theme.ACCENT_BLUE);
              } else {
                g2.setColor(theme.BG_DARK);
                g2.fillRoundRect(5, 5, getWidth() - 10, getHeight() - 10, 8, 8);
                g2.setColor(theme.TEXT_DIM);
              }
              // Draw dashed border
              float[] dash = {5.0f, 5.0f};
              g2.setStroke(
                  new BasicStroke(
                      1.5f, BasicStroke.CAP_BUTT, BasicStroke.JOIN_MITER, 10.0f, dash, 0.0f));
              g2.drawRoundRect(5, 5, getWidth() - 10, getHeight() - 10, 8, 8);
              g2.dispose();
            }
          };

      placeholder.setLayout(new GridBagLayout());
      placeholder.setPreferredSize(new Dimension(theme.scale(220), theme.scale(80)));
      placeholder.setMinimumSize(new Dimension(theme.scale(220), theme.scale(80)));
      placeholder.setMaximumSize(new Dimension(Short.MAX_VALUE, theme.scale(100)));

      JLabel placeLbl = new JLabel("Drag & drop effects here");
      placeLbl.setForeground(theme.TEXT_DIM);
      placeLbl.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(10.0f)));
      placeholder.add(placeLbl);

      paramListPanel.add(Box.createVerticalGlue());
      paramListPanel.add(placeholder);
      paramListPanel.add(Box.createVerticalGlue());
    } else {
      // List effects
      for (int i = 0; i < state.effects.size(); ++i) {
        final int idx = i;
        PadEffectUIState eff = state.effects.get(i);

        JPanel row = new JPanel(new BorderLayout(5, 0));
        row.setOpaque(true);
        row.setBackground(theme.PANEL_BG);
        row.setBorder(
            BorderFactory.createCompoundBorder(
                BorderFactory.createLineBorder(theme.BORDER, 1),
                BorderFactory.createEmptyBorder(2, 6, 2, 6)));
        row.setMaximumSize(new Dimension(Short.MAX_VALUE, theme.scale(26)));

        // Hover highlight on the row itself
        row.addMouseListener(
            new MouseAdapter() {
              public void mouseEntered(MouseEvent e) {
                row.setBackground(theme.PANEL_BG_LIGHT);
              }

              public void mouseExited(MouseEvent e) {
                row.setBackground(theme.PANEL_BG);
              }
            });

        JLabel nameLbl = new JLabel((idx + 1) + ". " + getPluginDisplayName(eff.effectPath));
        nameLbl.setForeground(theme.TEXT_LIGHT);
        nameLbl.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(9.0f)));
        row.add(nameLbl, BorderLayout.CENTER);

        JToggleButton editEffBtn = new JToggleButton("Edit");
        editEffBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
        editEffBtn.setFocusPainted(false);
        editEffBtn.setSelected(showingChildUi && childUiIsEffect && childUiEffectIndex == idx);

        editEffBtn.addActionListener(
            e -> {
              Class<? extends AbstractDevicePanel> panelClass = getChildPanelClass(eff.effectPath);
              if (panelClass == null && !eff.effectPath.isEmpty()) {
                editEffBtn.setSelected(false);
                sendDrumPadCmd(
                    DrumPadCmd.Action.ACTION_SHOW_GUI, selectedPad, "", 0, 0, "", idx, true);
              } else {
                showingChildUi = editEffBtn.isSelected();
                childUiIsEffect = showingChildUi;
                childUiEffectIndex = showingChildUi ? idx : -1;
                if (showingChildUi) {
                  editBtn.setSelected(false);
                  showChildUi();
                } else {
                  hideChildUi();
                }
                refreshDetailEditor(); // Redraw list to sync selected button states
              }
            });

        JButton removeBtn = new JButton("\u274C");
        removeBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
        removeBtn.setFocusPainted(false);
        removeBtn.addActionListener(
            ev -> {
              sendDrumPadCmd(
                  DrumPadCmd.Action.ACTION_REMOVE_PLUGIN, selectedPad, "", 0, 0, "", idx, true);
            });

        JPanel btnPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT, 4, 0));
        btnPanel.setOpaque(false);
        btnPanel.add(editEffBtn);
        btnPanel.add(removeBtn);
        row.add(btnPanel, BorderLayout.EAST);

        paramListPanel.add(row);
        paramListPanel.add(Box.createVerticalStrut(2));
      }
    }

    paramListPanel.revalidate();
    paramListPanel.repaint();
    paramScrollPane.revalidate();
    paramScrollPane.repaint();

    updatingUi = false;
  }

  private String getNoteName(int pitch) {
    int octave = (pitch / 12) - 1;
    String note = NOTE_NAMES[pitch % 12];
    return note + octave;
  }

  private Class<? extends AbstractDevicePanel> getChildPanelClass(String path) {
    if ("builtin://sampler".equals(path)) {
      return SamplerDevicePanel.class;
    }
    if ("builtin://3xosc".equals(path)) {
      return ThreeOscDevicePanel.class;
    }
    if ("builtin://dr8_kick".equals(path)) return Dr8KickDevicePanel.class;
    if ("builtin://dr8_snare".equals(path)) return Dr8SnareDevicePanel.class;
    if ("builtin://dr8_hat".equals(path)) return Dr8HatDevicePanel.class;
    if ("builtin://dr8_tom".equals(path)) return Dr8TomDevicePanel.class;
    if ("builtin://dr8_clap".equals(path)) return Dr8ClapDevicePanel.class;
    if ("builtin://dr8_cowbell".equals(path)) return Dr8CowbellDevicePanel.class;
    if ("builtin://dr8_crash".equals(path)) return Dr8CrashDevicePanel.class;
    if ("builtin://dr8_rim".equals(path)) return Dr8RimshotDevicePanel.class;
    if ("builtin://dr8_conga".equals(path)) return Dr8CongaDevicePanel.class;

    // Built-in effects
    if ("builtin://eq".equals(path)) return EqDevicePanel.class;
    if ("builtin://compressor".equals(path)) return CompressorDevicePanel.class;
    if ("builtin://delay".equals(path)) return DelayDevicePanel.class;
    if ("builtin://reverb".equals(path)) return ReverbDevicePanel.class;
    if ("builtin://limiter".equals(path)) return LimiterDevicePanel.class;
    if ("builtin://maxim".equals(path)) return MaximDevicePanel.class;
    if ("builtin://hott".equals(path)) return HottDevicePanel.class;
    if ("builtin://envelope_shaper".equals(path)) return EnvelopeShaperDevicePanel.class;
    if ("builtin://phaser".equals(path)) return PhaserDevicePanel.class;
    if ("builtin://convolver".equals(path)) return ConvolverDevicePanel.class;
    if ("builtin://vocodey".equals(path)) return VocodeyDevicePanel.class;

    return null;
  }

  private void showChildUi() {
    childUiContainer.removeAll();
    if (childUiPanel != null) {
      childUiPanel.setCustomParamSender(null);
      childUiPanel = null;
    }

    PadUIState state = pads[selectedPad];
    String path =
        childUiIsEffect ? state.effects.get(childUiEffectIndex).effectPath : state.pluginPath;
    Class<? extends AbstractDevicePanel> panelClass = getChildPanelClass(path);
    if (panelClass != null) {
      try {
        childUiPadIndex = selectedPad;
        childUiPanel =
            panelClass.getConstructor(int.class, int.class).newInstance(trackIndex, pluginIndex);

        List<ParamInfo> paramsList =
            childUiIsEffect ? state.effects.get(childUiEffectIndex).effectParams : state.params;
        for (ParamInfo info : paramsList) {
          childUiPanel.handleParamChange(info.getId(), info.getCurrentValue());
        }

        final int targetPadIdx = selectedPad;
        final boolean isEffect = childUiIsEffect;
        final int targetEffectIdx = childUiEffectIndex;
        childUiPanel.setCustomParamSender(
            (paramId, value) -> {
              sendDrumPadCmd(
                  DrumPadCmd.Action.ACTION_SET_PARAM,
                  targetPadIdx,
                  "",
                  paramId,
                  value,
                  "",
                  targetEffectIdx,
                  isEffect);
              List<ParamInfo> list =
                  isEffect ? state.effects.get(targetEffectIdx).effectParams : state.params;
              for (ParamInfo info : list) {
                if (info.getId() == paramId) {
                  int idx = list.indexOf(info);
                  list.set(idx, info.toBuilder().setCurrentValue((float) value).build());
                  break;
                }
              }
            });

        childUiContainer.add(childUiPanel, BorderLayout.CENTER);
        childUiContainer.setVisible(true);
        showingChildUi = true;
      } catch (Exception ex) {
        ex.printStackTrace();
        childUiContainer.setVisible(false);
        showingChildUi = false;
        childUiPadIndex = -1;
      }
    } else {
      childUiContainer.setVisible(false);
      showingChildUi = false;
      childUiPadIndex = -1;
    }
    updatePanelSize();
  }

  private void hideChildUi() {
    childUiContainer.removeAll();
    childUiContainer.setVisible(false);
    if (childUiPanel != null) {
      childUiPanel.setCustomParamSender(null);
      childUiPanel = null;
    }
    showingChildUi = false;
    childUiPadIndex = -1;
    updatePanelSize();
  }

  private void updatePanelSize() {
    Theme theme = Theme.getInstance();
    int baseWidth = theme.scale(640);
    int height = theme.scale(330);

    if (showingChildUi && childUiPanel != null) {
      int childWidth = childUiPanel.getPreferredSize().width;
      setPreferredSize(new Dimension(baseWidth + childWidth, height));
      setMaximumSize(new Dimension(baseWidth + childWidth, Short.MAX_VALUE));
    } else {
      setPreferredSize(new Dimension(baseWidth, height));
      setMaximumSize(new Dimension(baseWidth, Short.MAX_VALUE));
    }

    Container parent = getParent();
    if (parent != null) {
      parent.revalidate();
      parent.repaint();
    }
    revalidate();
    repaint();
  }

  @Override
  public void handleParamChange(int paramId, double value) {
    super.handleParamChange(paramId, value);
    if (globalSelected) {
      refreshDetailEditor();
    }
  }
}
