package hibiki.ui.panels.devices;

import hibiki.ui.PluginPane;
import hibiki.ui.Theme;
import hibiki.ui.panels.KnobPanel;
import java.awt.*;
import java.awt.event.*;
import javax.swing.*;
import javax.swing.event.ChangeEvent;
import javax.swing.event.ChangeListener;

/**
 * OTT-style three-band multiband compressor device panel ("Hott"). Features T/B/A tabs (Time,
 * Below, Above) for per-band configuration, Soft Knee / RMS toggle buttons, draggable thresholds
 * and per-band att/rel controls.
 *
 * <p>Reference: https://www.makou.com/available-xfer-ott-parameters/
 */
public class HottDevicePanel extends AbstractDevicePanel {
  // Parameter IDs matching C++ BuiltinHott::ParamId
  private static final int PARAM_LOW_XOVER = 0;
  private static final int PARAM_HIGH_XOVER = 1;
  private static final int PARAM_AMOUNT = 2;
  private static final int PARAM_TIME = 3;
  private static final int PARAM_OUTPUT = 4;
  private static final int PARAM_LOW_OUT = 5;
  private static final int PARAM_MID_OUT = 6;
  private static final int PARAM_HIGH_OUT = 7;
  private static final int PARAM_ENABLE = 8;
  private static final int PARAM_LOW_DOWN_THRESH = 9;
  private static final int PARAM_MID_DOWN_THRESH = 10;
  private static final int PARAM_HIGH_DOWN_THRESH = 11;
  private static final int PARAM_LOW_UP_THRESH = 12;
  private static final int PARAM_MID_UP_THRESH = 13;
  private static final int PARAM_HIGH_UP_THRESH = 14;
  private static final int PARAM_SOFT_KNEE = 15;
  private static final int PARAM_RMS_MODE = 16;
  private static final int PARAM_LOW_ATTACK = 17;
  private static final int PARAM_MID_ATTACK = 18;
  private static final int PARAM_HIGH_ATTACK = 19;
  private static final int PARAM_LOW_RELEASE = 20;
  private static final int PARAM_MID_RELEASE = 21;
  private static final int PARAM_HIGH_RELEASE = 22;
  private static final int PARAM_LOW_DOWN_RATIO = 23;
  private static final int PARAM_MID_DOWN_RATIO = 24;
  private static final int PARAM_HIGH_DOWN_RATIO = 25;
  private static final int PARAM_LOW_UP_RATIO = 26;
  private static final int PARAM_MID_UP_RATIO = 27;
  private static final int PARAM_HIGH_UP_RATIO = 28;
  private static final int PARAM_LOW_IN = 29;
  private static final int PARAM_MID_IN = 30;
  private static final int PARAM_HIGH_IN = 31;
  private static final int TOTAL_PARAMS = 32;

  // Band display names (display order: top=High, mid=Mid, bottom=Low)
  private static final String[] BAND_NAMES = {"High", "Mid", "Low"};

  // T/B/A tab modes
  private static final int TAB_TIME = 0;
  private static final int TAB_BELOW = 1;
  private static final int TAB_ABOVE = 2;
  private boolean enabled = true;
  private final float[] bandGrDb = {0, 0, 0};
  private float inputDb = -200, outputDb = -200;
  private int activeTab = TAB_TIME; // Current T/B/A tab
  private final KnobPanel knobLowXover, knobHighXover;
  private final KnobPanel knobAmount, knobTime, knobOutput;
  private final KnobPanel knobLowOut, knobMidOut, knobHighOut;
  private final BandMeterPanel meterPanel;
  private JToggleButton softKneeBtn, rmsBtn;
  private JToggleButton tabT, tabB, tabA;

  public HottDevicePanel(int trackIndex, int pluginIndex) {
    super(trackIndex, pluginIndex, TOTAL_PARAMS);

    // Defaults matching C++ BuiltinHott::reset()
    params[PARAM_LOW_XOVER] = 0.461;
    params[PARAM_HIGH_XOVER] = 0.436;
    params[PARAM_AMOUNT] = 1.0;
    params[PARAM_TIME] = 1.0;
    params[PARAM_OUTPUT] = 0.5;
    params[PARAM_LOW_OUT] = 0.715;
    params[PARAM_MID_OUT] = 0.619;
    params[PARAM_HIGH_OUT] = 0.715;
    params[PARAM_ENABLE] = 1.0;
    params[PARAM_LOW_DOWN_THRESH] = (-33.8 + 60.0) / 60.0;
    params[PARAM_MID_DOWN_THRESH] = (-30.2 + 60.0) / 60.0;
    params[PARAM_HIGH_DOWN_THRESH] = (-35.5 + 60.0) / 60.0;
    params[PARAM_LOW_UP_THRESH] = (-40.8 + 60.0) / 72.0;
    params[PARAM_MID_UP_THRESH] = (-41.8 + 60.0) / 72.0;
    params[PARAM_HIGH_UP_THRESH] = (-40.8 + 60.0) / 72.0;
    params[PARAM_SOFT_KNEE] = 1.0;
    params[PARAM_RMS_MODE] = 1.0;
    // att/rel/ratio norms will be set from backend on load; use reasonable
    // placeholders
    // Attack norm: log10(attack_ms/0.1)/3
    params[PARAM_LOW_ATTACK] = Math.log10(47.8 / 0.1) / 3.0;
    params[PARAM_MID_ATTACK] = Math.log10(22.4 / 0.1) / 3.0;
    params[PARAM_HIGH_ATTACK] = Math.log10(13.5 / 0.1) / 3.0;
    params[PARAM_LOW_RELEASE] = Math.log10(282.0 / 10.0) / 2.0;
    params[PARAM_MID_RELEASE] = Math.log10(282.0 / 10.0) / 2.0;
    params[PARAM_HIGH_RELEASE] = Math.log10(132.0 / 10.0) / 2.0;
    // Ratio norm: 1 - 1/ratio
    params[PARAM_LOW_DOWN_RATIO] = 1.0 - 1.0 / 66.7;
    params[PARAM_MID_DOWN_RATIO] = 1.0 - 1.0 / 66.7;
    params[PARAM_HIGH_DOWN_RATIO] = 0.999; // inf
    params[PARAM_LOW_UP_RATIO] = 1.0 - 1.0 / 4.17;
    params[PARAM_MID_UP_RATIO] = 1.0 - 1.0 / 4.17;
    params[PARAM_HIGH_UP_RATIO] = 1.0 - 1.0 / 4.17;
    params[PARAM_LOW_IN] = 0.608;
    params[PARAM_MID_IN] = 0.608;
    params[PARAM_HIGH_IN] = 0.608;

    Theme theme = Theme.getInstance();
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(theme.scale(520), theme.scale(260)));
    setMaximumSize(new Dimension(theme.scale(520), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // ── Header ──
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x1A6B8A));
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));

    JLabel nameLabel = new JLabel("⚡ Hott");
    nameLabel.setForeground(new Color(0x00D4FF));
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
    JToggleButton enableBtn = new JToggleButton("On", enabled);
    enableBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    enableBtn.setFocusPainted(false);
    enableBtn.addActionListener(
        e -> {
          enabled = enableBtn.isSelected();
          sendParam(PARAM_ENABLE, enabled ? 1.0 : 0.0);
          repaint();
        });
    btnPanel.add(enableBtn);
    JButton delBtn = new JButton("\u274C");
    delBtn.addActionListener(e -> sendRemove());
    btnPanel.add(delBtn);
    header.add(btnPanel, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // ── Main body: [Left: split freq + input knobs] [Center: meters] [Right:
    // output knobs] ──
    JPanel body = new JPanel(new BorderLayout(theme.scale(4), 0));
    body.setBackground(theme.BG_MEDIUM);
    body.setBorder(
        BorderFactory.createEmptyBorder(
            theme.scale(4), theme.scale(4), theme.scale(4), theme.scale(4)));

    // Left column: split freq knobs + per-band input knobs
    JPanel leftCol = new JPanel();
    leftCol.setLayout(new BoxLayout(leftCol, BoxLayout.Y_AXIS));
    leftCol.setBackground(theme.BG_MEDIUM);
    leftCol.setPreferredSize(new Dimension(theme.scale(70), 0));

    JLabel splitLabel = new JLabel("Split Freq", SwingConstants.CENTER);
    splitLabel.setForeground(theme.TEXT_DIM);
    splitLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    splitLabel.setAlignmentX(Component.CENTER_ALIGNMENT);
    leftCol.add(splitLabel);
    leftCol.add(Box.createVerticalStrut(theme.scale(2)));

    // High crossover
    JLabel hiLabel = new JLabel("High", SwingConstants.CENTER);
    hiLabel.setForeground(new Color(0x00D4FF));
    hiLabel.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(9.0f)));
    hiLabel.setAlignmentX(Component.CENTER_ALIGNMENT);
    leftCol.add(hiLabel);

    knobHighXover =
        new KnobPanel(
            "HiFreq",
            params[PARAM_HIGH_XOVER],
            v -> String.format("%.2f kHz", normToFreq(v, 500, 20000) / 1000));
    knobHighXover.addChangeListener(e -> onKnobChanged(PARAM_HIGH_XOVER, knobHighXover));
    leftCol.add(knobHighXover);

    leftCol.add(Box.createVerticalGlue());

    // Low crossover
    JLabel loLabel = new JLabel("Low", SwingConstants.CENTER);
    loLabel.setForeground(new Color(0x00D4FF));
    loLabel.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(9.0f)));
    loLabel.setAlignmentX(Component.CENTER_ALIGNMENT);
    leftCol.add(loLabel);

    knobLowXover =
        new KnobPanel(
            "LoFreq",
            params[PARAM_LOW_XOVER],
            v -> String.format("%.1f Hz", normToFreq(v, 20, 500)));
    knobLowXover.addChangeListener(e -> onKnobChanged(PARAM_LOW_XOVER, knobLowXover));
    leftCol.add(knobLowXover);

    // Soft Knee / RMS buttons at bottom of left column
    leftCol.add(Box.createVerticalStrut(theme.scale(4)));
    softKneeBtn = new JToggleButton("Soft Knee", params[PARAM_SOFT_KNEE] >= 0.5);
    softKneeBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    softKneeBtn.setFocusPainted(false);
    softKneeBtn.setAlignmentX(Component.CENTER_ALIGNMENT);
    softKneeBtn.setMaximumSize(new Dimension(theme.scale(66), theme.scale(20)));
    softKneeBtn.addActionListener(
        e -> {
          params[PARAM_SOFT_KNEE] = softKneeBtn.isSelected() ? 1.0 : 0.0;
          sendParam(PARAM_SOFT_KNEE, params[PARAM_SOFT_KNEE]);
        });
    leftCol.add(softKneeBtn);
    leftCol.add(Box.createVerticalStrut(theme.scale(2)));

    rmsBtn = new JToggleButton("RMS", params[PARAM_RMS_MODE] >= 0.5);
    rmsBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    rmsBtn.setFocusPainted(false);
    rmsBtn.setAlignmentX(Component.CENTER_ALIGNMENT);
    rmsBtn.setMaximumSize(new Dimension(theme.scale(66), theme.scale(20)));
    rmsBtn.addActionListener(
        e -> {
          params[PARAM_RMS_MODE] = rmsBtn.isSelected() ? 1.0 : 0.0;
          sendParam(PARAM_RMS_MODE, params[PARAM_RMS_MODE]);
        });
    leftCol.add(rmsBtn);

    body.add(leftCol, BorderLayout.WEST);

    // Center: band meters with T/B/A tabs
    meterPanel = new BandMeterPanel();
    body.add(meterPanel, BorderLayout.CENTER);

    // Right column: per-band output + global output/time/amount
    JPanel rightCol = new JPanel();
    rightCol.setLayout(new BoxLayout(rightCol, BoxLayout.Y_AXIS));
    rightCol.setBackground(theme.BG_MEDIUM);
    rightCol.setPreferredSize(new Dimension(theme.scale(160), 0));

    // Per-band output knobs in a row
    JPanel bandOutRow = new JPanel(new GridLayout(1, 3, theme.scale(2), 0));
    bandOutRow.setOpaque(false);
    bandOutRow.setMaximumSize(new Dimension(Short.MAX_VALUE, theme.scale(72)));

    knobHighOut =
        new KnobPanel("Hi Out", params[PARAM_HIGH_OUT], v -> String.format("%.1f dB", v * 48 - 24));
    knobHighOut.addChangeListener(e -> onKnobChanged(PARAM_HIGH_OUT, knobHighOut));
    bandOutRow.add(knobHighOut);

    knobMidOut =
        new KnobPanel("Mid Out", params[PARAM_MID_OUT], v -> String.format("%.1f dB", v * 48 - 24));
    knobMidOut.addChangeListener(e -> onKnobChanged(PARAM_MID_OUT, knobMidOut));
    bandOutRow.add(knobMidOut);

    knobLowOut =
        new KnobPanel("Lo Out", params[PARAM_LOW_OUT], v -> String.format("%.1f dB", v * 48 - 24));
    knobLowOut.addChangeListener(e -> onKnobChanged(PARAM_LOW_OUT, knobLowOut));
    bandOutRow.add(knobLowOut);

    rightCol.add(bandOutRow);

    // Separator
    rightCol.add(Box.createVerticalStrut(theme.scale(4)));
    JSeparator sep = new JSeparator();
    sep.setMaximumSize(new Dimension(Short.MAX_VALUE, 1));
    sep.setForeground(theme.BORDER);
    rightCol.add(sep);
    rightCol.add(Box.createVerticalStrut(theme.scale(4)));

    // Global knobs row
    JPanel globalRow = new JPanel(new GridLayout(1, 3, theme.scale(2), 0));
    globalRow.setOpaque(false);
    globalRow.setMaximumSize(new Dimension(Short.MAX_VALUE, theme.scale(72)));

    knobOutput =
        new KnobPanel("Output", params[PARAM_OUTPUT], v -> String.format("%.1f dB", v * 48 - 24));
    knobOutput.addChangeListener(e -> onKnobChanged(PARAM_OUTPUT, knobOutput));
    globalRow.add(knobOutput);

    knobTime = new KnobPanel("Time", params[PARAM_TIME], v -> String.format("%.0f %%", v * 100));
    knobTime.addChangeListener(e -> onKnobChanged(PARAM_TIME, knobTime));
    globalRow.add(knobTime);

    knobAmount =
        new KnobPanel("Amount", params[PARAM_AMOUNT], v -> String.format("%.0f %%", v * 100));
    knobAmount.addChangeListener(e -> onKnobChanged(PARAM_AMOUNT, knobAmount));
    globalRow.add(knobAmount);

    rightCol.add(globalRow);
    rightCol.add(Box.createVerticalGlue());

    body.add(rightCol, BorderLayout.EAST);
    add(body, BorderLayout.CENTER);
  }

  private void onKnobChanged(int paramId, KnobPanel knob) {
    if (updatingFromBackend) return;
    params[paramId] = knob.getValue();
    sendParam(paramId, params[paramId]);
    meterPanel.repaint();
  }

  /** Update a parameter from backend notification. */
  public void updateParam(int paramId, float value) {
    if (paramId < 0 || paramId >= TOTAL_PARAMS) return;
    updatingFromBackend = true;
    params[paramId] = value;
    if (paramId == PARAM_ENABLE) {
      enabled = value >= 0.5;
    } else if (paramId == PARAM_SOFT_KNEE) {
      softKneeBtn.setSelected(value >= 0.5);
    } else if (paramId == PARAM_RMS_MODE) {
      rmsBtn.setSelected(value >= 0.5);
    } else {
      KnobPanel knob = getKnobForParam(paramId);
      if (knob != null) knob.setValue(value);
    }
    updatingFromBackend = false;
    meterPanel.repaint();
  }

  /** Update metering data from backend. */
  public void setGainReduction(float db) {
    bandGrDb[0] = db;
    bandGrDb[1] = db;
    bandGrDb[2] = db;
    meterPanel.repaint();
  }

  /** Update real-time input/output levels. */
  public void setInputOutputLevel(float inDb, float outDb) {
    this.inputDb = inDb;
    this.outputDb = outDb;
    meterPanel.repaint();
  }

  private KnobPanel getKnobForParam(int paramId) {
    switch (paramId) {
      case PARAM_LOW_XOVER:
        return knobLowXover;
      case PARAM_HIGH_XOVER:
        return knobHighXover;
      case PARAM_AMOUNT:
        return knobAmount;
      case PARAM_TIME:
        return knobTime;
      case PARAM_OUTPUT:
        return knobOutput;
      case PARAM_LOW_OUT:
        return knobLowOut;
      case PARAM_MID_OUT:
        return knobMidOut;
      case PARAM_HIGH_OUT:
        return knobHighOut;
      default:
        return null; // threshold/att/rel/ratio params handled via meter panel drag
    }
  }

  // ─── Band Meter Panel (OTT-style with T/B/A tabs) ─────────────────────

  /** dB range for display: -80 to 0 dB */
  private static final float DB_MIN = -80f;

  private static final float DB_MAX = 0f;

  private static float dbToNorm(float db) {
    return Math.max(0f, Math.min(1f, (db - DB_MIN) / (DB_MAX - DB_MIN)));
  }

  private static float downThreshNormToDb(double norm) {
    return (float) (norm * 60.0 - 60.0);
  }

  private static double dbToDownThreshNorm(float db) {
    return Math.max(0.0, Math.min(1.0, (db + 60.0) / 60.0));
  }

  private static float upThreshNormToDb(double norm) {
    return (float) (norm * 72.0 - 60.0);
  }

  private static double dbToUpThreshNorm(float db) {
    return Math.max(0.0, Math.min(1.0, (db + 60.0) / 72.0));
  }

  /** Convert normalized attack param to ms: attack_ms = 0.1 * 1000^norm */
  private static float attackNormToMs(double norm) {
    return (float) (0.1 * Math.pow(1000.0, norm));
  }

  /** Convert ms to normalized attack param */
  private static double msToAttackNorm(float ms) {
    return Math.max(0.0, Math.min(1.0, Math.log10(ms / 0.1) / 3.0));
  }

  /** Convert normalized release param to ms: release_ms = 10 * 100^norm */
  private static float releaseNormToMs(double norm) {
    return (float) (10.0 * Math.pow(100.0, norm));
  }

  /** Convert ms to normalized release param */
  private static double msToReleaseNorm(float ms) {
    return Math.max(0.0, Math.min(1.0, Math.log10(ms / 10.0) / 2.0));
  }

  /** Convert normalized ratio param to ratio: ratio = 1/(1-norm) */
  private static float ratioNormToRatio(double norm) {
    if (norm >= 0.999) return 1000.0f;
    return 1.0f / (1.0f - (float) norm);
  }

  /** Convert ratio to normalized param */
  private static double ratioToNorm(float ratio) {
    if (ratio >= 999.0f) return 0.999;
    return Math.max(0.0, Math.min(0.999, 1.0 - 1.0 / ratio));
  }

  // Band index mapping: display order [0=High, 1=Mid, 2=Low]
  // to param order [High=2, Mid=1, Low=0]
  private static final int[] DISPLAY_TO_PARAM = {2, 1, 0};
  private static final int[] DOWN_THRESH_PARAMS = {
    PARAM_HIGH_DOWN_THRESH, PARAM_MID_DOWN_THRESH, PARAM_LOW_DOWN_THRESH
  };
  private static final int[] UP_THRESH_PARAMS = {
    PARAM_HIGH_UP_THRESH, PARAM_MID_UP_THRESH, PARAM_LOW_UP_THRESH
  };
  private static final int[] ATTACK_PARAMS = {
    PARAM_HIGH_ATTACK, PARAM_MID_ATTACK, PARAM_LOW_ATTACK
  };
  private static final int[] RELEASE_PARAMS = {
    PARAM_HIGH_RELEASE, PARAM_MID_RELEASE, PARAM_LOW_RELEASE
  };
  private static final int[] DOWN_RATIO_PARAMS = {
    PARAM_HIGH_DOWN_RATIO, PARAM_MID_DOWN_RATIO, PARAM_LOW_DOWN_RATIO
  };
  private static final int[] UP_RATIO_PARAMS = {
    PARAM_HIGH_UP_RATIO, PARAM_MID_UP_RATIO, PARAM_LOW_UP_RATIO
  };
  private static final int[] IN_GAIN_PARAMS = {PARAM_HIGH_IN, PARAM_MID_IN, PARAM_LOW_IN};

  private class BandMeterPanel extends JPanel {
    private int dragBand = -1;
    private boolean dragIsUp = false;
    private int dragType = 0; // 0=threshold, 1=att/rel value, 2=ratio

    BandMeterPanel() {
      setBackground(Theme.getInstance().BG_DARKER);
      setCursor(Cursor.getPredefinedCursor(Cursor.E_RESIZE_CURSOR));

      addMouseListener(
          new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
              handlePress(e.getX(), e.getY());
            }

            @Override
            public void mouseReleased(MouseEvent e) {
              dragBand = -1;
            }
          });
      addMouseMotionListener(
          new MouseMotionAdapter() {
            @Override
            public void mouseDragged(MouseEvent e) {
              handleDrag(e.getX(), e.getY());
            }
          });
    }

    private void handlePress(int mx, int my) {
      Theme theme = Theme.getInstance();
      int w = getWidth(), h = getHeight();
      int pad = theme.scale(6);
      int tabH = theme.scale(18);
      int bandAreaH = h - pad * 2 - tabH;
      int bandH = (bandAreaH - pad * 2) / 3;

      for (int b = 0; b < 3; b++) {
        int y = pad + b * (bandH + pad);
        if (my >= y && my < y + bandH) {
          dragBand = b;
          int meterW = getMeterWidth();
          int attRelW = getAttRelWidth();
          int meterX = pad;

          if (activeTab == TAB_TIME) {
            // Right side: att/rel area — draggable vertically
            int attRelX = pad + meterW + theme.scale(4);
            if (mx >= attRelX) {
              dragType = 1;
              // Upper half = attack, lower half = release
              dragIsUp = (my - y) < bandH / 2;
              handleDrag(mx, my);
              return;
            }
          }
          // Threshold drag in meter area
          dragType = 0;
          if (activeTab == TAB_BELOW) {
            dragIsUp = true; // below tab -> upward threshold
          } else if (activeTab == TAB_ABOVE) {
            dragIsUp = false; // above tab -> downward threshold
          } else {
            // Time tab: determine by proximity
            float downDb = downThreshNormToDb(params[DOWN_THRESH_PARAMS[b]]);
            float upDb = upThreshNormToDb(params[UP_THRESH_PARAMS[b]]);
            float downX = pad + dbToNorm(downDb) * meterW;
            float upX = pad + dbToNorm(upDb) * meterW;
            dragIsUp = Math.abs(mx - upX) < Math.abs(mx - downX);
          }
          handleDrag(mx, my);
          break;
        }
      }
    }

    private void handleDrag(int mx, int my) {
      if (dragBand < 0) return;
      Theme theme = Theme.getInstance();
      int pad = theme.scale(6);
      int meterW = getMeterWidth();

      if (dragType == 1) {
        // Dragging att/rel value — horizontal drag maps to ms
        int attRelX = pad + meterW + theme.scale(4);
        int attRelW = getAttRelWidth();
        float fraction = Math.max(0f, Math.min(1f, (mx - attRelX) / (float) attRelW));

        if (dragIsUp) {
          // Attack: map fraction to 0.1 - 100ms range (norm space)
          double norm = fraction;
          params[ATTACK_PARAMS[dragBand]] = norm;
          sendParam(ATTACK_PARAMS[dragBand], norm);
        } else {
          // Release: map fraction to 10 - 1000ms range (norm space)
          double norm = fraction;
          params[RELEASE_PARAMS[dragBand]] = norm;
          sendParam(RELEASE_PARAMS[dragBand], norm);
        }
        repaint();
        return;
      }

      // Threshold drag
      float db = DB_MIN + (mx - pad) * (DB_MAX - DB_MIN) / meterW;
      db = Math.max(DB_MIN, Math.min(DB_MAX, db));

      int paramId;
      double normVal;
      if (dragIsUp) {
        paramId = UP_THRESH_PARAMS[dragBand];
        normVal = dbToUpThreshNorm(db);
      } else {
        paramId = DOWN_THRESH_PARAMS[dragBand];
        normVal = dbToDownThreshNorm(db);
      }
      params[paramId] = normVal;
      sendParam(paramId, normVal);
      repaint();
    }

    private int getMeterWidth() {
      Theme theme = Theme.getInstance();
      int pad = theme.scale(6);
      int attRelW = getAttRelWidth();
      return getWidth() - pad * 2 - attRelW - theme.scale(4) - theme.scale(30); // tab area
    }

    private int getAttRelWidth() {
      return Theme.getInstance().scale(70);
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
      Theme theme = Theme.getInstance();
      int w = getWidth(), h = getHeight();
      int pad = theme.scale(6);
      int tabAreaW = theme.scale(30);
      int attRelW = getAttRelWidth();
      int meterW = w - pad * 2 - attRelW - theme.scale(4) - tabAreaW;
      int tabH = theme.scale(18);
      int bandAreaH = h - pad - tabH;
      int bandH = (bandAreaH - pad * 2) / 3;

      // dB gridlines
      g2.setFont(theme.FONT_UI.deriveFont(theme.scale(7.0f)));
      g2.setColor(new Color(255, 255, 255, 40));
      for (int db : new int[] {-80, -70, -60, -50, -40, -30, -20, -10, 0}) {
        float norm = dbToNorm(db);
        int x = pad + (int) (norm * meterW);
        g2.drawLine(x, pad, x, bandAreaH);
      }
      // dB labels at bottom of meter area
      g2.setColor(new Color(255, 255, 255, 60));
      int scaleY = bandAreaH + theme.scale(8);
      for (int db : new int[] {-80, -60, -40, -20, 0}) {
        float norm = dbToNorm(db);
        int x = pad + (int) (norm * meterW);
        g2.drawString(String.valueOf(Math.abs(db)), x - theme.scale(4), scaleY);
      }

      Color accentCyan = new Color(0x00D4FF);
      Color cyanDim = new Color(0x00, 0xD4, 0xFF, 80);
      Color meterBg = new Color(0x2A3540);
      Color grOrange = new Color(0xFF8C00);

      // --- Att/Rel header ---
      int attRelX = pad + meterW + theme.scale(4);
      g2.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(9.0f)));
      g2.setColor(theme.TEXT_DIM);
      String attRelHeader;
      if (activeTab == TAB_TIME) {
        attRelHeader = "Att/Rel";
      } else if (activeTab == TAB_BELOW) {
        attRelHeader = "Below";
      } else {
        attRelHeader = "Above";
      }
      g2.drawString(attRelHeader, attRelX, pad - 1);

      for (int b = 0; b < 3; b++) {
        int y = pad + b * (bandH + pad);

        // Band background
        g2.setColor(meterBg);
        g2.fillRect(pad, y, meterW, bandH);

        // Get thresholds in dB
        float downDb = downThreshNormToDb(params[DOWN_THRESH_PARAMS[b]]);
        float upDb = upThreshNormToDb(params[UP_THRESH_PARAMS[b]]);
        float downNorm = dbToNorm(downDb);
        float upNorm = dbToNorm(upDb);
        int downX = pad + (int) (downNorm * meterW);
        int upX = pad + (int) (upNorm * meterW);

        // Upward compression zone: cyan fill from left to upward threshold
        g2.setColor(cyanDim);
        int upZoneW = Math.max(0, upX - pad);
        g2.fillRect(pad, y, upZoneW, bandH);

        // Downward compression zone
        g2.setColor(new Color(0x1A, 0x25, 0x30, 120));
        int downZoneStart = Math.max(pad, downX);
        g2.fillRect(downZoneStart, y, pad + meterW - downZoneStart, bandH);

        // Gridlines inside band
        g2.setColor(new Color(255, 255, 255, 15));
        for (int db : new int[] {-60, -40, -20}) {
          float gn = dbToNorm(db);
          int gx = pad + (int) (gn * meterW);
          g2.drawLine(gx, y, gx, y + bandH);
        }

        // Threshold lines (thick cyan)
        g2.setColor(accentCyan);
        g2.setStroke(new BasicStroke(2.0f));
        g2.drawLine(upX, y, upX, y + bandH);
        g2.drawLine(downX, y, downX, y + bandH);
        g2.setStroke(new BasicStroke(1.0f));

        // Output level bar
        if (outputDb > DB_MIN) {
          float outNorm = dbToNorm(outputDb);
          int outW = (int) (outNorm * meterW);
          g2.setColor(new Color(0x00, 0xD4, 0xFF, 140));
          int barH = theme.scale(3);
          int barY = y + bandH / 2 - barH / 2;
          g2.fillRect(pad, barY, outW, barH);
        }

        // Input level indicator (orange dot)
        if (inputDb > DB_MIN) {
          float inNorm = dbToNorm(inputDb);
          int inX = pad + (int) (inNorm * meterW);
          g2.setColor(grOrange);
          int dotH = theme.scale(8);
          int dotW = theme.scale(3);
          g2.fillRect(inX - dotW / 2, y + bandH / 2 - dotH / 2, dotW, dotH);
        }

        // Threshold value labels in the band
        g2.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(10.0f)));
        g2.setColor(accentCyan);
        FontMetrics fm = g2.getFontMetrics();
        String downText = String.format("%.1f", downDb);
        int dtLabelX = Math.max(pad + 2, downX - fm.stringWidth(downText) - theme.scale(3));
        g2.drawString(downText, dtLabelX, y + bandH - theme.scale(3));

        // --- Right-side per-band info (depends on active tab) ---
        g2.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(10.0f)));
        g2.setColor(accentCyan);
        FontMetrics fm2 = g2.getFontMetrics();

        if (activeTab == TAB_TIME) {
          // Show Att/Rel times
          float attMs = attackNormToMs(params[ATTACK_PARAMS[b]]);
          float relMs = releaseNormToMs(params[RELEASE_PARAMS[b]]);
          String attText = String.format("%.1f ms", attMs);
          String relText = String.format("%.0f ms", relMs);
          g2.drawString(attText, attRelX, y + theme.scale(12));
          g2.drawString(relText, attRelX, y + bandH - theme.scale(3));
        } else if (activeTab == TAB_BELOW) {
          // Show upward threshold + ratio
          String thText = String.format("%.1f dB", upDb);
          float upRatio = ratioNormToRatio(params[UP_RATIO_PARAMS[b]]);
          String ratioText = String.format("1:%.2f", upRatio);
          g2.drawString(thText, attRelX, y + theme.scale(12));
          g2.setColor(new Color(0x00D4FF, true).brighter());
          g2.drawString(ratioText, attRelX, y + bandH - theme.scale(3));
        } else {
          // TAB_ABOVE: show downward threshold + ratio
          String thText = String.format("%.1f dB", downDb);
          float downRatio = ratioNormToRatio(params[DOWN_RATIO_PARAMS[b]]);
          String ratioText = downRatio >= 999f ? "inf:1" : String.format("%.1f:1", downRatio);
          g2.drawString(thText, attRelX, y + theme.scale(12));
          g2.setColor(new Color(0x00D4FF, true).brighter());
          g2.drawString(ratioText, attRelX, y + bandH - theme.scale(3));
        }

        // Band name label (small, top-right of band area)
        g2.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
        g2.setColor(new Color(255, 255, 255, 100));
        FontMetrics fm3 = g2.getFontMetrics();
        g2.drawString(
            BAND_NAMES[b],
            pad + meterW - fm3.stringWidth(BAND_NAMES[b]) - theme.scale(2),
            y + theme.scale(10));
      }

      // --- T/B/A tab buttons (bottom-right corner) ---
      int tabX = w - tabAreaW - pad;
      int tabY = bandAreaH + theme.scale(2);
      int tabBtnW = theme.scale(18);
      int tabBtnH = theme.scale(14);
      int tabGap = theme.scale(2);
      String[] tabLabels = {"T", "B", "A"};
      int[] tabs = {TAB_TIME, TAB_BELOW, TAB_ABOVE};

      for (int t = 0; t < 3; t++) {
        int tx = tabX + t * (tabBtnW + tabGap);
        boolean active = (activeTab == tabs[t]);
        g2.setColor(active ? accentCyan : new Color(0x3A4550));
        g2.fillRoundRect(tx, tabY, tabBtnW, tabBtnH, 3, 3);
        g2.setColor(active ? Color.BLACK : new Color(0xBBBBBB));
        g2.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(9.0f)));
        FontMetrics tfm = g2.getFontMetrics();
        g2.drawString(
            tabLabels[t],
            tx + (tabBtnW - tfm.stringWidth(tabLabels[t])) / 2,
            tabY + tabBtnH - theme.scale(3));
      }

      // Store tab button positions for click detection
      this.tabBtnX = tabX;
      this.tabBtnY = tabY;
      this.tabBtnW = tabBtnW;
      this.tabBtnH = tabBtnH;
      this.tabGap = tabGap;

      g2.dispose();
    }

    // Tab button hit detection state
    private int tabBtnX, tabBtnY, tabBtnW, tabBtnH, tabGap;

    {
      // Add click handler for tab buttons
      addMouseListener(
          new MouseAdapter() {
            @Override
            public void mouseClicked(MouseEvent e) {
              int mx = e.getX(), my = e.getY();
              int[] tabs = {TAB_TIME, TAB_BELOW, TAB_ABOVE};
              for (int t = 0; t < 3; t++) {
                int tx = tabBtnX + t * (tabBtnW + tabGap);
                if (mx >= tx && mx < tx + tabBtnW && my >= tabBtnY && my < tabBtnY + tabBtnH) {
                  activeTab = tabs[t];
                  repaint();
                  return;
                }
              }
            }
          });
    }
  }

  // ─── Knob Panel ─────────────────────────────────────────────────

  @FunctionalInterface
  interface ValueFormatter {
    String format(double value);
  }

  private class KnobPanel extends JPanel {
    private double value;
    private final String name;
    private final java.util.List<ChangeListener> listeners = new java.util.ArrayList<>();
    private int dragStartY;
    private final JLabel valLabel;
    private final ValueFormatter formatter;

    KnobPanel(String name, double initialValue, ValueFormatter formatter) {
      this.name = name;
      this.value = initialValue;
      this.formatter = formatter;
      Theme theme = Theme.getInstance();
      setBackground(theme.BG_DARK);
      setLayout(new BorderLayout());

      JLabel nameLabel = new JLabel(name, SwingConstants.CENTER);
      nameLabel.setForeground(theme.TEXT_DIM);
      nameLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
      add(nameLabel, BorderLayout.NORTH);

      JPanel knobCanvas =
          new JPanel() {
            @Override
            protected void paintComponent(Graphics g) {
              super.paintComponent(g);
              Graphics2D g2 = (Graphics2D) g.create();
              g2.setRenderingHint(
                  RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
              int sz = Math.min(getWidth(), getHeight()) - 4;
              int kx = (getWidth() - sz) / 2;
              int ky = (getHeight() - sz) / 2;

              // Background arc
              g2.setColor(new Color(0x3A3A3A));
              g2.setStroke(new BasicStroke(3.0f, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND));
              g2.drawArc(kx, ky, sz, sz, 225, -270);

              // Value arc (cyan)
              int arcAngle = (int) (-270 * value);
              g2.setColor(new Color(0x00D4FF));
              g2.drawArc(kx, ky, sz, sz, 225, arcAngle);

              // Indicator dot
              g2.setColor(new Color(0xEEEEEE));
              double angle = Math.toRadians(225 - 270 * value);
              int cx = kx + sz / 2 + (int) ((sz / 2 - 2) * Math.cos(angle));
              int cy = ky + sz / 2 - (int) ((sz / 2 - 2) * Math.sin(angle));
              g2.fillOval(cx - 2, cy - 2, 5, 5);

              g2.dispose();
            }
          };
      knobCanvas.setOpaque(false);
      knobCanvas.setPreferredSize(new Dimension(theme.scale(28), theme.scale(28)));
      knobCanvas.setCursor(Cursor.getPredefinedCursor(Cursor.N_RESIZE_CURSOR));
      knobCanvas.addMouseListener(
          new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
              dragStartY = e.getY();
            }
          });
      knobCanvas.addMouseMotionListener(
          new MouseMotionAdapter() {
            @Override
            public void mouseDragged(MouseEvent e) {
              int dy = dragStartY - e.getY();
              dragStartY = e.getY();
              value = Math.max(0.0, Math.min(1.0, value + dy * 0.005));
              valLabel.setText(formatter.format(value));
              knobCanvas.repaint();
              for (ChangeListener l : listeners) l.stateChanged(new ChangeEvent(KnobPanel.this));
            }
          });
      add(knobCanvas, BorderLayout.CENTER);

      valLabel = new JLabel(formatter.format(initialValue), SwingConstants.CENTER);
      valLabel.setForeground(theme.TEXT_LIGHT);
      valLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(7.0f)));
      add(valLabel, BorderLayout.SOUTH);
    }

    double getValue() {
      return value;
    }

    void setValue(double v) {
      this.value = v;
      valLabel.setText(formatter.format(v));
      repaint();
    }

    void addChangeListener(ChangeListener l) {
      listeners.add(l);
    }
  }

  // ─── Utility ────────────────────────────────────────────────────

  private static float normToFreq(double norm, float minHz, float maxHz) {
    return (float) (minHz * Math.pow(maxHz / minHz, norm));
  }

  // ─── Backend communication ──────────────────────────────────────
}
