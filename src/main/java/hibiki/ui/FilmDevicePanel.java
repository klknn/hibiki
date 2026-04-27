package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import java.awt.*;
import java.awt.event.*;
import javax.swing.*;

/**
 * FilM 6-operator FM Synthesizer device panel. Sytrus-inspired layout: left side has tabbed content
 * (Main/Op1-6/Filter1-3/FX), right side has always-visible 6×12 modulation matrix.
 */
public class FilmDevicePanel extends JPanel {
  static final int NUM_OPS = 6;
  static final int NUM_FILTERS = 3;
  static final int PARAMS_PER_OP = 23;
  static final int PARAMS_PER_FILTER = 12;
  private static final int OP_PARAMS = NUM_OPS * PARAMS_PER_OP; // 138
  private static final int FILTER_PARAMS = NUM_FILTERS * PARAMS_PER_FILTER; // 36
  static final int NUM_GLOBAL = 8;
  private static final int MATRIX_COLS = 12;
  private static final int MATRIX_PARAMS = NUM_OPS * MATRIX_COLS; // 72
  private static final int TOTAL_PARAMS = OP_PARAMS + FILTER_PARAMS + NUM_GLOBAL + MATRIX_PARAMS;
  private static final int MATRIX_BASE = OP_PARAMS + FILTER_PARAMS + NUM_GLOBAL;

  // Waveform names and normalized values
  private static final String[] WAVE_NAMES = {"Sin", "Saw", "Sq", "Tri", "Nse"};
  private static final double[] WAVE_NORMS = {0.0, 0.3, 0.5, 0.7, 0.9};
  private static final String[] FILTER_NAMES = {"LP", "HP", "BP", "LS", "HS", "Bell"};
  private static final double[] FILTER_NORMS = {0.0, 0.2, 0.4, 0.6, 0.8, 1.0};
  private static final String[] LFO_WAVE_NAMES = {"Sin", "Tri", "Sq"};
  private static final double[] LFO_WAVE_NORMS = {0.0, 0.5, 1.0};
  private static final String[] MATRIX_COL_LABELS = {
    "1", "2", "3", "4", "5", "6", "F1", "F2", "F3", "P", "FX", "O"
  };

  // Global param offsets
  private static final int G_ALGORITHM = OP_PARAMS + FILTER_PARAMS;
  private static final int G_MASTER_VOL = G_ALGORITHM + 1;
  private static final int G_ENABLE = G_ALGORITHM + 2;
  private static final int G_UNISON_VOICES = G_ALGORITHM + 3;
  private static final int G_UNISON_DETUNE = G_ALGORITHM + 4;
  private static final int G_UNISON_SPREAD = G_ALGORITHM + 5;
  private static final int G_PORTAMENTO = G_ALGORITHM + 6;
  private static final int G_RM_MODE = G_ALGORITHM + 7;

  // Per-op param offsets (relative)
  static final int OP_WAVEFORM = 0, OP_LEVEL = 1, OP_RATIO = 2, OP_FINE = 3;
  private static final int OP_ENV_A = 4, OP_ENV_D = 5, OP_ENV_S = 6, OP_ENV_R = 7;
  private static final int OP_FEEDBACK = 8, OP_PAN = 9;
  private static final int OP_LFO_RATE = 10, OP_LFO_DEPTH = 11, OP_LFO_WAVE = 12, OP_PHASE = 13;
  private static final int OP_SHAPE = 14, OP_TENSION = 15, OP_SKEW = 16;
  private static final int OP_SINE_SHAPER = 17, OP_NOISE_MIX = 18, OP_FREQ_OFFSET = 19;
  private static final int OP_HALF = 20, OP_EVEN = 21, OP_ABSOLUTE = 22;

  // Per-filter param offsets (relative)
  private static final int FLT_TYPE = 0, FLT_CUTOFF = 1, FLT_RESONANCE = 2;
  private static final int FLT_ENV_A = 3, FLT_ENV_D = 4, FLT_ENV_S = 5, FLT_ENV_R = 6;
  private static final int FLT_ENV_DEPTH = 7, FLT_MIX = 8;
  private static final int FLT_LFO_RATE = 9, FLT_LFO_DEPTH = 10, FLT_LFO_WAVE = 11;

  private final int trackIndex;
  private final int pluginIndex;
  private final double[] params = new double[TOTAL_PARAMS];
  private boolean enabled = true;
  private final KnobPanel[] matrixKnobs = new KnobPanel[MATRIX_PARAMS];
  private final java.util.Map<Integer, KnobPanel> allKnobs = new java.util.HashMap<>();

  // Envelope editors: one per operator + one per filter
  private final EnvelopeEditorPanel[] opEnvelopes = new EnvelopeEditorPanel[NUM_OPS];
  private final EnvelopeEditorPanel[] filterEnvelopes = new EnvelopeEditorPanel[NUM_FILTERS];
  private EnvelopeEditorPanel mainEnvelope; // MAIN tab shows OP1 envelope

  public Runnable modToggleCallback;

  // Colors — purple/yellow Sytrus theme
  private static final Color ACCENT_YELLOW = new Color(0xD4B84A);
  private static final Color ACCENT_PURPLE = new Color(0x7B5EA7);
  private static final Color HEADER_PURPLE = new Color(0x4A3570);
  private static final Color[] OP_COLORS = {
    new Color(0xD4B84A), new Color(0xC9A83E), new Color(0xBE9832),
    new Color(0x9B7EC8), new Color(0x8B6EB8), new Color(0x7B5EA7)
  };
  private static final Color FILTER_COLOR = new Color(0x9B7EC8);
  private static final Color MATRIX_BG = new Color(0x1E1E24);
  private static final Color MATRIX_GRID = new Color(0x333340);
  private static final Color MATRIX_INACTIVE = new Color(0x444450);

  public FilmDevicePanel(int trackIndex, int pluginIndex) {
    this.trackIndex = trackIndex;
    this.pluginIndex = pluginIndex;
    initDefaults();

    setLayout(new BorderLayout());
    Theme theme = Theme.getInstance();
    setPreferredSize(new Dimension(theme.scale(780), theme.scale(400)));
    setMaximumSize(new Dimension(theme.scale(780), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Header
    add(createHeader(theme), BorderLayout.NORTH);

    // Main split: left = tabs, right = mod matrix
    JSplitPane split = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT);
    split.setDividerLocation(theme.scale(420));
    split.setDividerSize(theme.scale(3));
    split.setBorder(null);

    split.setLeftComponent(createTabPanel(theme));
    split.setRightComponent(createMatrixPanel(theme));

    add(split, BorderLayout.CENTER);
  }

  // ── Header ──────────────────────────────────────────────────

  private JPanel createHeader(Theme theme) {
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(HEADER_PURPLE);
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));

    JLabel nameLabel = new JLabel("FilM");
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

    JToggleButton enableBtn = new JToggleButton("On", enabled);
    enableBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    enableBtn.setFocusPainted(false);
    enableBtn.addActionListener(
        e -> {
          enabled = enableBtn.isSelected();
          sendParam(G_ENABLE, enabled ? 1.0 : 0.0);
        });
    btnPanel.add(enableBtn);
    header.add(btnPanel, BorderLayout.EAST);

    return header;
  }

  // ── Tab Panel (left side) ───────────────────────────────────

  private JTabbedPane createTabPanel(Theme theme) {
    JTabbedPane tabs = new JTabbedPane(JTabbedPane.TOP);
    tabs.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    tabs.setBackground(theme.BG_MEDIUM);

    tabs.addTab("MAIN", createMainTab(theme));
    for (int i = 0; i < NUM_OPS; i++) {
      tabs.addTab("OP" + (i + 1), createOpTab(i, theme));
    }
    for (int i = 0; i < NUM_FILTERS; i++) {
      tabs.addTab("F" + (i + 1), createFilterTab(i, theme));
    }
    tabs.addTab("FX", createFxTab(theme));

    return tabs;
  }

  // ── MAIN Tab ────────────────────────────────────────────────

  private JPanel createMainTab(Theme theme) {
    JPanel main = new JPanel();
    main.setLayout(new BoxLayout(main, BoxLayout.Y_AXIS));
    main.setBackground(theme.BG_MEDIUM);
    main.setBorder(BorderFactory.createEmptyBorder(6, 6, 6, 6));

    // Row 1: Master Vol + Algorithm
    JPanel row1 = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(8), 0));
    row1.setOpaque(false);
    row1.add(createKnob("Volume", G_MASTER_VOL, 0.8, theme));
    row1.add(createKnob("Algo", G_ALGORITHM, 0.0, theme));
    row1.add(createKnob("Porta", G_PORTAMENTO, 0.0, theme));
    main.add(row1);

    main.add(Box.createVerticalStrut(theme.scale(6)));

    // Row 2: Unison
    JPanel uniPanel = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(8), 0));
    uniPanel.setOpaque(false);
    uniPanel.setBorder(
        BorderFactory.createTitledBorder(
            BorderFactory.createLineBorder(new Color(0x444444)),
            "Unison",
            0,
            0,
            theme.FONT_UI.deriveFont(theme.scale(9.0f)),
            new Color(0x999999)));
    uniPanel.add(createKnob("Voices", G_UNISON_VOICES, 0.0, theme));
    uniPanel.add(createKnob("Detune", G_UNISON_DETUNE, 0.0, theme));
    uniPanel.add(createKnob("Spread", G_UNISON_SPREAD, 0.5, theme));
    main.add(uniPanel);

    main.add(Box.createVerticalStrut(theme.scale(6)));

    // Row 3: Gain Envelope (OP1) — visual editor
    JPanel envPanel = new JPanel(new BorderLayout());
    envPanel.setOpaque(false);
    envPanel.setBorder(
        BorderFactory.createTitledBorder(
            BorderFactory.createLineBorder(new Color(0x444444)),
            "Gain Envelope (OP1)",
            0,
            0,
            theme.FONT_UI.deriveFont(theme.scale(9.0f)),
            new Color(0x999999)));
    mainEnvelope = new EnvelopeEditorPanel();
    mainEnvelope.setValues(0.0f, 0.2f, 0.7f, 0.3f);
    mainEnvelope.setPreferredSize(new Dimension(theme.scale(350), theme.scale(130)));
    mainEnvelope.addListener(
        (a, d, s, r) -> {
          params[OP_ENV_A] = a;
          params[OP_ENV_D] = d;
          params[OP_ENV_S] = s;
          params[OP_ENV_R] = r;
          sendParam(OP_ENV_A, a);
          sendParam(OP_ENV_D, d);
          sendParam(OP_ENV_S, s);
          sendParam(OP_ENV_R, r);
          // Sync OP1 tab envelope
          if (opEnvelopes[0] != null) {
            opEnvelopes[0].setValues(a, d, s, r);
          }
        });
    envPanel.add(mainEnvelope, BorderLayout.CENTER);
    main.add(envPanel);

    return main;
  }

  // ── OP Tab ──────────────────────────────────────────────────

  private JPanel createOpTab(int op, Theme theme) {
    int base = op * PARAMS_PER_OP;
    Color color = OP_COLORS[op];

    JPanel panel = new JPanel();
    panel.setLayout(new BoxLayout(panel, BoxLayout.Y_AXIS));
    panel.setBackground(theme.BG_MEDIUM);
    panel.setBorder(BorderFactory.createEmptyBorder(4, 6, 4, 6));

    // Waveform selector
    JPanel waveRow = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(3), 0));
    waveRow.setOpaque(false);
    JLabel opLabel = new JLabel("OP " + (op + 1));
    opLabel.setForeground(color);
    opLabel.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(10.0f)));
    waveRow.add(opLabel);
    waveRow.add(Box.createHorizontalStrut(theme.scale(8)));

    ButtonGroup bg = new ButtonGroup();
    for (int w = 0; w < WAVE_NAMES.length; w++) {
      JToggleButton btn = new JToggleButton(WAVE_NAMES[w]);
      btn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
      btn.setFocusPainted(false);
      btn.setMargin(new Insets(0, 2, 0, 2));
      btn.setPreferredSize(new Dimension(theme.scale(32), theme.scale(20)));
      if (w == 0) btn.setSelected(true);
      final int wIdx = w;
      btn.addActionListener(
          e -> {
            params[base + OP_WAVEFORM] = WAVE_NORMS[wIdx];
            sendParam(base + OP_WAVEFORM, WAVE_NORMS[wIdx]);
          });
      bg.add(btn);
      waveRow.add(btn);
    }
    panel.add(waveRow);
    panel.add(Box.createVerticalStrut(theme.scale(4)));

    // Sub-tabs for VOL/PITCH/PHASE/LFO
    JTabbedPane subTabs = new JTabbedPane(JTabbedPane.TOP);
    subTabs.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    subTabs.setBackground(theme.BG_DARK);

    // VOL sub-tab — knobs + envelope editor
    JPanel volTab = new JPanel();
    volTab.setLayout(new BoxLayout(volTab, BoxLayout.Y_AXIS));
    volTab.setBackground(theme.BG_DARK);
    JPanel volKnobs = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(6), theme.scale(2)));
    volKnobs.setOpaque(false);
    volKnobs.add(createKnob("Level", base + OP_LEVEL, op == 0 ? 1.0 : 0.0, theme));
    volKnobs.add(createKnob("Pan", base + OP_PAN, 0.5, theme));
    volKnobs.add(createKnob("FB", base + OP_FEEDBACK, 0.0, theme));
    volTab.add(volKnobs);
    // Envelope editor
    EnvelopeEditorPanel envEditor = new EnvelopeEditorPanel();
    envEditor.setValues(0.0f, 0.2f, 0.7f, 0.3f);
    envEditor.setPreferredSize(new Dimension(theme.scale(350), theme.scale(120)));
    opEnvelopes[op] = envEditor;
    final int opIdx = op;
    envEditor.addListener(
        (a, d, s, r) -> {
          params[base + OP_ENV_A] = a;
          params[base + OP_ENV_D] = d;
          params[base + OP_ENV_S] = s;
          params[base + OP_ENV_R] = r;
          sendParam(base + OP_ENV_A, a);
          sendParam(base + OP_ENV_D, d);
          sendParam(base + OP_ENV_S, s);
          sendParam(base + OP_ENV_R, r);
          // Sync MAIN tab envelope if this is OP1
          if (opIdx == 0 && mainEnvelope != null) {
            mainEnvelope.setValues(a, d, s, r);
          }
        });
    volTab.add(envEditor);
    subTabs.addTab("VOL", volTab);

    // PITCH sub-tab
    JPanel pitchTab = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(6), theme.scale(2)));
    pitchTab.setBackground(theme.BG_DARK);
    pitchTab.add(createKnob("Ratio", base + OP_RATIO, 0.5, theme));
    pitchTab.add(createKnob("Fine", base + OP_FINE, 0.5, theme));
    pitchTab.add(createKnob("FqOfs", base + OP_FREQ_OFFSET, 0.5, theme));
    subTabs.addTab("PITCH", pitchTab);

    // PHASE sub-tab
    JPanel phaseTab = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(6), theme.scale(2)));
    phaseTab.setBackground(theme.BG_DARK);
    phaseTab.add(createKnob("Phase", base + OP_PHASE, 0.0, theme));
    subTabs.addTab("PHASE", phaseTab);

    // LFO sub-tab
    JPanel lfoTab = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(6), theme.scale(2)));
    lfoTab.setBackground(theme.BG_DARK);
    lfoTab.add(createKnob("Rate", base + OP_LFO_RATE, 0.3, theme));
    lfoTab.add(createKnob("Depth", base + OP_LFO_DEPTH, 0.0, theme));
    // LFO waveform selector
    ButtonGroup lbg = new ButtonGroup();
    for (int w = 0; w < LFO_WAVE_NAMES.length; w++) {
      JToggleButton btn = new JToggleButton(LFO_WAVE_NAMES[w]);
      btn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
      btn.setFocusPainted(false);
      btn.setMargin(new Insets(0, 2, 0, 2));
      btn.setPreferredSize(new Dimension(theme.scale(30), theme.scale(18)));
      if (w == 0) btn.setSelected(true);
      final int wIdx = w;
      btn.addActionListener(
          e -> {
            params[base + OP_LFO_WAVE] = LFO_WAVE_NORMS[wIdx];
            sendParam(base + OP_LFO_WAVE, LFO_WAVE_NORMS[wIdx]);
          });
      lbg.add(btn);
      lfoTab.add(btn);
    }
    subTabs.addTab("LFO", lfoTab);

    // OSC sub-tab (shape modifiers)
    JPanel oscTab = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(4), theme.scale(2)));
    oscTab.setBackground(theme.BG_DARK);
    oscTab.add(createKnob("SH", base + OP_SHAPE, 0.5, theme));
    oscTab.add(createKnob("TN", base + OP_TENSION, 0.5, theme));
    oscTab.add(createKnob("SK", base + OP_SKEW, 0.5, theme));
    oscTab.add(createKnob("SN", base + OP_SINE_SHAPER, 0.0, theme));
    oscTab.add(createKnob("NS", base + OP_NOISE_MIX, 0.0, theme));
    // Toggle switches for Half/Even/Absolute
    String[] modeLabels = {"Half", "Even", "Abs"};
    int[] modeParams = {OP_HALF, OP_EVEN, OP_ABSOLUTE};
    for (int m = 0; m < 3; m++) {
      JToggleButton modeBtn = new JToggleButton(modeLabels[m]);
      modeBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(7.5f)));
      modeBtn.setFocusPainted(false);
      modeBtn.setMargin(new Insets(0, 2, 0, 2));
      modeBtn.setPreferredSize(new Dimension(theme.scale(34), theme.scale(18)));
      final int mIdx = modeParams[m];
      modeBtn.addActionListener(
          e -> {
            double v = modeBtn.isSelected() ? 1.0 : 0.0;
            params[base + mIdx] = v;
            sendParam(base + mIdx, v);
          });
      oscTab.add(modeBtn);
    }
    subTabs.addTab("OSC", oscTab);

    panel.add(subTabs);
    return panel;
  }

  // ── Filter Tab ──────────────────────────────────────────────

  private JPanel createFilterTab(int flt, Theme theme) {
    int base = OP_PARAMS + flt * PARAMS_PER_FILTER;

    JPanel panel = new JPanel();
    panel.setLayout(new BoxLayout(panel, BoxLayout.Y_AXIS));
    panel.setBackground(theme.BG_MEDIUM);
    panel.setBorder(BorderFactory.createEmptyBorder(4, 6, 4, 6));

    // Filter type selector
    JPanel typeRow = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(3), 0));
    typeRow.setOpaque(false);
    JLabel fltLabel = new JLabel("FILTER " + (flt + 1));
    fltLabel.setForeground(FILTER_COLOR);
    fltLabel.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(10.0f)));
    typeRow.add(fltLabel);
    typeRow.add(Box.createHorizontalStrut(theme.scale(8)));

    ButtonGroup bg = new ButtonGroup();
    for (int f = 0; f < FILTER_NAMES.length; f++) {
      JToggleButton btn = new JToggleButton(FILTER_NAMES[f]);
      btn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
      btn.setFocusPainted(false);
      btn.setMargin(new Insets(0, 2, 0, 2));
      btn.setPreferredSize(new Dimension(theme.scale(30), theme.scale(20)));
      if (f == 0) btn.setSelected(true);
      final int fIdx = f;
      btn.addActionListener(
          e -> {
            params[base + FLT_TYPE] = FILTER_NORMS[fIdx];
            sendParam(base + FLT_TYPE, FILTER_NORMS[fIdx]);
          });
      bg.add(btn);
      typeRow.add(btn);
    }
    panel.add(typeRow);
    panel.add(Box.createVerticalStrut(theme.scale(4)));

    // Sub-tabs for controls + LFO
    JTabbedPane subTabs = new JTabbedPane(JTabbedPane.TOP);
    subTabs.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    subTabs.setBackground(theme.BG_DARK);

    // Controls sub-tab
    JPanel ctrlTab = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(6), theme.scale(2)));
    ctrlTab.setBackground(theme.BG_DARK);
    ctrlTab.add(createKnob("Cut", base + FLT_CUTOFF, 1.0, theme));
    ctrlTab.add(createKnob("Res", base + FLT_RESONANCE, 0.0, theme));
    ctrlTab.add(createKnob("Depth", base + FLT_ENV_DEPTH, 0.5, theme));
    ctrlTab.add(createKnob("Mix", base + FLT_MIX, 1.0, theme));
    subTabs.addTab("CTRL", ctrlTab);

    // ENV sub-tab — visual envelope editor
    JPanel envTab = new JPanel(new BorderLayout());
    envTab.setBackground(theme.BG_DARK);
    EnvelopeEditorPanel fltEnvEditor = new EnvelopeEditorPanel();
    fltEnvEditor.setValues(0.0f, 0.2f, 0.7f, 0.3f);
    fltEnvEditor.setPreferredSize(new Dimension(theme.scale(350), theme.scale(120)));
    filterEnvelopes[flt] = fltEnvEditor;
    fltEnvEditor.addListener(
        (a, d, s, r) -> {
          params[base + FLT_ENV_A] = a;
          params[base + FLT_ENV_D] = d;
          params[base + FLT_ENV_S] = s;
          params[base + FLT_ENV_R] = r;
          sendParam(base + FLT_ENV_A, a);
          sendParam(base + FLT_ENV_D, d);
          sendParam(base + FLT_ENV_S, s);
          sendParam(base + FLT_ENV_R, r);
        });
    envTab.add(fltEnvEditor, BorderLayout.CENTER);
    subTabs.addTab("ENV", envTab);

    // LFO sub-tab
    JPanel lfoTab = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(6), theme.scale(2)));
    lfoTab.setBackground(theme.BG_DARK);
    lfoTab.add(createKnob("Rate", base + FLT_LFO_RATE, 0.3, theme));
    lfoTab.add(createKnob("Depth", base + FLT_LFO_DEPTH, 0.0, theme));
    ButtonGroup lbg = new ButtonGroup();
    for (int w = 0; w < LFO_WAVE_NAMES.length; w++) {
      JToggleButton btn = new JToggleButton(LFO_WAVE_NAMES[w]);
      btn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
      btn.setFocusPainted(false);
      btn.setMargin(new Insets(0, 2, 0, 2));
      btn.setPreferredSize(new Dimension(theme.scale(30), theme.scale(18)));
      if (w == 0) btn.setSelected(true);
      final int wIdx = w;
      btn.addActionListener(
          e -> {
            params[base + FLT_LFO_WAVE] = LFO_WAVE_NORMS[wIdx];
            sendParam(base + FLT_LFO_WAVE, LFO_WAVE_NORMS[wIdx]);
          });
      lbg.add(btn);
      lfoTab.add(btn);
    }
    subTabs.addTab("LFO", lfoTab);

    panel.add(subTabs);
    return panel;
  }

  // ── FX Tab (placeholder) ────────────────────────────────────

  private JPanel createFxTab(Theme theme) {
    JPanel panel = new JPanel(new BorderLayout());
    panel.setBackground(theme.BG_MEDIUM);
    JLabel lbl = new JLabel("FX — Coming Soon", SwingConstants.CENTER);
    lbl.setForeground(new Color(0x555555));
    lbl.setFont(theme.FONT_UI.deriveFont(theme.scale(12.0f)));
    panel.add(lbl, BorderLayout.CENTER);
    return panel;
  }

  // ── Modulation Matrix (right side, always visible) ──────────

  private JPanel createMatrixPanel(Theme theme) {
    JPanel wrapper = new JPanel(new BorderLayout());
    wrapper.setBackground(MATRIX_BG);
    wrapper.setBorder(BorderFactory.createEmptyBorder(2, 4, 4, 4));

    // Title
    JLabel title = new JLabel("MATRIX", SwingConstants.CENTER);
    title.setForeground(ACCENT_YELLOW);
    title.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(10.0f)));
    wrapper.add(title, BorderLayout.NORTH);

    // Grid: (1 + NUM_OPS) rows × (1 + MATRIX_COLS) cols — visible grid lines via gap+bg color
    JPanel grid = new JPanel(new GridLayout(NUM_OPS + 1, MATRIX_COLS + 1, 1, 1));
    grid.setBackground(MATRIX_GRID);
    grid.setBorder(BorderFactory.createLineBorder(MATRIX_GRID, 1));

    // Header row
    grid.add(new JLabel()); // top-left empty cell
    for (int c = 0; c < MATRIX_COLS; c++) {
      JLabel lbl = new JLabel(MATRIX_COL_LABELS[c], SwingConstants.CENTER);
      lbl.setForeground(c < 6 ? ACCENT_YELLOW : (c < 9 ? FILTER_COLOR : new Color(0x888890)));
      lbl.setFont(theme.FONT_UI.deriveFont(theme.scale(7.0f)));
      lbl.setOpaque(true);
      lbl.setBackground(MATRIX_BG);
      grid.add(lbl);
    }

    // Matrix knob rows
    for (int r = 0; r < NUM_OPS; r++) {
      // Row label
      JLabel rowLbl = new JLabel(String.valueOf(r + 1), SwingConstants.CENTER);
      rowLbl.setForeground(OP_COLORS[r]);
      rowLbl.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(9.0f)));
      rowLbl.setOpaque(true);
      rowLbl.setBackground(MATRIX_BG);
      grid.add(rowLbl);

      for (int c = 0; c < MATRIX_COLS; c++) {
        int paramIdx = MATRIX_BASE + r * MATRIX_COLS + c;
        double defaultVal = (c == 11 && r == 0) ? 0.75 : 0.5;
        Color knobColor = r == c ? OP_COLORS[r] : (c < 6 ? ACCENT_YELLOW : ACCENT_PURPLE);
        KnobPanel knob = new MatrixKnobPanel(paramIdx, defaultVal, knobColor);
        matrixKnobs[r * MATRIX_COLS + c] = knob;
        allKnobs.put(paramIdx, knob);
        JPanel cell = new JPanel(new BorderLayout());
        cell.setBackground(MATRIX_BG);
        cell.add(knob, BorderLayout.CENTER);
        grid.add(cell);
      }
    }

    JScrollPane scroll = new JScrollPane(grid);
    scroll.setBorder(null);
    scroll.getViewport().setBackground(MATRIX_BG);
    wrapper.add(scroll, BorderLayout.CENTER);

    // FM/RM toggle at bottom of matrix
    JPanel modePanel = new JPanel(new FlowLayout(FlowLayout.CENTER, theme.scale(4), 0));
    modePanel.setBackground(MATRIX_BG);
    JToggleButton rmToggle = new JToggleButton("FM");
    rmToggle.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(9.0f)));
    rmToggle.setForeground(ACCENT_YELLOW);
    rmToggle.setBackground(new Color(0x2A2A30));
    rmToggle.setFocusPainted(false);
    rmToggle.setToolTipText("Toggle FM/RM modulation mode");
    rmToggle.addActionListener(
        e -> {
          boolean rm = rmToggle.isSelected();
          rmToggle.setText(rm ? "RM" : "FM");
          rmToggle.setForeground(rm ? new Color(0xFF6644) : ACCENT_YELLOW);
          params[G_RM_MODE] = rm ? 1.0 : 0.0;
          sendParam(G_RM_MODE, params[G_RM_MODE]);
        });
    modePanel.add(rmToggle);
    wrapper.add(modePanel, BorderLayout.SOUTH);

    return wrapper;
  }

  // ── Knob helper ─────────────────────────────────────────────

  private JPanel createKnob(String label, int paramId, double defaultVal, Theme theme) {
    JPanel p = new JPanel(new BorderLayout());
    p.setOpaque(false);
    p.setPreferredSize(new Dimension(theme.scale(40), theme.scale(52)));

    KnobPanel knob = new KnobPanel(paramId, defaultVal);
    allKnobs.put(paramId, knob);
    p.add(knob, BorderLayout.CENTER);

    JLabel l = new JLabel(label, SwingConstants.CENTER);
    l.setForeground(new Color(0x999999));
    l.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    p.add(l, BorderLayout.SOUTH);

    return p;
  }

  // ── Backend communication ───────────────────────────────────

  public void handleParamChange(int paramId, double value) {
    if (paramId >= 0 && paramId < TOTAL_PARAMS) {
      params[paramId] = value;
      KnobPanel knob = allKnobs.get(paramId);
      if (knob != null) {
        knob.value = value;
      }
      // Sync envelope editors when ADSR params change externally
      syncEnvelopeEditors(paramId);
      repaint();
    }
  }

  /** Sync envelope editor widgets when ADSR-related params change. */
  private void syncEnvelopeEditors(int paramId) {
    // Check operator envelopes
    for (int op = 0; op < NUM_OPS; op++) {
      int base = op * PARAMS_PER_OP;
      if (paramId >= base + OP_ENV_A && paramId <= base + OP_ENV_R) {
        if (opEnvelopes[op] != null) {
          opEnvelopes[op].setValues(
              (float) params[base + OP_ENV_A],
              (float) params[base + OP_ENV_D],
              (float) params[base + OP_ENV_S],
              (float) params[base + OP_ENV_R]);
        }
        // Sync MAIN tab if OP1
        if (op == 0 && mainEnvelope != null) {
          mainEnvelope.setValues(
              (float) params[OP_ENV_A],
              (float) params[OP_ENV_D],
              (float) params[OP_ENV_S],
              (float) params[OP_ENV_R]);
        }
        return;
      }
    }
    // Check filter envelopes
    for (int f = 0; f < NUM_FILTERS; f++) {
      int base = OP_PARAMS + f * PARAMS_PER_FILTER;
      if (paramId >= base + FLT_ENV_A && paramId <= base + FLT_ENV_R) {
        if (filterEnvelopes[f] != null) {
          filterEnvelopes[f].setValues(
              (float) params[base + FLT_ENV_A],
              (float) params[base + FLT_ENV_D],
              (float) params[base + FLT_ENV_S],
              (float) params[base + FLT_ENV_R]);
        }
        return;
      }
    }
  }

  /** Test accessor: get the params[] array value for a given paramId. */
  double getParamValue(int paramId) {
    return params[paramId];
  }

  /** Test accessor: get the KnobPanel's rendered value for a given paramId. */
  double getKnobValue(int paramId) {
    KnobPanel knob = allKnobs.get(paramId);
    return knob != null ? knob.value : Double.NaN;
  }

  private void sendParam(int paramId, double value) {
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
                        .setParamValue((float) value))
                .build());
  }

  private void sendRemove() {
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
  }

  // ── KnobPanel ───────────────────────────────────────────────

  private class KnobPanel extends JPanel {
    protected double value;
    protected final int paramId;
    private int dragStartY;

    KnobPanel(int paramId, double defaultVal) {
      this.paramId = paramId;
      this.value = defaultVal;
      params[paramId] = defaultVal;
      setOpaque(false);
      setPreferredSize(new Dimension(30, 30));
      setCursor(Cursor.getPredefinedCursor(Cursor.N_RESIZE_CURSOR));

      addMouseListener(
          new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
              dragStartY = e.getY();
            }
          });
      addMouseMotionListener(
          new MouseMotionAdapter() {
            @Override
            public void mouseDragged(MouseEvent e) {
              int dy = dragStartY - e.getY();
              dragStartY = e.getY();
              value = Math.max(0.0, Math.min(1.0, value + dy * 0.005));
              params[paramId] = value;
              sendParam(paramId, value);
              repaint();
            }
          });
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
      int size = Math.min(getWidth(), getHeight()) - 2;
      int x = (getWidth() - size) / 2;
      int y = (getHeight() - size) / 2;

      g2.setColor(new Color(0x333340));
      g2.setStroke(new BasicStroke(3.0f, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND));
      g2.drawArc(x, y, size, size, 225, -270);

      int arcAngle = (int) (-270 * value);
      g2.setColor(ACCENT_YELLOW);
      g2.drawArc(x, y, size, size, 225, arcAngle);

      g2.setColor(new Color(0xDDDDDD));
      double angle = Math.toRadians(225 - 270 * value);
      int cx = x + size / 2 + (int) ((size / 2 - 2) * Math.cos(angle));
      int cy = y + size / 2 - (int) ((size / 2 - 2) * Math.sin(angle));
      g2.fillOval(cx - 2, cy - 2, 4, 4);

      g2.dispose();
    }
  }

  // ── Matrix Knob (smaller, colored) ──────────────────────────

  private class MatrixKnobPanel extends KnobPanel {
    private final Color arcColor;

    MatrixKnobPanel(int paramId, double defaultVal, Color arcColor) {
      super(paramId, defaultVal);
      this.arcColor = arcColor;
      setPreferredSize(new Dimension(22, 22));
    }

    @Override
    protected void paintComponent(Graphics g) {
      Graphics2D g2 = (Graphics2D) g.create();
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
      int size = Math.min(getWidth(), getHeight()) - 2;
      int x = (getWidth() - size) / 2;
      int y = (getHeight() - size) / 2;

      // Inactive (neutral = 0.5): gray out, no color arc, no indicator
      boolean inactive = Math.abs(value - 0.5) < 0.01;

      g2.setColor(inactive ? MATRIX_INACTIVE : new Color(0x2A2A30));
      g2.setStroke(new BasicStroke(2.0f, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND));
      g2.drawArc(x, y, size, size, 225, -270);

      if (!inactive) {
        int arcAngle = (int) (-270 * value);
        g2.setColor(arcColor);
        g2.drawArc(x, y, size, size, 225, arcAngle);

        g2.setColor(new Color(0xCCCCCC));
        double angle = Math.toRadians(225 - 270 * value);
        int cx = x + size / 2 + (int) ((size / 2 - 1) * Math.cos(angle));
        int cy = y + size / 2 - (int) ((size / 2 - 1) * Math.sin(angle));
        g2.fillOval(cx - 1, cy - 1, 3, 3);
      }

      g2.dispose();
    }
  }

  // ── Defaults ────────────────────────────────────────────────

  private void initDefaults() {
    // Op defaults
    for (int op = 0; op < NUM_OPS; op++) {
      int base = op * PARAMS_PER_OP;
      params[base + OP_WAVEFORM] = 0.0;
      params[base + OP_LEVEL] = (op == 0) ? 1.0 : 0.0;
      params[base + OP_RATIO] = 0.5;
      params[base + OP_FINE] = 0.5;
      params[base + OP_ENV_A] = 0.0;
      params[base + OP_ENV_D] = 0.2;
      params[base + OP_ENV_S] = 0.7;
      params[base + OP_ENV_R] = 0.3;
      params[base + OP_FEEDBACK] = 0.0;
      params[base + OP_PAN] = 0.5;
      params[base + OP_LFO_RATE] = 0.3;
      params[base + OP_LFO_DEPTH] = 0.0;
      params[base + OP_LFO_WAVE] = 0.0;
      params[base + OP_PHASE] = 0.0;
      params[base + OP_SHAPE] = 0.5;
      params[base + OP_TENSION] = 0.5;
      params[base + OP_SKEW] = 0.5;
      params[base + OP_SINE_SHAPER] = 0.0;
      params[base + OP_NOISE_MIX] = 0.0;
      params[base + OP_FREQ_OFFSET] = 0.5;
      params[base + OP_HALF] = 0.0;
      params[base + OP_EVEN] = 0.0;
      params[base + OP_ABSOLUTE] = 0.0;
    }
    // Filter defaults
    for (int f = 0; f < NUM_FILTERS; f++) {
      int base = OP_PARAMS + f * PARAMS_PER_FILTER;
      params[base + FLT_TYPE] = 0.0;
      params[base + FLT_CUTOFF] = 1.0;
      params[base + FLT_RESONANCE] = 0.0;
      params[base + FLT_ENV_A] = 0.0;
      params[base + FLT_ENV_D] = 0.2;
      params[base + FLT_ENV_S] = 0.7;
      params[base + FLT_ENV_R] = 0.3;
      params[base + FLT_ENV_DEPTH] = 0.5;
      params[base + FLT_MIX] = 1.0;
      params[base + FLT_LFO_RATE] = 0.3;
      params[base + FLT_LFO_DEPTH] = 0.0;
      params[base + FLT_LFO_WAVE] = 0.0;
    }
    // Global defaults
    params[G_ALGORITHM] = 0.0;
    params[G_MASTER_VOL] = 0.8;
    params[G_ENABLE] = 1.0;
    params[G_UNISON_VOICES] = 0.0;
    params[G_UNISON_DETUNE] = 0.0;
    params[G_UNISON_SPREAD] = 0.5;
    params[G_PORTAMENTO] = 0.0;
    // Matrix defaults
    for (int r = 0; r < NUM_OPS; r++) {
      for (int c = 0; c < MATRIX_COLS; c++) {
        int idx = MATRIX_BASE + r * MATRIX_COLS + c;
        params[idx] = (c == 11 && r == 0) ? 0.75 : 0.5;
      }
    }
  }
}
