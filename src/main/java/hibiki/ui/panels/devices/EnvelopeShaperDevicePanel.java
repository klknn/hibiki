package hibiki.ui.panels.devices;

import hibiki.ui.PluginPane;
import hibiki.ui.Theme;
import hibiki.ui.panels.KnobPanel;
import java.awt.*;
import java.awt.event.*;
import java.awt.geom.*;
import javax.swing.*;

/**
 * Envelope Shaper device panel with interactive curve editor, audio envelope visualization,
 * catmull-rom smoothing display, and arc-knob controls.
 */
public class EnvelopeShaperDevicePanel extends AbstractDevicePanel {
  private static final int PARAM_MIX = 0;
  private static final int PARAM_RATE = 1;
  private static final int PARAM_SMOOTH = 2;
  private static final int PARAM_MODE = 3;
  private static final int PARAM_ENABLE = 4;
  private static final int PARAM_POINT_Y_0 = 5;
  private static final int PARAM_NUM_POINTS = 21;
  private static final int MAX_POINTS = 16;
  private static final int TOTAL_PARAMS = PARAM_NUM_POINTS + 1;

  private final KnobPanel mixKnob, rateKnob, smoothKnob;
  private final JComboBox<String> modeCombo;
  private final JToggleButton enableBtn;
  private final CurveCanvas curveCanvas;

  private double[] pointY = new double[MAX_POINTS];
  private int numPoints = 6;
  private float envelopePhase = 0;
  private double smoothValue = 0.3;
  private static final int METER_HISTORY = 128;
  private float[] inHistory = new float[METER_HISTORY];
  private float[] outHistory = new float[METER_HISTORY];
  private int meterWrite = 0;
  public Runnable modToggleCallback;

  private static final String[] RATE_LABELS = {
    "1/16", "1/8 T", "1/8 d", "1/8", "1/4 T", "1/4 d", "1/4",
    "1/2 T", "1/2 d", "1/2", "3/4", "1 bar", "2 bars", "4 bars"
  };
  private static final String[] MODE_NAMES = {"Gain", "HPF", "BPF", "LPF"};
  private static final double[] MODE_VALUES = {0.0, 0.15, 0.4, 0.8};

  public EnvelopeShaperDevicePanel(int trackIndex, int pluginIndex) {
    super(trackIndex, pluginIndex, TOTAL_PARAMS);
    Theme theme = Theme.getInstance();

    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(theme.scale(440), theme.scale(260)));
    setMaximumSize(new Dimension(theme.scale(440), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Default curve
    pointY[0] = 1.0;
    pointY[1] = 0.0;
    pointY[2] = 0.05;
    pointY[3] = 0.2;
    pointY[4] = 0.6;
    pointY[5] = 1.0;
    for (int i = 6; i < MAX_POINTS; i++) pointY[i] = 1.0;

    // Header
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x7A4B1E));
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));
    JLabel nameLabel = new JLabel("EnvShaper");
    nameLabel.setForeground(Color.WHITE);
    nameLabel.setFont(theme.FONT_UI_BOLD);
    header.add(nameLabel, BorderLayout.CENTER);

    JPanel btnPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT, 2, 0));
    btnPanel.setOpaque(false);
    enableBtn = new JToggleButton("On", true);
    enableBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    enableBtn.addActionListener(e -> sendParam(PARAM_ENABLE, enableBtn.isSelected() ? 1.0 : 0.0));
    btnPanel.add(enableBtn);
    JButton modBtn = new JButton("Mod");
    modBtn.addActionListener(
        e -> {
          if (modToggleCallback != null) modToggleCallback.run();
        });
    btnPanel.add(modBtn);
    JButton scBtn = new JButton("SC");
    scBtn.addActionListener(e -> PluginPane.showSidechainPopup(scBtn, trackIndex, pluginIndex));
    btnPanel.add(scBtn);
    JButton delBtn = new JButton("\u274C");
    delBtn.addActionListener(e -> sendRemove());
    btnPanel.add(delBtn);
    header.add(btnPanel, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // Center: curve canvas
    curveCanvas = new CurveCanvas();
    add(curveCanvas, BorderLayout.CENTER);

    // Bottom: knobs + mode dropdown
    JPanel knobRow = new JPanel(new FlowLayout(FlowLayout.CENTER, theme.scale(8), 2));
    knobRow.setBackground(theme.BG_DARK);
    knobRow.setBorder(BorderFactory.createEmptyBorder(4, 4, 4, 4));
    knobRow.setPreferredSize(new Dimension(0, theme.scale(75)));

    mixKnob = new KnobPanel("Mix", PARAM_MIX, 1.0);
    rateKnob = new KnobPanel("Rate", PARAM_RATE, 0.43);
    smoothKnob = new KnobPanel("Smooth", PARAM_SMOOTH, 0.3);

    modeCombo = new JComboBox<>(MODE_NAMES);
    modeCombo.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    modeCombo.setPreferredSize(new Dimension(theme.scale(60), theme.scale(22)));
    modeCombo.addActionListener(
        e -> {
          int idx = modeCombo.getSelectedIndex();
          if (idx >= 0 && idx < MODE_VALUES.length) sendParam(PARAM_MODE, MODE_VALUES[idx]);
        });

    knobRow.add(mixKnob);
    knobRow.add(rateKnob);
    knobRow.add(smoothKnob);
    JPanel modePanel = new JPanel(new BorderLayout());
    modePanel.setOpaque(false);
    modePanel.setPreferredSize(new Dimension(theme.scale(60), theme.scale(65)));
    JLabel modeLabel = new JLabel("Mode", SwingConstants.CENTER);
    modeLabel.setForeground(theme.TEXT_DIM);
    modeLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    modePanel.add(modeLabel, BorderLayout.NORTH);
    modePanel.add(modeCombo, BorderLayout.CENTER);
    knobRow.add(modePanel);

    add(knobRow, BorderLayout.SOUTH);
  }

  public void updateParam(int paramId, double value) {
    if (paramId == PARAM_MIX) mixKnob.setValue(value);
    else if (paramId == PARAM_RATE) rateKnob.setValue(value);
    else if (paramId == PARAM_SMOOTH) {
      smoothValue = value;
      smoothKnob.setValue(value);
      curveCanvas.repaint();
    } else if (paramId == PARAM_MODE) {
      // Find closest mode
      int best = 0;
      double minDist = 2.0;
      for (int i = 0; i < MODE_VALUES.length; i++) {
        double d = Math.abs(value - MODE_VALUES[i]);
        if (d < minDist) {
          minDist = d;
          best = i;
        }
      }
      modeCombo.setSelectedIndex(best);
    } else if (paramId == PARAM_ENABLE) enableBtn.setSelected(value >= 0.5);
    else if (paramId == PARAM_NUM_POINTS) {
      numPoints = Math.max(2, Math.min(MAX_POINTS, (int) (value * 14 + 2)));
      curveCanvas.repaint();
    } else if (paramId >= PARAM_POINT_Y_0 && paramId < PARAM_POINT_Y_0 + MAX_POINTS) {
      pointY[paramId - PARAM_POINT_Y_0] = value;
      curveCanvas.repaint();
    }
  }

  public void setInputOutputLevel(float inDb, float outDb) {
    float inAmp = (float) Math.pow(10.0, Math.max(-60, inDb) / 20.0);
    float outAmp = (float) Math.pow(10.0, Math.max(-60, outDb) / 20.0);
    inHistory[meterWrite % METER_HISTORY] = inAmp;
    outHistory[meterWrite % METER_HISTORY] = outAmp;
    meterWrite++;
    curveCanvas.repaint();
  }

  public void setEnvelopePhase(float phase) {
    this.envelopePhase = phase;
    curveCanvas.repaint();
  }

  // --- Curve Canvas ---

  private class CurveCanvas extends JPanel {
    private int dragPoint = -1;

    CurveCanvas() {
      setOpaque(true);
      setBackground(new Color(0x1E1E1E));
      addMouseListener(
          new MouseAdapter() {
            public void mousePressed(MouseEvent e) {
              if (e.getClickCount() == 2) {
                handleDoubleClick(e.getX(), e.getY());
                return;
              }
              handlePress(e.getX(), e.getY());
            }

            public void mouseReleased(MouseEvent e) {
              dragPoint = -1;
            }
          });
      addMouseMotionListener(
          new MouseMotionAdapter() {
            public void mouseDragged(MouseEvent e) {
              handleDrag(e.getX(), e.getY());
            }
          });
    }

    private void handlePress(int mx, int my) {
      int w = getWidth(), h = getHeight(), pad = 10;
      for (int i = 0; i < numPoints; i++) {
        float px = pad + (float) i / (numPoints - 1) * (w - 2 * pad);
        float py = h - pad - (float) pointY[i] * (h - 2 * pad);
        if (Math.abs(mx - px) < 8 && Math.abs(my - py) < 8) {
          dragPoint = i;
          return;
        }
      }
    }

    private void handleDrag(int mx, int my) {
      if (dragPoint < 0 || dragPoint >= numPoints) return;
      int h = getHeight(), pad = 10;
      double v = Math.max(0.0, Math.min(1.0, 1.0 - (double) (my - pad) / (h - 2 * pad)));
      pointY[dragPoint] = v;
      sendParam(PARAM_POINT_Y_0 + dragPoint, v);
      repaint();
    }

    private void handleDoubleClick(int mx, int my) {
      int w = getWidth(), h = getHeight(), pad = 10;
      // Check if clicking on an existing point → delete it
      for (int i = 0; i < numPoints; i++) {
        float px = pad + (float) i / (numPoints - 1) * (w - 2 * pad);
        float py = h - pad - (float) pointY[i] * (h - 2 * pad);
        if (Math.abs(mx - px) < 8 && Math.abs(my - py) < 8) {
          if (numPoints <= 2) return; // Need at least 2 points
          // Shift points left to delete
          for (int j = i; j < numPoints - 1; j++) pointY[j] = pointY[j + 1];
          numPoints--;
          sendParam(PARAM_NUM_POINTS, (numPoints - 2) / 14.0);
          for (int j = 0; j < numPoints; j++) sendParam(PARAM_POINT_Y_0 + j, pointY[j]);
          repaint();
          return;
        }
      }
      // Not on a point → add new point
      if (numPoints >= MAX_POINTS) return;
      float normX = Math.max(0, Math.min(1, (float) (mx - pad) / (w - 2 * pad)));
      int insertIdx = Math.max(1, Math.min(numPoints, (int) (normX * numPoints + 0.5)));
      double yVal = Math.max(0.0, Math.min(1.0, 1.0 - (double) (my - pad) / (h - 2 * pad)));
      for (int i = MAX_POINTS - 1; i > insertIdx; i--) pointY[i] = pointY[i - 1];
      pointY[insertIdx] = yVal;
      numPoints++;
      sendParam(PARAM_NUM_POINTS, (numPoints - 2) / 14.0);
      for (int i = 0; i < numPoints; i++) sendParam(PARAM_POINT_Y_0 + i, pointY[i]);
      repaint();
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
      int w = getWidth(), h = getHeight(), pad = 10;

      // Grid
      g2.setColor(new Color(0x2A2A2A));
      for (int i = 0; i <= 4; i++) {
        int y = pad + i * (h - 2 * pad) / 4;
        g2.drawLine(pad, y, w - pad, y);
      }

      // Audio envelope overlay from gain meters (input=green, output=orange)
      if (meterWrite > 1) {
        int count = Math.min(meterWrite, METER_HISTORY);
        drawMeterOverlay(g2, w, h, pad, inHistory, count, new Color(0x4400FF00, true));
        drawMeterOverlay(g2, w, h, pad, outHistory, count, new Color(0x44FF6600, true));
      }

      // Cursor (playhead)
      if (envelopePhase > 0) {
        int cx = pad + (int) (envelopePhase * (w - 2 * pad));
        g2.setColor(new Color(0x55FF8800, true));
        g2.fillRect(cx - 1, pad, 2, h - 2 * pad);
      }

      // Interpolated curve (uses smoothing)
      g2.setColor(new Color(0xFF8800));
      g2.setStroke(new BasicStroke(2.0f));
      Path2D path = new Path2D.Float();
      for (int px = 0; px < w - 2 * pad; px++) {
        float t = (float) px / (w - 2 * pad);
        float y = evaluateCurve(t);
        int sy = h - pad - (int) (y * (h - 2 * pad));
        if (px == 0) path.moveTo(pad + px, sy);
        else path.lineTo(pad + px, sy);
      }
      g2.draw(path);

      // Fill under curve
      g2.setColor(new Color(0x33FF8800, true));
      Path2D fill = new Path2D.Float(path);
      fill.lineTo(w - pad, h - pad);
      fill.lineTo(pad, h - pad);
      fill.closePath();
      g2.fill(fill);

      // Breakpoints
      for (int i = 0; i < numPoints; i++) {
        float bx = pad + (float) i / (numPoints - 1) * (w - 2 * pad);
        float by = h - pad - (float) pointY[i] * (h - 2 * pad);
        g2.setColor(dragPoint == i ? Color.WHITE : new Color(0xFF8800));
        g2.fillOval((int) bx - 4, (int) by - 4, 8, 8);
        g2.setColor(new Color(0x1E1E1E));
        g2.drawOval((int) bx - 4, (int) by - 4, 8, 8);
      }

      g2.dispose();
    }

    private void drawMeterOverlay(
        Graphics2D g2, int w, int h, int pad, float[] history, int count, Color color) {
      g2.setColor(color);
      g2.setStroke(new BasicStroke(1.5f));
      Path2D p = new Path2D.Float();
      int start = (meterWrite - count + METER_HISTORY) % METER_HISTORY;
      for (int i = 0; i < count; i++) {
        float x = pad + (float) i / count * (w - 2 * pad);
        float amp = Math.min(1.0f, history[(start + i) % METER_HISTORY] * 4.0f);
        float y = h - pad - amp * (h - 2 * pad);
        if (i == 0) p.moveTo(x, y);
        else p.lineTo(x, y);
      }
      g2.draw(p);
    }

    /** Evaluate curve with catmull-rom smoothing matching the C++ implementation. */
    private float evaluateCurve(float t) {
      if (numPoints < 2) return 1.0f;
      float scaled = t * (numPoints - 1);
      int seg = Math.min((int) scaled, numPoints - 2);
      float frac = scaled - seg;
      float smooth = (float) smoothValue;

      if (smooth < 0.01f) {
        // Linear
        float y0 = (float) pointY[seg];
        float y1 = (float) pointY[Math.min(seg + 1, numPoints - 1)];
        return y0 + (y1 - y0) * frac;
      }

      // Catmull-Rom spline
      int i0 = Math.max(0, seg - 1);
      int i1 = seg;
      int i2 = Math.min(numPoints - 1, seg + 1);
      int i3 = Math.min(numPoints - 1, seg + 2);

      float p0 = (float) pointY[i0], p1 = (float) pointY[i1];
      float p2 = (float) pointY[i2], p3 = (float) pointY[i3];

      float t2 = frac * frac, t3 = t2 * frac;
      float spline =
          0.5f
              * ((2f * p1)
                  + (-p0 + p2) * frac
                  + (2f * p0 - 5f * p1 + 4f * p2 - p3) * t2
                  + (-p0 + 3f * p1 - 3f * p2 + p3) * t3);
      float linear = p1 + (p2 - p1) * frac;
      float val = linear * (1f - smooth) + spline * smooth;
      return Math.max(0f, Math.min(1f, val));
    }
  }

  // --- KnobPanel (matching existing panel convention: clockwise 0→100%) ---

  private class KnobPanel extends JPanel {
    private double value;
    private final int paramId;
    private int dragStartY;
    private final JLabel valLabel;

    KnobPanel(String name, int paramId, double defaultVal) {
      this.paramId = paramId;
      this.value = defaultVal;
      Theme theme = Theme.getInstance();
      setLayout(new BorderLayout());
      setOpaque(false);
      setPreferredSize(new Dimension(theme.scale(55), theme.scale(65)));

      JLabel nameLabel = new JLabel(name, SwingConstants.CENTER);
      nameLabel.setForeground(theme.TEXT_DIM);
      nameLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
      add(nameLabel, BorderLayout.NORTH);

      JPanel knobArea =
          new JPanel() {
            @Override
            protected void paintComponent(Graphics g) {
              paintArcKnob((Graphics2D) g, getWidth(), getHeight(), value, new Color(0x8BC34A));
            }
          };
      knobArea.setOpaque(false);
      knobArea.setCursor(Cursor.getPredefinedCursor(Cursor.N_RESIZE_CURSOR));
      knobArea.addMouseListener(
          new MouseAdapter() {
            public void mousePressed(MouseEvent e) {
              dragStartY = e.getY();
            }
          });
      knobArea.addMouseMotionListener(
          new MouseMotionAdapter() {
            public void mouseDragged(MouseEvent e) {
              int dy = dragStartY - e.getY();
              dragStartY = e.getY();
              value = Math.max(0.0, Math.min(1.0, value + dy * 0.005));
              sendParam(paramId, value);
              valLabel.setText(formatValue());
              repaint();
            }
          });
      add(knobArea, BorderLayout.CENTER);

      valLabel = new JLabel(formatValue(), SwingConstants.CENTER);
      valLabel.setForeground(theme.TEXT_LIGHT);
      valLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
      add(valLabel, BorderLayout.SOUTH);
    }

    void setValue(double v) {
      this.value = v;
      valLabel.setText(formatValue());
      repaint();
    }

    private String formatValue() {
      if (paramId == PARAM_RATE) {
        int idx =
            Math.max(
                0,
                Math.min(RATE_LABELS.length - 1, (int) (value * (RATE_LABELS.length - 1) + 0.5)));
        return RATE_LABELS[idx];
      }
      return String.format("%.0f%%", value * 100);
    }
  }

  /** Arc knob matching existing panel convention: clockwise 0→100%. */
}
