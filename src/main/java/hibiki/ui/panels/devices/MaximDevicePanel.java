package hibiki.ui.panels.devices;

import hibiki.ui.PluginPane;
import hibiki.ui.Theme;
import hibiki.ui.panels.KnobPanel;
import java.awt.*;
import java.awt.event.*;
import java.awt.geom.*;
import javax.swing.*;

/**
 * Premium 3-band mastering limiter (Maxim) device panel. Features: - Tabbed bands (LOW, MID, HIGH,
 * MASTER, CROSSOVERS) - Interactive compression transfer curve rendering (with active bouncing
 * level dot) - Crossover frequency response visualizer (LR4 LP/HP splits plotted in log frequency
 * space) - Pro-grade multiband real-time metering (Low, Mid, High, Master input, output, and GR) -
 * Elegant dark theme design with band-specific color coding
 */
public class MaximDevicePanel extends AbstractDevicePanel {

  // Global / Crossover params
  private static final int PARAM_LOW_XOVER = 0;
  private static final int PARAM_HIGH_XOVER = 1;
  private static final int PARAM_LOOKAHEAD = 2;
  private static final int PARAM_ENABLE = 3;

  private static final int TOTAL_PARAMS = 44;

  // Band index: 0=Low, 1=Mid, 2=High, 3=Master
  private int selectedBand = 3; // Default to Master tab
  private boolean isCrossoversTab = false;

  // Band Colors
  private static final Color COLOR_LOW = new Color(0xE25822);
  private static final Color COLOR_MID = new Color(0x10B981);
  private static final Color COLOR_HIGH = new Color(0x3B82F6);
  private static final Color COLOR_MASTER = new Color(0xF59E0B);
  private static final Color COLOR_XOVER = new Color(0x8B5CF6);

  private boolean enabled = true;

  // Real-time metering values for all bands
  private float[] inputLevels = new float[4]; // Low, Mid, High, Master
  private float[] outputLevels = new float[4];
  private float[] grLevels = new float[4];

  private final VisualizerPanel visualizerPanel;
  private final MultibandMeterPanel meterPanel;
  private final JPanel knobContainer;

  private final JToggleButton[] tabButtons = new JToggleButton[5];
  private final KnobPanel[] knobs = new KnobPanel[10];

  public MaximDevicePanel(int trackIndex, int pluginIndex) {
    super(trackIndex, pluginIndex, TOTAL_PARAMS);

    // Instantiate final fields first so they can be captured safely in callbacks/lambdas
    visualizerPanel = new VisualizerPanel();
    meterPanel = new MultibandMeterPanel();
    knobContainer = new JPanel(new CardLayout());

    // Initial Defaults
    params[PARAM_LOW_XOVER] = 0.461; // ~200Hz
    params[PARAM_HIGH_XOVER] = 0.436; // ~2000Hz
    params[PARAM_LOOKAHEAD] = 0.2; // ~2.0ms
    params[PARAM_ENABLE] = 1.0;

    // Set defaults for the 4 bands (Low, Mid, High, Master)
    for (int b = 0; b < 4; b++) {
      int offset = 4 + b * 10;
      params[offset + 0] = 0.5; // Pre-Gain (0 dB)
      params[offset + 1] = 0.5; // Post-Gain (0 dB)
      params[offset + 2] = 0.0; // Sat Amount (0)
      params[offset + 3] = 1.0; // Sat Thresh (0 dB)
      params[offset + 4] = 1.0; // Thresh (0 dB)
      params[offset + 5] = 0.0; // Ratio (1:1)
      params[offset + 6] = 0.0; // Knee (0 dB)
      params[offset + 7] = 0.3; // Attack (10 ms)
      params[offset + 8] = 0.3; // Release (100 ms)
      params[offset + 9] =
          (b == 3) ? 0.975 : 1.0; // Ceiling (Master defaults to ~ -0.3 dB, others 0 dB)
    }

    Theme theme = Theme.getInstance();
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(theme.scale(420), theme.scale(260)));
    setMaximumSize(new Dimension(theme.scale(420), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Header
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x2A2D32));
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));

    JLabel nameLabel = new JLabel("Maxim");
    nameLabel.setForeground(Color.WHITE);
    nameLabel.setFont(theme.FONT_UI_BOLD);
    header.add(nameLabel, BorderLayout.WEST);

    // Custom Tab Buttons
    JPanel tabsPanel = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(3), 0));
    tabsPanel.setOpaque(false);
    ButtonGroup tabGroup = new ButtonGroup();
    String[] tabNames = {"LOW", "MID", "HIGH", "MASTER", "SPLITS"};
    for (int i = 0; i < 5; i++) {
      final int idx = i;
      JToggleButton tabBtn = new JToggleButton(tabNames[i]);
      tabBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
      tabBtn.setFocusPainted(false);
      tabBtn.setPreferredSize(new Dimension(theme.scale(54), theme.scale(18)));
      tabBtn.setBackground(theme.BG_DARK);
      tabBtn.setForeground(new Color(0xBBBBBB));
      tabBtn.setBorder(BorderFactory.createLineBorder(theme.BORDER));
      tabBtn.setSelected(i == 3); // Default to MASTER selected
      tabBtn.addActionListener(
          e -> {
            isCrossoversTab = (idx == 4);
            if (!isCrossoversTab) {
              selectedBand = idx;
            }
            updateActiveTabStyles();
            rebuildKnobs();
            visualizerPanel.repaint();
          });
      tabButtons[i] = tabBtn;
      tabGroup.add(tabBtn);
      tabsPanel.add(tabBtn);
    }
    header.add(tabsPanel, BorderLayout.CENTER);

    // Header Actions (On/SC/Remove)
    JPanel btnPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT, 2, 0));
    btnPanel.setOpaque(false);

    JButton scBtn = new JButton("SC");
    scBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
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
          visualizerPanel.repaint();
        });
    btnPanel.add(enableBtn);

    JButton delBtn = new JButton("\u274C");
    delBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    delBtn.addActionListener(e -> sendRemove());
    btnPanel.add(delBtn);

    header.add(btnPanel, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // Center Panel: Visualizer (Left) + Multiband Meter (Right)
    JPanel centerPanel = new JPanel(new BorderLayout());
    centerPanel.setBackground(theme.BG_DARKER);

    centerPanel.add(visualizerPanel, BorderLayout.CENTER);

    meterPanel.setPreferredSize(new Dimension(theme.scale(90), 0));
    centerPanel.add(meterPanel, BorderLayout.EAST);

    add(centerPanel, BorderLayout.CENTER);

    // Bottom Panel: Parameter Knobs Container
    knobContainer.setBackground(theme.BG_DARK);
    knobContainer.setPreferredSize(new Dimension(0, theme.scale(105)));
    knobContainer.setBorder(
        BorderFactory.createEmptyBorder(
            theme.scale(4), theme.scale(6), theme.scale(4), theme.scale(6)));
    add(knobContainer, BorderLayout.SOUTH);

    // Initial styles and knobs setup
    updateActiveTabStyles();
    rebuildKnobs();
  }

  private void updateActiveTabStyles() {
    Theme theme = Theme.getInstance();
    for (int i = 0; i < 5; i++) {
      if (tabButtons[i].isSelected()) {
        tabButtons[i].setBackground(getTabColor(i));
        tabButtons[i].setForeground(Color.BLACK);
      } else {
        tabButtons[i].setBackground(theme.BG_DARK);
        tabButtons[i].setForeground(new Color(0xBBBBBB));
      }
    }
  }

  private Color getTabColor(int tabIdx) {
    switch (tabIdx) {
      case 0:
        return COLOR_LOW;
      case 1:
        return COLOR_MID;
      case 2:
        return COLOR_HIGH;
      case 3:
        return COLOR_MASTER;
      case 4:
        return COLOR_XOVER;
      default:
        return COLOR_MASTER;
    }
  }

  private Color getActiveColor() {
    if (isCrossoversTab) return COLOR_XOVER;
    return getTabColor(selectedBand);
  }

  private void rebuildKnobs() {
    knobContainer.removeAll();
    Theme theme = Theme.getInstance();
    Color accent = getActiveColor();

    if (isCrossoversTab) {
      // Splits/Lookahead: 3 knobs centered
      JPanel grid = new JPanel(new FlowLayout(FlowLayout.CENTER, theme.scale(15), theme.scale(8)));
      grid.setOpaque(false);

      int[] pids = {PARAM_LOW_XOVER, PARAM_HIGH_XOVER, PARAM_LOOKAHEAD};
      String[] names = {"Low-Mid", "Mid-High", "Lookahead"};

      for (int i = 0; i < 3; i++) {
        final int pid = pids[i];
        KnobPanel k = new KnobPanel(names[i], params[pid], makeFormatter(pid), accent);
        k.addChangeListener(
            e -> {
              if (updatingFromBackend) return;
              params[pid] = k.getValue();
              sendParam(pid, params[pid]);
              visualizerPanel.repaint();
            });
        grid.add(k);
      }
      knobContainer.add(grid);
    } else {
      // Band Processing: 10 knobs in 2 rows of 5
      JPanel grid = new JPanel(new GridLayout(2, 5, theme.scale(2), theme.scale(2)));
      grid.setOpaque(false);

      int offset = 4 + selectedBand * 10;
      String[] paramNames = {
        "Pre-Gain", "Post-Gain", "Sat Amt", "Sat Thresh", "Ceiling",
        "Thresh", "Ratio", "Knee", "Attack", "Release"
      };
      // Parameter indices inside band (0 to 9)
      int[] pindices = {0, 1, 2, 3, 9, 4, 5, 6, 7, 8};

      for (int i = 0; i < 10; i++) {
        final int pidx = pindices[i];
        final int pid = offset + pidx;
        knobs[pidx] = new KnobPanel(paramNames[i], params[pid], makeFormatter(pid), accent);
        knobs[pidx].addChangeListener(
            e -> {
              if (updatingFromBackend) return;
              params[pid] = knobs[pidx].getValue();
              sendParam(pid, params[pid]);
              visualizerPanel.repaint();
            });
        grid.add(knobs[pidx]);
      }
      knobContainer.add(grid);
    }

    knobContainer.revalidate();
    knobContainer.repaint();
  }

  private ValueFormatter makeFormatter(int paramId) {
    return norm -> {
      if (paramId == PARAM_LOW_XOVER) {
        float f = 20.0f * (float) Math.pow(500.0f / 20.0f, norm);
        return String.format("%.0f Hz", f);
      }
      if (paramId == PARAM_HIGH_XOVER) {
        float f = 500.0f * (float) Math.pow(20000.0f / 500.0f, norm);
        return f >= 1000 ? String.format("%.1f kHz", f / 1000.0f) : String.format("%.0f Hz", f);
      }
      if (paramId == PARAM_LOOKAHEAD) {
        return String.format("%.1f ms", 0.1f * Math.pow(100.0f, norm));
      }

      // Band specific param formatters
      int p = (paramId - 4) % 10;
      if (p == 0 || p == 1) { // Pre/Post Gain
        return String.format("%.1f dB", norm * 48.0 - 24.0);
      }
      if (p == 2) { // Sat amount
        return String.format("%.0f%%", norm * 100.0);
      }
      if (p == 3) { // Sat threshold
        return String.format("%.1f dB", norm * 30.0 - 30.0);
      }
      if (p == 4) { // Thresh
        return String.format("%.1f dB", norm * 60.0 - 60.0);
      }
      if (p == 5) { // Ratio
        if (norm >= 0.999) return "\u221E:1";
        float r = 1.0f / (1.0f - (float) norm);
        return String.format("%.1f:1", r);
      }
      if (p == 6) { // Knee
        return String.format("%.1f dB", norm * 24.0);
      }
      if (p == 7) { // Attack
        return String.format("%.1f ms", 0.1f * Math.pow(1000.0f, norm));
      }
      if (p == 8) { // Release
        return String.format("%.0f ms", 10.0f * Math.pow(100.0f, norm));
      }
      if (p == 9) { // Ceiling
        return String.format("%.1f dB", norm * 12.0 - 12.0);
      }
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
      if (isCrossoversTab) {
        // If we are looking at crossover parameters
        rebuildKnobs();
      } else {
        int b = (paramId - 4) / 10;
        int p = (paramId - 4) % 10;
        if (b == selectedBand && knobs[p] != null) {
          knobs[p].setValue(value);
        }
      }
    }
    updatingFromBackend = false;
    visualizerPanel.repaint();
  }

  public void updateMetering(hibiki.pb.notifications.PluginMeteringData meter) {
    inputLevels[0] = meter.getLowInDb();
    outputLevels[0] = meter.getLowOutDb();
    grLevels[0] = meter.getLowGrDb();

    inputLevels[1] = meter.getMidInDb();
    outputLevels[1] = meter.getMidOutDb();
    grLevels[1] = meter.getMidGrDb();

    inputLevels[2] = meter.getHighInDb();
    outputLevels[2] = meter.getHighOutDb();
    grLevels[2] = meter.getHighGrDb();

    inputLevels[3] = meter.getInputDb(); // Master input
    outputLevels[3] = meter.getOutputDb(); // Master output
    grLevels[3] = meter.getGainReductionDb(); // Master GR

    visualizerPanel.repaint();
    meterPanel.repaint();
  }

  // ─── Visualizer Panel: Transfer Curve or Crossover Curve ───────

  private class VisualizerPanel extends JPanel {
    private static final float DB_MIN = -60, DB_MAX = 0;

    VisualizerPanel() {
      setBackground(Theme.getInstance().BG_DARKER);
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
      int w = getWidth(), h = getHeight();
      int pad = 4;
      int pw = w - pad * 2, ph = h - pad * 2;

      if (!enabled) {
        g2.setColor(new Color(255, 255, 255, 30));
        g2.setFont(Theme.getInstance().FONT_UI_BOLD.deriveFont(Theme.getInstance().scale(16.0f)));
        FontMetrics fm = g2.getFontMetrics();
        g2.drawString("BYPASS", (w - fm.stringWidth("BYPASS")) / 2, h / 2 + 5);
        g2.dispose();
        return;
      }

      if (isCrossoversTab) {
        paintCrossoverSplits(g2, pw, ph, pad);
      } else {
        paintTransferCurve(g2, pw, ph, pad);
      }

      g2.dispose();
    }

    // Paint the Transfer Curve for the selected band
    private void paintTransferCurve(Graphics2D g2, int pw, int ph, int pad) {
      // Unity line
      g2.setColor(new Color(255, 255, 255, 25));
      g2.setStroke(new BasicStroke(0.5f));
      g2.drawLine(pad, pad, pad + pw, pad + ph);

      // Grid Lines (-48, -36, -24, -12 dB)
      for (float db : new float[] {-48, -36, -24, -12}) {
        int x = dbToX(db, pw, pad);
        int y = dbToY(db, ph, pad);
        g2.setColor(new Color(255, 255, 255, 12));
        g2.drawLine(x, pad, x, pad + ph);
        g2.drawLine(pad, y, pad + pw, y);
      }

      // Read parameter values for the active band
      int offset = 4 + selectedBand * 10;
      float preGain = (float) (params[offset + 0] * 48.0 - 24.0);
      float postGain = (float) (params[offset + 1] * 48.0 - 24.0);
      float satAmount = (float) params[offset + 2];
      float satThresh = (float) (params[offset + 3] * 30.0 - 30.0);
      float thresh = (float) (params[offset + 4] * 60.0 - 60.0);
      float ratio =
          (params[offset + 5] >= 0.999) ? 1000.0f : 1.0f / (1.0f - (float) params[offset + 5]);
      float knee = (float) (params[offset + 6] * 24.0);
      float ceiling = (float) (params[offset + 9] * 12.0 - 12.0);

      Color color = getTabColor(selectedBand);
      g2.setColor(color);
      g2.setStroke(new BasicStroke(2.0f));

      GeneralPath path = new GeneralPath();
      boolean first = true;

      for (int px = 0; px <= pw; px++) {
        float inDb = DB_MIN + (float) px / pw * (DB_MAX - DB_MIN);

        // DSP sequence emulation:
        // 1. Pre-Gain
        float xDb = inDb + preGain;
        float xLin = (float) Math.pow(10.0, xDb / 20.0);

        // 2. Saturation
        float satLin = xLin;
        if (satAmount > 0.001f) {
          float satThreshLin = (float) Math.pow(10.0, satThresh / 20.0);
          if (xLin > satThreshLin) {
            float excess = xLin - satThreshLin;
            float clip =
                satThreshLin
                    + (1.0f - satThreshLin)
                        * (float) Math.tanh(excess / Math.max(1.0f - satThreshLin, 0.0001f));
            satLin = xLin * (1.0f - satAmount) + clip * satAmount;
          }
        }
        float satDb = (satLin > 1e-10f) ? 20.0f * (float) Math.log10(satLin) : -200.0f;

        // 3. Compressor
        float grDb = computeGainReduction(satDb, thresh, ratio, knee);
        float compDb = satDb + grDb;

        // 4. Limiter Ceiling
        float compLin = (float) Math.pow(10.0, compDb / 20.0);
        float ceilLin = (float) Math.pow(10.0, ceiling / 20.0);
        float limLin = Math.min(compLin, ceilLin);
        float limDb = (limLin > 1e-10f) ? 20.0f * (float) Math.log10(limLin) : -200.0f;

        // 5. Post-Gain
        float outDb = limDb + postGain;
        outDb = Math.max(DB_MIN, Math.min(DB_MAX, outDb));

        int x = pad + px;
        int y = dbToY(outDb, ph, pad);

        if (first) {
          path.moveTo(x, y);
          first = false;
        } else {
          path.lineTo(x, y);
        }
      }
      g2.draw(path);

      // Draw dashed Compressor Threshold line
      int threshX = dbToX(thresh, pw, pad);
      g2.setColor(new Color(color.getRed(), color.getGreen(), color.getBlue(), 80));
      g2.setStroke(
          new BasicStroke(
              1.0f, BasicStroke.CAP_BUTT, BasicStroke.JOIN_MITER, 10.0f, new float[] {3, 3}, 0));
      g2.drawLine(threshX, pad, threshX, pad + ph);

      // Draw dashed Ceiling line
      int ceilY = dbToY(ceiling, ph, pad);
      g2.drawLine(pad, ceilY, pad + pw, ceilY);

      // Labels
      g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(7.5f)));
      g2.setColor(new Color(255, 255, 255, 100));
      g2.drawString(String.format("Thr: %.1fdB", thresh), threshX + 4, pad + 12);
      g2.drawString(String.format("Ceil: %.1fdB", ceiling), pad + 4, ceilY - 4);

      // Draw bouncing real-time dot
      float inLevel = inputLevels[selectedBand];
      float outLevel = outputLevels[selectedBand];
      if (inLevel > -100 && outLevel > -100) {
        float dotIn = Math.max(DB_MIN, Math.min(DB_MAX, inLevel));
        float dotOut = Math.max(DB_MIN, Math.min(DB_MAX, outLevel));
        int dx = dbToX(dotIn, pw, pad);
        int dy = dbToY(dotOut, ph, pad);

        // Glow ring
        g2.setColor(new Color(color.getRed(), color.getGreen(), color.getBlue(), 40));
        g2.fillOval(dx - 8, dy - 8, 16, 16);
        g2.setColor(new Color(color.getRed(), color.getGreen(), color.getBlue(), 120));
        g2.fillOval(dx - 4, dy - 4, 8, 8);
        // White core
        g2.setColor(Color.WHITE);
        g2.fillOval(dx - 2, dy - 2, 4, 4);
      }
    }

    private float computeGainReduction(float inputDb, float threshold, float ratio, float kneeDb) {
      if (inputDb <= -100.0f) return 0.0f;
      float halfKnee = kneeDb / 2.0f;
      float grDb = 0.0f;

      if (kneeDb <= 0.01f) {
        if (inputDb > threshold) {
          grDb = (threshold - inputDb) * (1.0f - 1.0f / ratio);
        }
      } else {
        float lower = threshold - halfKnee;
        float upper = threshold + halfKnee;
        if (inputDb >= upper) {
          grDb = (threshold - inputDb) * (1.0f - 1.0f / ratio);
        } else if (inputDb > lower) {
          float x = inputDb - lower;
          grDb = -(1.0f - 1.0f / ratio) * x * x / (2.0f * kneeDb);
        }
      }
      return grDb;
    }

    // Paint Crossover Frequency Response Plot (splits)
    private void paintCrossoverSplits(Graphics2D g2, int pw, int ph, int pad) {
      float lowFreq = 20.0f * (float) Math.pow(500.0f / 20.0f, params[PARAM_LOW_XOVER]);
      float highFreq = 500.0f * (float) Math.pow(20000.0f / 500.0f, params[PARAM_HIGH_XOVER]);

      // Grid (vertical lines for log-spaced frequencies: 100Hz, 1kHz, 10kHz)
      float[] gridFreqs = {100.0f, 1000.0f, 10000.0f};
      g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(7.0f)));
      for (float f : gridFreqs) {
        int x = freqToX(f, pw, pad);
        g2.setColor(new Color(255, 255, 255, 12));
        g2.drawLine(x, pad, x, pad + ph);
        g2.setColor(new Color(255, 255, 255, 60));
        String fstr =
            f >= 1000 ? String.format("%.0fkHz", f / 1000.0f) : String.format("%.0fHz", f);
        g2.drawString(fstr, x + 3, pad + ph - 4);
      }

      // Plot curves for Low, Mid, High band LR4 response
      GeneralPath lowPath = new GeneralPath();
      GeneralPath midPath = new GeneralPath();
      GeneralPath highPath = new GeneralPath();

      boolean first = true;
      for (int px = 0; px <= pw; px++) {
        float f = xToFreq(px, pw);

        // LR4 amplitude response:
        // LP: 1 / (1 + (f/fc)^4)
        // HP: 1 / (1 + (fc/f)^4)
        double lowLP = 1.0 / (1.0 + Math.pow(f / lowFreq, 4.0));
        double midHP = 1.0 / (1.0 + Math.pow(lowFreq / f, 4.0));
        double midLP = 1.0 / (1.0 + Math.pow(f / highFreq, 4.0));
        double highHP = 1.0 / (1.0 + Math.pow(highFreq / f, 4.0));

        // Decibel values
        double lowDb = 10.0 * Math.log10(lowLP);
        double midDb = 10.0 * Math.log10(midHP * midLP);
        double highDb = 10.0 * Math.log10(highHP);

        // Clamp DB for graphics
        float lY = freqDbToY((float) lowDb, ph, pad);
        float mY = freqDbToY((float) midDb, ph, pad);
        float hY = freqDbToY((float) highDb, ph, pad);

        int x = pad + px;
        if (first) {
          lowPath.moveTo(x, lY);
          midPath.moveTo(x, mY);
          highPath.moveTo(x, hY);
          first = false;
        } else {
          lowPath.lineTo(x, lY);
          midPath.lineTo(x, mY);
          highPath.lineTo(x, hY);
        }
      }

      // Draw fills with gradient
      g2.setStroke(new BasicStroke(1.5f));

      // Low split fill
      g2.setColor(new Color(COLOR_LOW.getRed(), COLOR_LOW.getGreen(), COLOR_LOW.getBlue(), 35));
      GeneralPath lowFill = (GeneralPath) lowPath.clone();
      lowFill.lineTo(pad + pw, pad + ph);
      lowFill.lineTo(pad, pad + ph);
      lowFill.closePath();
      g2.fill(lowFill);
      g2.setColor(COLOR_LOW);
      g2.draw(lowPath);

      // Mid split fill
      g2.setColor(new Color(COLOR_MID.getRed(), COLOR_MID.getGreen(), COLOR_MID.getBlue(), 35));
      GeneralPath midFill = (GeneralPath) midPath.clone();
      midFill.lineTo(pad + pw, pad + ph);
      midFill.lineTo(pad, pad + ph);
      midFill.closePath();
      g2.fill(midFill);
      g2.setColor(COLOR_MID);
      g2.draw(midPath);

      // High split fill
      g2.setColor(new Color(COLOR_HIGH.getRed(), COLOR_HIGH.getGreen(), COLOR_HIGH.getBlue(), 35));
      GeneralPath highFill = (GeneralPath) highPath.clone();
      highFill.lineTo(pad + pw, pad + ph);
      highFill.lineTo(pad, pad + ph);
      highFill.closePath();
      g2.fill(highFill);
      g2.setColor(COLOR_HIGH);
      g2.draw(highPath);

      // Draw Split lines
      g2.setColor(COLOR_XOVER);
      g2.setStroke(
          new BasicStroke(
              1.0f, BasicStroke.CAP_BUTT, BasicStroke.JOIN_MITER, 10.0f, new float[] {2, 2}, 0));
      int lx = freqToX(lowFreq, pw, pad);
      int hx = freqToX(highFreq, pw, pad);
      g2.drawLine(lx, pad, lx, pad + ph);
      g2.drawLine(hx, pad, hx, pad + ph);

      g2.setColor(Color.WHITE);
      g2.drawString(String.format("%.0fHz", lowFreq), lx + 4, pad + 15);
      g2.drawString(
          highFreq >= 1000
              ? String.format("%.1fkHz", highFreq / 1000.0f)
              : String.format("%.0fHz", highFreq),
          hx + 4,
          pad + 15);
    }

    private int dbToX(float db, int w, int pad) {
      return pad + (int) ((db - DB_MIN) / (DB_MAX - DB_MIN) * w);
    }

    private int dbToY(float db, int h, int pad) {
      return pad + h - (int) ((db - DB_MIN) / (DB_MAX - DB_MIN) * h);
    }

    private int freqToX(float freq, int w, int pad) {
      double minLog = Math.log(20.0);
      double maxLog = Math.log(20000.0);
      double fLog = Math.log(Math.max(20.0, Math.min(20000.0, freq)));
      return pad + (int) ((fLog - minLog) / (maxLog - minLog) * w);
    }

    private float xToFreq(int px, int w) {
      double minLog = Math.log(20.0);
      double maxLog = Math.log(20000.0);
      double logVal = minLog + (double) px / w * (maxLog - minLog);
      return (float) Math.exp(logVal);
    }

    private float freqDbToY(float db, int h, int pad) {
      // Show response down to -36dB
      float val = Math.max(-36.0f, Math.min(0.0f, db));
      return pad + h - (int) ((val + 36.0f) / 36.0f * (h - 10));
    }
  }

  // ─── Multiband Meter Panel (Right side) ──────────────────────────

  private class MultibandMeterPanel extends JPanel {
    MultibandMeterPanel() {
      setBackground(Theme.getInstance().BG_DARKER);
      setBorder(BorderFactory.createMatteBorder(0, 1, 0, 0, Theme.getInstance().BORDER));
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      int w = getWidth(), h = getHeight();
      Theme theme = Theme.getInstance();

      int pad = theme.scale(4);
      int bandW = (w - pad * 2) / 4;
      int meterH = h - theme.scale(20);

      String[] labels = {"L", "M", "H", "MST"};
      Color[] colors = {COLOR_LOW, COLOR_MID, COLOR_HIGH, COLOR_MASTER};

      g2.setFont(theme.FONT_UI.deriveFont(theme.scale(7.0f)));

      for (int i = 0; i < 4; i++) {
        int bx = pad + i * bandW;

        // Draw background channel strip slot
        g2.setColor(new Color(0x1B1D20));
        g2.fillRect(bx + 1, pad, bandW - 2, meterH);

        // Input Level
        float inNorm = Math.min(1, Math.max(0, (inputLevels[i] + 60.0f) / 60.0f));
        int inH = (int) (inNorm * meterH);
        g2.setColor(new Color(0x2E7D32)); // Dark Green for input
        g2.fillRect(bx + 2, pad + meterH - inH, (bandW - 4) / 2, inH);

        // Output Level
        float outNorm = Math.min(1, Math.max(0, (outputLevels[i] + 60.0f) / 60.0f));
        int outH = (int) (outNorm * meterH);
        g2.setColor(colors[i]); // Band accent for output
        g2.fillRect(bx + 2 + (bandW - 4) / 2, pad + meterH - outH, (bandW - 4) / 2, outH);

        // Gain Reduction Overlay (descending orange/red bar)
        float grNorm = Math.min(1, Math.abs(grLevels[i]) / 30.0f);
        int grH = (int) (grNorm * meterH);
        if (grH > 0) {
          g2.setColor(new Color(0xE74C3C)); // Red for GR
          g2.fillRect(bx + 2, pad, bandW - 4, grH);
        }

        // Band Label
        g2.setColor(tabButtons[i].isSelected() ? Color.WHITE : new Color(0x7F8C8D));
        if (i == 3 && tabButtons[3].isSelected()) {
          g2.setColor(COLOR_MASTER);
        } else if (i < 3 && tabButtons[i].isSelected()) {
          g2.setColor(colors[i]);
        }
        FontMetrics fm = g2.getFontMetrics();
        g2.drawString(labels[i], bx + (bandW - fm.stringWidth(labels[i])) / 2, h - pad);
      }

      g2.dispose();
    }
  }
}
