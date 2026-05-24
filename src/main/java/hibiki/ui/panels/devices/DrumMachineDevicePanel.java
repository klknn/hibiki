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

  static class PadUIState {
    String pluginPath = "";
    String effectPath = "";
    float volume = 1.0f;
    float pan = 0.0f;
    boolean mute = false;
    boolean solo = false;
    String sampleName = "";
    List<ParamInfo> params = new ArrayList<>();
    List<ParamInfo> effectParams = new ArrayList<>();
    int triggerNote = 60;
  }

  int currentBank = 0; // 0=A, 1=B, 2=C, 3=D
  int selectedPad = 0; // 0..63
  final PadUIState[] pads = new PadUIState[NUM_PADS];
  final JButton[] padButtons = new JButton[16];
  final boolean[] flashing = new boolean[16];

  final JLabel selectedPadLabel;
  final JComboBox<String> pluginCombo;
  final JComboBox<String> effectCombo;
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
  JToggleButton effectEditBtn;

  private AbstractDevicePanel childUiPanel = null;
  private JPanel childUiContainer = null;
  private boolean showingChildUi = false;
  private boolean childUiIsEffect = false;
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

    // Bank Selector Tabs (A, B, C, D)
    JPanel bankPanel = new JPanel(new GridLayout(1, 4, 2, 0));
    bankPanel.setOpaque(false);
    ButtonGroup bankGroup = new ButtonGroup();
    String[] bankNames = {"Bank A", "Bank B", "Bank C", "Bank D"};
    for (int b = 0; b < 4; ++b) {
      final int bankIdx = b;
      JToggleButton bankBtn = new JToggleButton(bankNames[b], b == 0);
      bankBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
      bankBtn.setFocusPainted(false);
      bankBtn.addActionListener(
          e -> {
            currentBank = bankIdx;
            refreshGrid();
          });
      bankGroup.add(bankBtn);
      bankPanel.add(bankBtn);
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
              } else if (indexInPads == selectedPad) {
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
    JPanel detailTop = new JPanel(new GridLayout(4, 1, 2, 2));
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
            sendDrumPadCmd(DrumPadCmd.Action.ACTION_SHOW_GUI, selectedPad, "", 0, 0, "", false);
          } else {
            showingChildUi = editBtn.isSelected();
            childUiIsEffect = false;
            if (showingChildUi) {
              effectEditBtn.setSelected(false);
              showChildUi();
            } else {
              hideChildUi();
            }
          }
        });
    pluginSelRow.add(editBtn, BorderLayout.EAST);
    detailTop.add(pluginSelRow);

    JPanel effectSelRow = new JPanel(new BorderLayout(5, 0));
    effectSelRow.setOpaque(false);
    JLabel effectLabel = new JLabel("Effect:");
    effectLabel.setForeground(new Color(0xCCCCCC));
    effectLabel.setFont(theme.FONT_UI);
    effectSelRow.add(effectLabel, BorderLayout.WEST);

    effectCombo =
        new JComboBox<>(
            new String[] {
              "Empty",
              "EQ Eight",
              "Compressor",
              "Delay",
              "Reverb",
              "Limiter",
              "Maxim",
              "Hott",
              "EnvShaper",
              "Phaser",
              "Convolver",
              "Bitcrusher",
              "Chorus",
              "Stereo Width",
              "Vocodey",
              "Load VST3..."
            });
    effectCombo.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    effectCombo.addActionListener(
        e -> {
          if (updatingUi) return;
          String sel = (String) effectCombo.getSelectedItem();
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
                  DrumPadCmd.Action.ACTION_LOAD_PLUGIN, selectedPad, vst3Path, 0, 0, "", true);
            } else {
              refreshDetailEditor(); // Reset combo selection to actual value
            }
            return;
          }

          String path = "";
          if ("EQ Eight".equals(sel)) path = "builtin://eq";
          else if ("Compressor".equals(sel)) path = "builtin://compressor";
          else if ("Delay".equals(sel)) path = "builtin://delay";
          else if ("Reverb".equals(sel)) path = "builtin://reverb";
          else if ("Limiter".equals(sel)) path = "builtin://limiter";
          else if ("Maxim".equals(sel)) path = "builtin://maxim";
          else if ("Hott".equals(sel)) path = "builtin://hott";
          else if ("EnvShaper".equals(sel)) path = "builtin://envelope_shaper";
          else if ("Phaser".equals(sel)) path = "builtin://phaser";
          else if ("Convolver".equals(sel)) path = "builtin://convolver";
          else if ("Bitcrusher".equals(sel)) path = "builtin://bitcrusher";
          else if ("Chorus".equals(sel)) path = "builtin://chorus";
          else if ("Stereo Width".equals(sel)) path = "builtin://stereo_width";
          else if ("Vocodey".equals(sel)) path = "builtin://vocodey";
          else if (!"Empty".equals(sel)) {
            path = pads[selectedPad].effectPath;
          }

          if (path.isEmpty()) {
            sendDrumPadCmd(DrumPadCmd.Action.ACTION_REMOVE_PLUGIN, selectedPad, "", 0, 0, "", true);
          } else {
            sendDrumPadCmd(DrumPadCmd.Action.ACTION_LOAD_PLUGIN, selectedPad, path, 0, 0, "", true);
          }
        });
    effectSelRow.add(effectCombo, BorderLayout.CENTER);

    if (!java.awt.GraphicsEnvironment.isHeadless()) {
      new DropTarget(
          effectCombo,
          new DropTargetAdapter() {
            @Override
            public void drop(DropTargetDropEvent dtde) {
              try {
                if (dtde.isDataFlavorSupported(DataFlavor.stringFlavor)) {
                  dtde.acceptDrop(DnDConstants.ACTION_COPY);
                  String data =
                      (String) dtde.getTransferable().getTransferData(DataFlavor.stringFlavor);
                  dtde.dropComplete(true);
                  String[] parts = data.split(":", 2);
                  if (parts.length == 2) {
                    if ("builtin".equals(parts[0])
                        || "vst".equals(parts[0])
                        || "remote-vst".equals(parts[0])
                        || "plugin".equals(parts[0])) {
                      sendDrumPadCmd(
                          DrumPadCmd.Action.ACTION_LOAD_PLUGIN,
                          selectedPad,
                          parts[1],
                          0,
                          0,
                          "",
                          true);
                    }
                  }
                }
              } catch (Exception ex) {
                dtde.rejectDrop();
              }
            }
          });
    }

    effectEditBtn = new JToggleButton("Edit");
    effectEditBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    effectEditBtn.setFocusPainted(false);
    effectEditBtn.addActionListener(
        e -> {
          PadUIState state = pads[selectedPad];
          Class<? extends AbstractDevicePanel> panelClass = getChildPanelClass(state.effectPath);
          if (panelClass == null && !state.effectPath.isEmpty()) {
            effectEditBtn.setSelected(false);
            sendDrumPadCmd(DrumPadCmd.Action.ACTION_SHOW_GUI, selectedPad, "", 0, 0, "", true);
          } else {
            showingChildUi = effectEditBtn.isSelected();
            childUiIsEffect = showingChildUi;
            if (showingChildUi) {
              editBtn.setSelected(false);
              showChildUi();
            } else {
              hideChildUi();
            }
          }
        });
    effectSelRow.add(effectEditBtn, BorderLayout.EAST);
    detailTop.add(effectSelRow);

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
          sendDrumPadCmd(
              DrumPadCmd.Action.ACTION_SET_VOLUME,
              selectedPad,
              "",
              0,
              padVolSlider.getValue() / 100.0,
              "");
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
          sendDrumPadCmd(
              DrumPadCmd.Action.ACTION_SET_PAN,
              selectedPad,
              "",
              0,
              padPanSlider.getValue() / 50.0,
              "");
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
          sendDrumPadCmd(
              DrumPadCmd.Action.ACTION_SET_MUTE,
              selectedPad,
              "",
              0,
              padMuteBtn.isSelected() ? 1.0 : 0.0,
              "");
        });
    mixPanel.add(padMuteBtn);

    padSoloBtn = new JToggleButton("Solo");
    padSoloBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    padSoloBtn.setFocusPainted(false);
    padSoloBtn.addActionListener(
        e -> {
          if (updatingUi) return;
          sendDrumPadCmd(
              DrumPadCmd.Action.ACTION_SET_SOLO,
              selectedPad,
              "",
              0,
              padSoloBtn.isSelected() ? 1.0 : 0.0,
              "");
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
            BorderFactory.createLineBorder(theme.BORDER), "Parameters"));
    ((javax.swing.border.TitledBorder) paramScrollPane.getBorder()).setTitleColor(Color.LIGHT_GRAY);
    ((javax.swing.border.TitledBorder) paramScrollPane.getBorder())
        .setTitleFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    paramScrollPane.setBackground(theme.BG_DARK);
    detailCenter.add(paramScrollPane);

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

  void loadAudioSample(int padIdx, String path) {
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

  private void sendDrumPadCmd(
      DrumPadCmd.Action action,
      int padIdx,
      String pluginPath,
      int paramId,
      double paramValue,
      String samplePath) {
    sendDrumPadCmd(action, padIdx, pluginPath, paramId, paramValue, samplePath, -1, false);
  }

  private void sendDrumPadCmd(
      DrumPadCmd.Action action,
      int padIdx,
      String pluginPath,
      int paramId,
      double paramValue,
      String samplePath,
      boolean targetEffect) {
    sendDrumPadCmd(action, padIdx, pluginPath, paramId, paramValue, samplePath, -1, targetEffect);
  }

  private void sendDrumPadCmd(
      DrumPadCmd.Action action,
      int padIdx,
      String pluginPath,
      int paramId,
      double paramValue,
      String samplePath,
      int triggerNote) {
    sendDrumPadCmd(action, padIdx, pluginPath, paramId, paramValue, samplePath, triggerNote, false);
  }

  private void sendDrumPadCmd(
      DrumPadCmd.Action action,
      int padIdx,
      String pluginPath,
      int paramId,
      double paramValue,
      String samplePath,
      int triggerNote,
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
            .setTargetEffect(targetEffect);
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
          List<ParamInfo> activeParamsList = childUiIsEffect ? state.effectParams : state.params;
          for (ParamInfo info : activeParamsList) {
            childUiPanel.handleParamChange(info.getId(), info.getCurrentValue());
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
    String dispName = "Empty";
    if ("builtin://sampler".equals(pPath)) {
      dispName = "Sampler";
    } else if ("builtin://3xosc".equals(pPath)) {
      dispName = "3xOsc";
    } else if ("builtin://acid_bass".equals(pPath)) {
      dispName = "Acid Bass";
    } else if ("builtin://dr8_kick".equals(pPath)) {
      dispName = "DR8 Kick";
    } else if ("builtin://dr8_snare".equals(pPath)) {
      dispName = "DR8 Snare";
    } else if ("builtin://dr8_hat".equals(pPath)) {
      dispName = "DR8 Hat";
    } else if ("builtin://dr8_tom".equals(pPath)) {
      dispName = "DR8 Tom";
    } else if ("builtin://dr8_clap".equals(pPath)) {
      dispName = "DR8 Clap";
    } else if ("builtin://dr8_cowbell".equals(pPath)) {
      dispName = "DR8 Cowbell";
    } else if ("builtin://dr8_crash".equals(pPath)) {
      dispName = "DR8 Crash";
    } else if ("builtin://dr8_rim".equals(pPath)) {
      dispName = "DR8 Rimshot";
    } else if ("builtin://dr8_conga".equals(pPath)) {
      dispName = "DR8 Conga";
    } else if ("builtin://organ".equals(pPath)) {
      dispName = "Organ";
    } else if (pPath.startsWith("builtin://film")) {
      dispName = "Film";
    } else if (!pPath.isEmpty()) {
      int slash = pPath.lastIndexOf('/');
      if (slash == -1) slash = pPath.lastIndexOf('\\');
      dispName = (slash != -1) ? pPath.substring(slash + 1) : pPath;
      if (dispName.endsWith(".vst3")) {
        dispName = dispName.substring(0, dispName.length() - 5);
      }
    }

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

    // Effect dropdown
    if (state.effectPath.isEmpty()) {
      effectCombo.setSelectedItem("Empty");
    } else if ("builtin://eq".equals(state.effectPath)) {
      effectCombo.setSelectedItem("EQ Eight");
    } else if ("builtin://compressor".equals(state.effectPath)) {
      effectCombo.setSelectedItem("Compressor");
    } else if ("builtin://delay".equals(state.effectPath)) {
      effectCombo.setSelectedItem("Delay");
    } else if ("builtin://reverb".equals(state.effectPath)) {
      effectCombo.setSelectedItem("Reverb");
    } else if ("builtin://limiter".equals(state.effectPath)) {
      effectCombo.setSelectedItem("Limiter");
    } else if ("builtin://maxim".equals(state.effectPath)) {
      effectCombo.setSelectedItem("Maxim");
    } else if ("builtin://hott".equals(state.effectPath)) {
      effectCombo.setSelectedItem("Hott");
    } else if ("builtin://envelope_shaper".equals(state.effectPath)) {
      effectCombo.setSelectedItem("EnvShaper");
    } else if ("builtin://phaser".equals(state.effectPath)) {
      effectCombo.setSelectedItem("Phaser");
    } else if ("builtin://convolver".equals(state.effectPath)) {
      effectCombo.setSelectedItem("Convolver");
    } else if ("builtin://bitcrusher".equals(state.effectPath)) {
      effectCombo.setSelectedItem("Bitcrusher");
    } else if ("builtin://chorus".equals(state.effectPath)) {
      effectCombo.setSelectedItem("Chorus");
    } else if ("builtin://stereo_width".equals(state.effectPath)) {
      effectCombo.setSelectedItem("Stereo Width");
    } else if ("builtin://vocodey".equals(state.effectPath)) {
      effectCombo.setSelectedItem("Vocodey");
    } else {
      DefaultComboBoxModel<String> model = (DefaultComboBoxModel<String>) effectCombo.getModel();
      String dispName = state.effectPath;
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
      effectCombo.setSelectedItem(dispName);
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
    boolean hasEffect = !state.effectPath.isEmpty();

    String childPath = childUiIsEffect ? state.effectPath : state.pluginPath;
    Class<? extends AbstractDevicePanel> clz = getChildPanelClass(childPath);
    if (clz == null) {
      showingChildUi = false;
    }
    editBtn.setEnabled(hasPlugin);
    effectEditBtn.setEnabled(hasEffect);

    editBtn.setSelected(showingChildUi && !childUiIsEffect && hasPlugin);
    effectEditBtn.setSelected(showingChildUi && childUiIsEffect && hasEffect);

    boolean shouldShowChild = showingChildUi && (childUiIsEffect ? hasEffect : hasPlugin);
    if (shouldShowChild) {
      if (childUiPanel == null || childUiPadIndex != selectedPad || !clz.isInstance(childUiPanel)) {
        showChildUi();
      } else {
        List<ParamInfo> paramsList = childUiIsEffect ? state.effectParams : state.params;
        for (ParamInfo info : paramsList) {
          childUiPanel.handleParamChange(info.getId(), info.getCurrentValue());
        }
      }
    } else {
      hideChildUi();
    }

    // Rebuild parameters list
    paramListPanel.removeAll();
    Theme theme = Theme.getInstance();

    List<ParamInfo> activeParamsList = childUiIsEffect ? state.effectParams : state.params;
    final boolean activeIsEffect = childUiIsEffect;
    for (ParamInfo info : activeParamsList) {
      JPanel row = new JPanel(new BorderLayout(5, 0));
      row.setOpaque(false);
      row.setMaximumSize(new Dimension(Short.MAX_VALUE, theme.scale(20)));

      JLabel lbl = new JLabel(info.getName());
      lbl.setForeground(Color.WHITE);
      lbl.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
      lbl.setPreferredSize(new Dimension(theme.scale(70), 0));
      row.add(lbl, BorderLayout.WEST);

      JSlider slider = new JSlider(0, 1000, (int) (info.getCurrentValue() * 1000));
      slider.setOpaque(false);
      slider.addChangeListener(
          e -> {
            if (slider.getValueIsAdjusting()) return;
            sendDrumPadCmd(
                DrumPadCmd.Action.ACTION_SET_PARAM,
                selectedPad,
                "",
                info.getId(),
                slider.getValue() / 1000.0,
                "",
                activeIsEffect);
          });
      row.add(slider, BorderLayout.CENTER);

      paramListPanel.add(row);
      paramListPanel.add(Box.createVerticalStrut(2));
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
    String path = childUiIsEffect ? state.effectPath : state.pluginPath;
    Class<? extends AbstractDevicePanel> panelClass = getChildPanelClass(path);
    if (panelClass != null) {
      try {
        childUiPadIndex = selectedPad;
        childUiPanel =
            panelClass.getConstructor(int.class, int.class).newInstance(trackIndex, pluginIndex);

        List<ParamInfo> paramsList = childUiIsEffect ? state.effectParams : state.params;
        for (ParamInfo info : paramsList) {
          childUiPanel.handleParamChange(info.getId(), info.getCurrentValue());
        }

        final int targetPadIdx = selectedPad;
        final boolean isEffect = childUiIsEffect;
        childUiPanel.setCustomParamSender(
            (paramId, value) -> {
              sendDrumPadCmd(
                  DrumPadCmd.Action.ACTION_SET_PARAM,
                  targetPadIdx,
                  "",
                  paramId,
                  value,
                  "",
                  isEffect);
              List<ParamInfo> list = isEffect ? state.effectParams : state.params;
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
}
