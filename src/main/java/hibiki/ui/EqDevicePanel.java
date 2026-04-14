package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import java.awt.*;
import java.awt.event.*;
import java.awt.geom.*;
import java.util.List;
import javax.swing.*;

/**
 * Ableton Live-style EQ Eight device panel. Shows an interactive frequency response curve with
 * draggable band handles, shift-drag Q control, double-click to create bands, and real-time FFT
 * spectrum overlay.
 */
public class EqDevicePanel extends JPanel {
  private static final int NUM_BANDS = 8;
  private static final int PARAMS_PER_BAND = 4; // type, freq, gain, q

  // Filter type constants (matching C++ BuiltinEq::FilterType)
  private static final int TYPE_OFF = 0;
  private static final int TYPE_LPF = 1;
  private static final int TYPE_HPF = 2;
  private static final int TYPE_LOW_SHELF = 3;
  private static final int TYPE_HIGH_SHELF = 4;
  private static final int TYPE_BELL = 5;

  private static final String[] TYPE_NAMES = {"Off", "LP", "HP", "LS", "HS", "Bell"};
  // Normalized values for each type (matching C++ thresholds)
  private static final double[] TYPE_NORM = {0.0, 0.2, 0.4, 0.6, 0.8, 1.0};

  // Band colors (Ableton-style rainbow)
  private static final Color[] BAND_COLORS = {
    new Color(0xE04040), new Color(0xE07020), new Color(0xD0C020),
    new Color(0x40B040), new Color(0x30A0A0), new Color(0x4080E0),
    new Color(0x8060D0), new Color(0xC050A0),
  };

  private final int trackIndex;
  private final int pluginIndex;
  private final double[] params = new double[NUM_BANDS * PARAMS_PER_BAND + 1];
  private boolean enabled = true;
  private final CurvePanel curvePanel;
  private final JPanel bandControlsPanel;
  private final JComboBox<String>[] typeDropdowns;
  private boolean updatingFromBackend = false;

  /** Callback invoked when user clicks Mod button; set by PluginPane wrapper. */
  public Runnable modToggleCallback;

  // FFT spectrum data (64 bins, log-spaced 20Hz-20kHz)
  private float[] spectrumInputDb = null;
  private float[] spectrumOutputDb = null;

  @SuppressWarnings("unchecked")
  public EqDevicePanel(int trackIndex, int pluginIndex) {
    this.trackIndex = trackIndex;
    this.pluginIndex = pluginIndex;

    // Initialize default params
    double[] defaultFreqs = {30, 80, 250, 700, 2000, 5000, 10000, 16000};
    for (int b = 0; b < NUM_BANDS; b++) {
      params[b] = 0.0; // OFF
      params[b + NUM_BANDS] = freqToNorm(defaultFreqs[b]);
      params[b + NUM_BANDS * 2] = 0.5; // 0 dB
      params[b + NUM_BANDS * 3] = qToNorm(0.707);
    }
    params[NUM_BANDS * PARAMS_PER_BAND] = 1.0; // enabled

    setLayout(new BorderLayout());
    Theme theme = Theme.getInstance();
    setPreferredSize(new Dimension(theme.scale(360), theme.scale(230)));
    setMaximumSize(new Dimension(theme.scale(360), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Header
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x2D5AA0));
    header.setBorder(BorderFactory.createEmptyBorder(3, 6, 3, 6));

    JLabel nameLabel = new JLabel("EQ Eight");
    nameLabel.setForeground(Color.WHITE);
    nameLabel.setFont(theme.FONT_UI_BOLD);
    header.add(nameLabel, BorderLayout.CENTER);

    // Frequency response curve (initialized before button listener references it)
    curvePanel = new CurvePanel();

    JPanel btnPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT, 2, 0));
    btnPanel.setOpaque(false);

    JButton modBtn = new JButton("Mod");
    modBtn.addActionListener(
        e -> {
          if (modToggleCallback != null) modToggleCallback.run();
        });
    btnPanel.add(modBtn);

    JToggleButton enableBtn = new JToggleButton("On", enabled);
    enableBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    enableBtn.setFocusPainted(false);
    enableBtn.addActionListener(
        e -> {
          enabled = enableBtn.isSelected();
          sendParam(NUM_BANDS * PARAMS_PER_BAND, enabled ? 1.0 : 0.0);
          curvePanel.repaint();
        });
    btnPanel.add(enableBtn);

    JButton delBtn = new JButton("\u274C");
    delBtn.addActionListener(e -> sendRemove());
    btnPanel.add(delBtn);
    header.add(btnPanel, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    add(curvePanel, BorderLayout.CENTER);

    // Band controls at bottom
    bandControlsPanel = new JPanel(new GridLayout(1, NUM_BANDS, 1, 0));
    bandControlsPanel.setBackground(theme.BG_DARK);
    bandControlsPanel.setPreferredSize(new Dimension(0, theme.scale(65)));

    typeDropdowns = new JComboBox[NUM_BANDS];
    for (int b = 0; b < NUM_BANDS; b++) {
      final int band = b;
      JPanel bp = new JPanel(new BorderLayout());
      bp.setBackground(theme.BG_DARK);
      bp.setBorder(BorderFactory.createMatteBorder(0, 0, 0, 1, theme.BORDER));

      // Color indicator dot + type dropdown
      JPanel topRow = new JPanel(new FlowLayout(FlowLayout.CENTER, 2, 2));
      topRow.setOpaque(false);

      JLabel dot = new JLabel("\u25CF");
      dot.setForeground(BAND_COLORS[b]);
      dot.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
      topRow.add(dot);

      JComboBox<String> typeBox = new JComboBox<>(TYPE_NAMES);
      typeBox.setFont(theme.FONT_UI.deriveFont(theme.scale(9.5f)));
      typeBox.setSelectedIndex(0);
      typeBox.addActionListener(
          e -> {
            if (updatingFromBackend) return;
            int sel = typeBox.getSelectedIndex();
            params[band] = TYPE_NORM[sel];
            sendParam(band, TYPE_NORM[sel]);
            curvePanel.repaint();
          });
      typeDropdowns[b] = typeBox;
      topRow.add(typeBox);
      bp.add(topRow, BorderLayout.NORTH);

      // Freq label
      JLabel freqLabel = new JLabel(formatFreq(defaultFreqs[b]), SwingConstants.CENTER);
      freqLabel.setForeground(theme.TEXT_DIM);
      freqLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
      bp.add(freqLabel, BorderLayout.CENTER);

      bandControlsPanel.add(bp);
    }
    add(bandControlsPanel, BorderLayout.SOUTH);
  }

  /** Update a parameter from backend notification. */
  public void updateParam(int paramId, float value) {
    if (paramId < 0 || paramId >= params.length) return;
    updatingFromBackend = true;
    params[paramId] = value;

    if (paramId == NUM_BANDS * PARAMS_PER_BAND) {
      enabled = value >= 0.5;
    } else if (paramId < NUM_BANDS) {
      // Type changed — update dropdown
      int typeIdx = normToTypeIndex(value);
      if (typeDropdowns[paramId] != null) {
        typeDropdowns[paramId].setSelectedIndex(typeIdx);
      }
    }
    updatingFromBackend = false;
    curvePanel.repaint();
  }

  /** Update FFT spectrum data from backend (called from PluginPane). */
  public void setSpectrumData(List<Float> inputMags, List<Float> outputMags) {
    if (inputMags != null && !inputMags.isEmpty()) {
      spectrumInputDb = new float[inputMags.size()];
      for (int i = 0; i < inputMags.size(); i++) spectrumInputDb[i] = inputMags.get(i);
    }
    if (outputMags != null && !outputMags.isEmpty()) {
      spectrumOutputDb = new float[outputMags.size()];
      for (int i = 0; i < outputMags.size(); i++) spectrumOutputDb[i] = outputMags.get(i);
    }
    curvePanel.repaint();
  }

  private int normToTypeIndex(double norm) {
    if (norm < 0.1) return 0;
    if (norm < 0.3) return 1;
    if (norm < 0.5) return 2;
    if (norm < 0.7) return 3;
    if (norm < 0.9) return 4;
    return 5;
  }

  // ─── Frequency response curve panel ─────────────────────────────────

  private class CurvePanel extends JPanel implements MouseListener, MouseMotionListener {
    private int dragBand = -1;
    private boolean shiftDrag = false;
    private int dragStartY = 0;
    private double dragStartQ = 0;
    private static final float MIN_FREQ = 20, MAX_FREQ = 20000;
    private static final float MIN_DB = -25, MAX_DB = 25;

    CurvePanel() {
      setBackground(Theme.getInstance().BG_DARKER);
      addMouseListener(this);
      addMouseMotionListener(this);
      setCursor(Cursor.getPredefinedCursor(Cursor.CROSSHAIR_CURSOR));
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
      int w = getWidth(), h = getHeight();

      // Grid lines
      g2.setColor(new Color(255, 255, 255, 20));
      g2.setStroke(new BasicStroke(0.5f));
      for (float freq : new float[] {100, 1000, 10000}) {
        int x = freqToX(freq, w);
        g2.drawLine(x, 0, x, h);
      }
      int y0 = dbToY(0, h);
      g2.drawLine(0, y0, w, y0);
      for (float db : new float[] {-12, 12}) {
        int y = dbToY(db, h);
        g2.setColor(new Color(255, 255, 255, 12));
        g2.drawLine(0, y, w, y);
      }

      // Grid labels
      g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(8.0f)));
      g2.setColor(new Color(255, 255, 255, 60));
      g2.drawString("100", freqToX(100, w) + 2, h - 3);
      g2.drawString("1k", freqToX(1000, w) + 2, h - 3);
      g2.drawString("10k", freqToX(10000, w) + 2, h - 3);

      // FFT spectrum overlay (behind EQ curves)
      drawSpectrumOverlay(g2, w, h);

      if (!enabled) {
        g2.setColor(new Color(255, 255, 255, 40));
        g2.setFont(Theme.getInstance().FONT_UI_BOLD);
        g2.drawString("BYPASSED", w / 2 - 30, h / 2);
        g2.dispose();
        return;
      }

      // Per-band curves (subtle)
      for (int b = 0; b < NUM_BANDS; b++) {
        if (normToTypeIndex(params[b]) == 0) continue;
        Color bc = BAND_COLORS[b];
        g2.setColor(new Color(bc.getRed(), bc.getGreen(), bc.getBlue(), 40));
        g2.setStroke(new BasicStroke(1.0f));
        drawBandCurve(g2, b, w, h);
      }

      // Composite curve
      g2.setColor(new Color(0xFFFFFF));
      g2.setStroke(new BasicStroke(1.5f));
      GeneralPath path = new GeneralPath();
      for (int px = 0; px < w; px++) {
        float freq = xToFreq(px, w);
        float db = getCompositeMagnitudeDb(freq);
        int y = dbToY(db, h);
        if (px == 0) path.moveTo(px, y);
        else path.lineTo(px, y);
      }
      g2.draw(path);

      // Fill under the curve
      GeneralPath fill = new GeneralPath(path);
      fill.lineTo(w, dbToY(0, h));
      fill.lineTo(0, dbToY(0, h));
      fill.closePath();
      g2.setColor(new Color(255, 255, 255, 15));
      g2.fill(fill);

      // Band handle dots
      for (int b = 0; b < NUM_BANDS; b++) {
        if (normToTypeIndex(params[b]) == 0) continue;
        float freq = normToFreq(params[b + NUM_BANDS]);
        float gain = (float) (params[b + NUM_BANDS * 2] - 0.5) * 48;
        int bx = freqToX(freq, w);
        int by = dbToY(gain, h);

        Color bc = BAND_COLORS[b];
        g2.setColor(bc);
        g2.setStroke(new BasicStroke(1.5f));
        g2.drawOval(bx - 6, by - 6, 12, 12);
        g2.setColor(new Color(bc.getRed(), bc.getGreen(), bc.getBlue(), dragBand == b ? 200 : 120));
        g2.fillOval(bx - 5, by - 5, 10, 10);

        // Show band number
        g2.setColor(Color.WHITE);
        g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(7.0f)));
        g2.drawString(String.valueOf(b + 1), bx - 3, by + 3);
      }

      g2.dispose();
    }

    private void drawSpectrumOverlay(Graphics2D g2, int w, int h) {
      float[] inDb = spectrumInputDb;
      float[] outDb = spectrumOutputDb;
      if (inDb == null && outDb == null) return;

      int numBins = 64;
      // Log-spaced bin center frequencies
      double logMin = Math.log(20.0);
      double logMax = Math.log(20000.0);

      // Input spectrum (blue)
      if (inDb != null && inDb.length >= numBins) {
        drawSpectrumCurve(g2, inDb, numBins, logMin, logMax, w, h, new Color(0x4080E0, true), 50);
      }
      // Output spectrum (yellow-orange)
      if (outDb != null && outDb.length >= numBins) {
        drawSpectrumCurve(g2, outDb, numBins, logMin, logMax, w, h, new Color(0xE0C040, true), 60);
      }
    }

    private void drawSpectrumCurve(
        Graphics2D g2,
        float[] bins,
        int numBins,
        double logMin,
        double logMax,
        int w,
        int h,
        Color color,
        int alpha) {
      GeneralPath path = new GeneralPath();
      boolean first = true;
      for (int i = 0; i < numBins; i++) {
        double logFreq = logMin + (logMax - logMin) * (i + 0.5) / numBins;
        float freq = (float) Math.exp(logFreq);
        int x = freqToX(freq, w);
        // Map dB to Y: spectrum range -80 to 0 dB mapped to panel height
        float db = Math.max(-80, Math.min(0, bins[i]));
        float normalized = (db + 80) / 80.0f; // 0..1
        int y = h - (int) (normalized * h * 0.8f) - (int) (h * 0.05f);
        if (first) {
          path.moveTo(x, y);
          first = false;
        } else path.lineTo(x, y);
      }

      // Fill under curve
      GeneralPath fill = new GeneralPath(path);
      fill.lineTo(w, h);
      fill.lineTo(0, h);
      fill.closePath();
      g2.setColor(new Color(color.getRed(), color.getGreen(), color.getBlue(), alpha / 3));
      g2.fill(fill);

      // Stroke
      g2.setColor(new Color(color.getRed(), color.getGreen(), color.getBlue(), alpha));
      g2.setStroke(new BasicStroke(1.0f));
      g2.draw(path);
    }

    private void drawBandCurve(Graphics2D g2, int band, int w, int h) {
      GeneralPath path = new GeneralPath();
      for (int px = 0; px < w; px += 2) {
        float freq = xToFreq(px, w);
        float db = getBandMagnitudeDb(band, freq);
        int y = dbToY(db, h);
        if (px == 0) path.moveTo(px, y);
        else path.lineTo(px, y);
      }
      g2.draw(path);
    }

    // ─── Mouse interaction ─────────────────────────────────────────

    @Override
    public void mousePressed(MouseEvent e) {
      int w = getWidth(), h = getHeight();
      dragBand = -1;
      shiftDrag = false;
      for (int b = 0; b < NUM_BANDS; b++) {
        if (normToTypeIndex(params[b]) == 0) continue;
        float freq = normToFreq(params[b + NUM_BANDS]);
        float gain = (float) (params[b + NUM_BANDS * 2] - 0.5) * 48;
        int bx = freqToX(freq, w);
        int by = dbToY(gain, h);
        if (Math.abs(e.getX() - bx) < 10 && Math.abs(e.getY() - by) < 10) {
          dragBand = b;
          if (e.isShiftDown()) {
            shiftDrag = true;
            dragStartY = e.getY();
            dragStartQ = params[b + NUM_BANDS * 3];
          }
          break;
        }
      }
    }

    @Override
    public void mouseDragged(MouseEvent e) {
      if (dragBand < 0) return;
      int w = getWidth(), h = getHeight();

      if (shiftDrag) {
        // Shift+drag: vertical movement adjusts Q
        // Moving up = narrower Q, moving down = wider Q
        int dy = dragStartY - e.getY();
        double qDelta = dy * 0.005; // sensitivity
        double newQ = Math.max(0, Math.min(1, dragStartQ + qDelta));
        params[dragBand + NUM_BANDS * 3] = newQ;
        sendParam(dragBand + NUM_BANDS * 3, newQ);
      } else {
        // Normal drag: move freq and gain
        float freq = xToFreq(Math.max(0, Math.min(w, e.getX())), w);
        float db = yToDb(Math.max(0, Math.min(h, e.getY())), h);

        double freqNorm = freqToNorm(freq);
        double gainNorm = (db / 48.0) + 0.5;
        freqNorm = Math.max(0, Math.min(1, freqNorm));
        gainNorm = Math.max(0, Math.min(1, gainNorm));

        params[dragBand + NUM_BANDS] = freqNorm;
        params[dragBand + NUM_BANDS * 2] = gainNorm;
        sendParam(dragBand + NUM_BANDS, freqNorm);
        sendParam(dragBand + NUM_BANDS * 2, gainNorm);
      }
      repaint();
    }

    @Override
    public void mouseReleased(MouseEvent e) {
      dragBand = -1;
      shiftDrag = false;
    }

    @Override
    public void mouseClicked(MouseEvent e) {
      if (e.getClickCount() == 2) {
        // Double-click: activate the nearest OFF band as Bell at click position
        int w = getWidth(), h = getHeight();
        float freq = xToFreq(e.getX(), w);
        float db = yToDb(e.getY(), h);

        // Find first OFF band
        int offBand = -1;
        for (int b = 0; b < NUM_BANDS; b++) {
          if (normToTypeIndex(params[b]) == 0) {
            offBand = b;
            break;
          }
        }
        if (offBand < 0) return; // all bands active

        // Set to Bell type at clicked position
        double freqNorm = freqToNorm(freq);
        double gainNorm = (db / 48.0) + 0.5;
        freqNorm = Math.max(0, Math.min(1, freqNorm));
        gainNorm = Math.max(0, Math.min(1, gainNorm));

        params[offBand] = TYPE_NORM[TYPE_BELL];
        params[offBand + NUM_BANDS] = freqNorm;
        params[offBand + NUM_BANDS * 2] = gainNorm;
        params[offBand + NUM_BANDS * 3] = qToNorm(0.707); // default Q

        sendParam(offBand, TYPE_NORM[TYPE_BELL]);
        sendParam(offBand + NUM_BANDS, freqNorm);
        sendParam(offBand + NUM_BANDS * 2, gainNorm);
        sendParam(offBand + NUM_BANDS * 3, qToNorm(0.707));

        // Update the type dropdown
        updatingFromBackend = true;
        if (typeDropdowns[offBand] != null) {
          typeDropdowns[offBand].setSelectedIndex(TYPE_BELL);
        }
        updatingFromBackend = false;
        repaint();
      }
    }

    @Override
    public void mouseEntered(MouseEvent e) {}

    @Override
    public void mouseExited(MouseEvent e) {}

    @Override
    public void mouseMoved(MouseEvent e) {}

    // ─── Coordinate transforms ─────────────────────────────────────

    private int freqToX(float freq, int w) {
      double log = Math.log(freq / MIN_FREQ) / Math.log(MAX_FREQ / MIN_FREQ);
      return (int) (log * w);
    }

    private float xToFreq(int x, int w) {
      double norm = (double) x / w;
      return (float) (MIN_FREQ * Math.pow(MAX_FREQ / MIN_FREQ, norm));
    }

    private int dbToY(float db, int h) {
      return (int) ((1.0 - (db - MIN_DB) / (MAX_DB - MIN_DB)) * h);
    }

    private float yToDb(int y, int h) {
      return (float) (MAX_DB - (double) y / h * (MAX_DB - MIN_DB));
    }
  }

  // ─── DSP: Biquad magnitude calculation (mirrors C++ for UI) ─────

  private float getCompositeMagnitudeDb(float freq) {
    float totalDb = 0;
    for (int b = 0; b < NUM_BANDS; b++) {
      totalDb += getBandMagnitudeDb(b, freq);
    }
    return totalDb;
  }

  private float getBandMagnitudeDb(int band, float freq) {
    int typeIdx = normToTypeIndex(params[band]);
    if (typeIdx == TYPE_OFF) return 0;

    float bFreq = normToFreq(params[band + NUM_BANDS]);
    float bGain = (float) (params[band + NUM_BANDS * 2] - 0.5) * 48;
    float bQ = normToQ(params[band + NUM_BANDS * 3]);
    float sampleRate = 44100;

    double w0 = 2 * Math.PI * bFreq / sampleRate;
    double cosW0 = Math.cos(w0);
    double sinW0 = Math.sin(w0);
    double alpha = sinW0 / (2.0 * bQ);
    double A = Math.pow(10.0, bGain / 40.0);

    double b0, b1, b2, a0, a1, a2;
    switch (typeIdx) {
      case TYPE_LPF:
        b0 = (1 - cosW0) / 2;
        b1 = 1 - cosW0;
        b2 = (1 - cosW0) / 2;
        a0 = 1 + alpha;
        a1 = -2 * cosW0;
        a2 = 1 - alpha;
        break;
      case TYPE_HPF:
        b0 = (1 + cosW0) / 2;
        b1 = -(1 + cosW0);
        b2 = (1 + cosW0) / 2;
        a0 = 1 + alpha;
        a1 = -2 * cosW0;
        a2 = 1 - alpha;
        break;
      case TYPE_LOW_SHELF:
        {
          double sqA = Math.sqrt(A), tsa = 2 * sqA * alpha;
          b0 = A * ((A + 1) - (A - 1) * cosW0 + tsa);
          b1 = 2 * A * ((A - 1) - (A + 1) * cosW0);
          b2 = A * ((A + 1) - (A - 1) * cosW0 - tsa);
          a0 = (A + 1) + (A - 1) * cosW0 + tsa;
          a1 = -2 * ((A - 1) + (A + 1) * cosW0);
          a2 = (A + 1) + (A - 1) * cosW0 - tsa;
          break;
        }
      case TYPE_HIGH_SHELF:
        {
          double sqA = Math.sqrt(A), tsa = 2 * sqA * alpha;
          b0 = A * ((A + 1) + (A - 1) * cosW0 + tsa);
          b1 = -2 * A * ((A - 1) + (A + 1) * cosW0);
          b2 = A * ((A + 1) + (A - 1) * cosW0 - tsa);
          a0 = (A + 1) - (A - 1) * cosW0 + tsa;
          a1 = 2 * ((A - 1) - (A + 1) * cosW0);
          a2 = (A + 1) - (A - 1) * cosW0 - tsa;
          break;
        }
      default: // BELL
        b0 = 1 + alpha * A;
        b1 = -2 * cosW0;
        b2 = 1 - alpha * A;
        a0 = 1 + alpha / A;
        a1 = -2 * cosW0;
        a2 = 1 - alpha / A;
        break;
    }

    double w = 2 * Math.PI * freq / sampleRate;
    double cw = Math.cos(w), c2w = Math.cos(2 * w);
    double sw = Math.sin(w), s2w = Math.sin(2 * w);
    double nb0 = b0 / a0, nb1 = b1 / a0, nb2 = b2 / a0;
    double na1 = a1 / a0, na2 = a2 / a0;
    double numRe = nb0 + nb1 * cw + nb2 * c2w;
    double numIm = -(nb1 * sw + nb2 * s2w);
    double denRe = 1 + na1 * cw + na2 * c2w;
    double denIm = -(na1 * sw + na2 * s2w);
    double magSq = (numRe * numRe + numIm * numIm) / (denRe * denRe + denIm * denIm);
    return (magSq > 0) ? (float) (10 * Math.log10(magSq)) : 0;
  }

  // ─── Param mapping (matching C++) ───────────────────────────────

  private static float normToFreq(double norm) {
    return (float) (20.0 * Math.pow(1000.0, norm));
  }

  private static double freqToNorm(double freq) {
    return Math.log(freq / 20.0) / Math.log(1000.0);
  }

  private static float normToQ(double norm) {
    return (float) (0.1 * Math.pow(180.0, norm));
  }

  private static double qToNorm(double q) {
    return Math.log(q / 0.1) / Math.log(180.0);
  }

  private static String formatFreq(double freq) {
    if (freq >= 1000) return String.format("%.1fk", freq / 1000);
    return String.format("%.0f", freq);
  }

  // ─── Backend communication ──────────────────────────────────────

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
}
