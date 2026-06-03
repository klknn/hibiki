package hibiki.ui.panels.devices;

import hibiki.BackendManager;
import hibiki.ui.PluginPane;
import hibiki.ui.Theme;
import java.awt.*;
import java.awt.event.*;
import java.util.ArrayList;
import java.util.List;
import javax.swing.*;
import javax.swing.event.ChangeEvent;
import javax.swing.event.ChangeListener;

/**
 * Custom Swing UI panel for the built-in Acid Bass monophonic synthesizer. Styled after the classic
 * Roland TB-303 silver face, with vertical black partition lines, red glowing LED lights, and
 * custom metal dials with white pointers. Includes a virtual TB-303 style MIDI keyboard with 12
 * note trigger indicators and red glowing LEDs.
 */
public class AcidBassDevicePanel extends AbstractDevicePanel {
  private static final int PARAM_WAVEFORM = 0; // 0.0 = Saw, 1.0 = Square
  private static final int PARAM_CUTOFF = 1; // 0.0 to 1.0
  private static final int PARAM_RESONANCE = 2; // 0.0 to 1.0
  private static final int PARAM_ENV_MOD = 3; // 0.0 to 1.0
  private static final int PARAM_DECAY = 4; // 0.05s to 3.0s
  private static final int PARAM_ACCENT = 5; // 0.0 to 1.0
  private static final int PARAM_OVERDRIVE = 6; // 0.0 to 1.0
  private static final int PARAM_VOLUME = 7; // 0.0 to 1.0
  private static final int PARAM_TRANSPOSE = 8; // 0.0 to 1.0
  private static final int PARAM_SLIDE = 9; // 0.0 or 1.0
  private static final int PARAM_ACCENT_SWITCH = 10; // 0.0 or 1.0
  private static final int TOTAL_PARAMS = 11;

  // Custom visual components
  private final WaveformSelectorPanel waveformSelector;
  private final AcidKnobPanel[] knobs;
  private final LedPanel powerLed;
  private final MidiKeyboardPanel keyboardPanel;

  public AcidBassDevicePanel(int trackIndex, int pluginIndex) {
    super(trackIndex, pluginIndex, TOTAL_PARAMS);

    // Initial default parameters matching C++ constructor defaults
    params[PARAM_WAVEFORM] = 0.0;
    params[PARAM_CUTOFF] = 0.3;
    params[PARAM_RESONANCE] = 0.6;
    params[PARAM_ENV_MOD] = 0.5;
    params[PARAM_DECAY] = 0.2;
    params[PARAM_ACCENT] = 0.0;
    params[PARAM_OVERDRIVE] = 0.1;
    params[PARAM_VOLUME] = 0.7;
    params[PARAM_TRANSPOSE] = 0.5;
    params[PARAM_SLIDE] = 0.0;
    params[PARAM_ACCENT_SWITCH] = 0.0;

    Theme theme = Theme.getInstance();
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(theme.scale(500), theme.scale(245)));
    setMaximumSize(new Dimension(theme.scale(500), Short.MAX_VALUE));
    setBorder(BorderFactory.createLineBorder(new Color(0x333333)));

    // Header
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x3E3E3E));
    header.setBorder(BorderFactory.createEmptyBorder(2, 6, 2, 6));

    JPanel titlePanel = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(4), 0));
    titlePanel.setOpaque(false);

    // Power LED indicator
    powerLed = new LedPanel(true, 8);
    titlePanel.add(powerLed);

    JLabel nameLabel = new JLabel("ACID BASS");
    nameLabel.setForeground(new Color(0xEEEEEE));
    nameLabel.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(10.0f)));
    titlePanel.add(nameLabel);
    header.add(titlePanel, BorderLayout.CENTER);

    JPanel btnPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT, 2, 0));
    btnPanel.setOpaque(false);

    JButton modBtn = new JButton("Mod");
    modBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    modBtn.setFocusPainted(false);
    btnPanel.add(modBtn);

    JButton scBtn = new JButton("SC");
    scBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    scBtn.setFocusPainted(false);
    scBtn.setToolTipText("Sidechain Source");
    btnPanel.add(scBtn);

    JButton delBtn = new JButton("\u274C");
    delBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    delBtn.setFocusPainted(false);
    btnPanel.add(delBtn);
    header.add(btnPanel, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // Body container: custom painted silver panel with black lines
    JPanel body =
        new JPanel() {
          @Override
          protected void paintComponent(Graphics g) {
            super.paintComponent(g);
            Graphics2D g2 = (Graphics2D) g.create();
            int w = getWidth(), h = getHeight();
            // Silver face gradient
            GradientPaint gp =
                new GradientPaint(0, 0, new Color(0xEAEAEA), 0, h, new Color(0xB5B5B5));
            g2.setPaint(gp);
            g2.fillRect(0, 0, w, h);

            // Thin black border at the bottom of the header
            g2.setColor(new Color(0x333333));
            g2.setStroke(new BasicStroke(1.0f));
            g2.drawLine(0, 0, w, 0);

            // Partition lines (vertical lines between controls)
            g2.setStroke(new BasicStroke(1.2f));
            int numSections = 8;
            int sectionW = w / numSections;
            for (int i = 1; i < numSections; i++) {
              int lx = i * sectionW;
              g2.drawLine(lx, 0, lx, h);
            }
            g2.dispose();
          }
        };
    body.setLayout(new GridLayout(1, 8, 0, 0));

    // Initialize all components first (to avoid definite-assignment issues)
    waveformSelector = new WaveformSelectorPanel(theme);

    String[] knobNames = {
      "Cutoff", "Resonance", "Env Mod", "Decay", "Accent", "Overdrive", "Volume"
    };
    int[] knobParamIds = {
      PARAM_CUTOFF,
      PARAM_RESONANCE,
      PARAM_ENV_MOD,
      PARAM_DECAY,
      PARAM_ACCENT,
      PARAM_OVERDRIVE,
      PARAM_VOLUME
    };
    knobs = new AcidKnobPanel[knobNames.length];
    for (int i = 0; i < knobNames.length; i++) {
      final int pid = knobParamIds[i];
      knobs[i] = new AcidKnobPanel(knobNames[i], params[pid], makeFormatter(pid), theme);
    }

    // Add components to the body layout
    body.add(waveformSelector);
    for (AcidKnobPanel knob : knobs) {
      body.add(knob);
    }
    add(body, BorderLayout.CENTER);

    // Initialize keyboard panel
    keyboardPanel = new MidiKeyboardPanel(theme);
    add(keyboardPanel, BorderLayout.SOUTH);

    // Wire up event listeners AFTER everything is instantiated
    modBtn.addActionListener(
        e -> {
          if (modToggleCallback != null) {
            modToggleCallback.run();
          }
        });

    scBtn.addActionListener(e -> PluginPane.showSidechainPopup(scBtn, trackIndex, pluginIndex));
    delBtn.addActionListener(e -> sendRemove());

    waveformSelector.addChangeListener(
        e -> {
          if (updatingFromBackend) return;
          double val = waveformSelector.getValue();
          params[PARAM_WAVEFORM] = val;
          sendParam(PARAM_WAVEFORM, val);
        });

    for (int i = 0; i < knobNames.length; i++) {
      final int idx = i;
      final int pid = knobParamIds[idx];
      knobs[idx].addChangeListener(
          e -> {
            if (updatingFromBackend) return;
            params[pid] = knobs[idx].getValue();
            sendParam(pid, params[pid]);
          });
    }
  }

  @Override
  public void updateParam(int paramId, double value) {
    if (paramId < 0 || paramId >= TOTAL_PARAMS) return;
    updatingFromBackend = true;
    params[paramId] = value;
    if (paramId == PARAM_WAVEFORM) {
      waveformSelector.setValue(value);
    } else if (paramId == PARAM_TRANSPOSE) {
      keyboardPanel.setTransposeParam(value);
    } else if (paramId == PARAM_SLIDE) {
      keyboardPanel.setSlideParam(value);
    } else if (paramId == PARAM_ACCENT_SWITCH) {
      keyboardPanel.setAccentParam(value);
    } else {
      int ki = findKnobIndex(paramId);
      if (ki >= 0 && ki < knobs.length && knobs[ki] != null) {
        knobs[ki].setValue(value);
      }
    }
    updatingFromBackend = false;
  }

  private int findKnobIndex(int paramId) {
    int[] knobParamIds = {
      PARAM_CUTOFF,
      PARAM_RESONANCE,
      PARAM_ENV_MOD,
      PARAM_DECAY,
      PARAM_ACCENT,
      PARAM_OVERDRIVE,
      PARAM_VOLUME
    };
    for (int i = 0; i < knobParamIds.length; i++) {
      if (knobParamIds[i] == paramId) return i;
    }
    return -1;
  }

  private ValueFormatter makeFormatter(int paramId) {
    return norm -> {
      if (paramId == PARAM_CUTOFF) {
        double hz = 100.0 * Math.pow(30.0, norm);
        return hz >= 1000.0 ? String.format("%.2fkHz", hz / 1000.0) : String.format("%.0fHz", hz);
      }
      if (paramId == PARAM_DECAY) {
        double s = 0.05 * Math.pow(60.0, norm);
        return s < 1.0 ? String.format("%.0fms", s * 1000.0) : String.format("%.2fs", s);
      }
      return String.format("%.0f%%", norm * 100.0);
    };
  }

  // ── Custom Waveform Selector Panel with LEDs ──────────────────
  private class WaveformSelectorPanel extends JPanel {
    private double val = 0.0;
    private final JToggleButton toggleBtn;
    private final List<ChangeListener> listeners = new ArrayList<>();
    private final LedPanel sawLed;
    private final LedPanel sqLed;

    WaveformSelectorPanel(Theme theme) {
      setOpaque(false);
      setLayout(new BorderLayout());
      setBorder(
          BorderFactory.createEmptyBorder(
              theme.scale(6), theme.scale(4), theme.scale(6), theme.scale(4)));

      JLabel titleLabel = new JLabel("WAVEFORM", SwingConstants.CENTER);
      titleLabel.setForeground(new Color(0x222222));
      titleLabel.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(8.0f)));
      add(titleLabel, BorderLayout.NORTH);

      // Custom selector drawing
      JPanel centerPanel = new JPanel(new GridBagLayout());
      centerPanel.setOpaque(false);

      GridBagConstraints gbc = new GridBagConstraints();
      gbc.gridx = 0;
      gbc.gridy = 0;
      gbc.insets = new Insets(theme.scale(2), theme.scale(2), theme.scale(2), theme.scale(2));

      // Saw icon/label & LED
      JLabel sawLabel = new JLabel("SAW", SwingConstants.CENTER);
      sawLabel.setForeground(new Color(0x333333));
      sawLabel.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(8.0f)));
      sawLed = new LedPanel(true, 6);

      centerPanel.add(sawLed, gbc);
      gbc.gridy = 1;
      centerPanel.add(sawLabel, gbc);

      // Button
      toggleBtn = new JToggleButton("~");
      toggleBtn.setPreferredSize(new Dimension(theme.scale(26), theme.scale(26)));
      toggleBtn.setFocusPainted(false);
      toggleBtn.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(9.0f)));
      toggleBtn.setMargin(new Insets(0, 0, 0, 0));
      toggleBtn.addActionListener(
          e -> {
            setValue(toggleBtn.isSelected() ? 1.0 : 0.0);
            for (ChangeListener l : listeners) {
              l.stateChanged(new ChangeEvent(WaveformSelectorPanel.this));
            }
          });

      gbc.gridx = 1;
      gbc.gridy = 0;
      gbc.gridheight = 2;
      centerPanel.add(toggleBtn, gbc);

      // Sq icon/label & LED
      gbc.gridx = 2;
      gbc.gridy = 0;
      gbc.gridheight = 1;
      JLabel sqLabel = new JLabel("SQR", SwingConstants.CENTER);
      sqLabel.setForeground(new Color(0x333333));
      sqLabel.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(8.0f)));
      sqLed = new LedPanel(false, 6);

      centerPanel.add(sqLed, gbc);
      gbc.gridy = 1;
      centerPanel.add(sqLabel, gbc);

      add(centerPanel, BorderLayout.CENTER);

      JLabel modeLabel = new JLabel("SAW / SQR", SwingConstants.CENTER);
      modeLabel.setForeground(new Color(0x555555));
      modeLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(7.5f)));
      add(modeLabel, BorderLayout.SOUTH);
    }

    double getValue() {
      return val;
    }

    void setValue(double value) {
      this.val = value;
      boolean isSquare = value >= 0.5;
      toggleBtn.setSelected(isSquare);
      sawLed.setLit(!isSquare);
      sqLed.setLit(isSquare);
      repaint();
    }

    void addChangeListener(ChangeListener l) {
      listeners.add(l);
    }
  }

  // ── Custom TB-303 Styled Knob Panel ────────────────────────────
  private class AcidKnobPanel extends JPanel {
    private double value;
    private final String name;
    private final ValueFormatter formatter;
    private final List<ChangeListener> listeners = new ArrayList<>();
    private final JLabel valLabel;
    private final JPanel knobCanvas;
    private int dragStartY;

    AcidKnobPanel(String name, double initialValue, ValueFormatter formatter, Theme theme) {
      this.value = initialValue;
      this.name = name;
      this.formatter = formatter;

      setOpaque(false);
      setLayout(new BorderLayout());
      setBorder(
          BorderFactory.createEmptyBorder(
              theme.scale(6), theme.scale(4), theme.scale(6), theme.scale(4)));

      JLabel nameLabel = new JLabel(name.toUpperCase(), SwingConstants.CENTER);
      nameLabel.setForeground(new Color(0x222222));
      nameLabel.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(8.0f)));
      add(nameLabel, BorderLayout.NORTH);

      knobCanvas =
          new JPanel() {
            @Override
            protected void paintComponent(Graphics g) {
              super.paintComponent(g);
              Graphics2D g2 = (Graphics2D) g.create();
              g2.setRenderingHint(
                  RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
              int w = getWidth(), h = getHeight();
              int size = Math.min(w, h) - theme.scale(6);
              int x = (w - size) / 2;
              int y = (h - size) / 2;
              int cx = w / 2;
              int cy = h / 2;

              // Draw tick marks around the knob (11 tick marks from 225 deg to -45 deg)
              g2.setColor(new Color(0x333333));
              g2.setStroke(new BasicStroke(theme.scale(1.2f)));
              for (int i = 0; i <= 10; i++) {
                double angleRad = Math.toRadians(225.0 - i * 27.0);
                int sx = cx + (int) ((size / 2 + theme.scale(1)) * Math.cos(angleRad));
                int sy = cy - (int) ((size / 2 + theme.scale(1)) * Math.sin(angleRad));
                int ex = cx + (int) ((size / 2 + theme.scale(4)) * Math.cos(angleRad));
                int ey = cy - (int) ((size / 2 + theme.scale(4)) * Math.sin(angleRad));
                g2.drawLine(sx, sy, ex, ey);
              }

              // Draw dial body shadow
              g2.setColor(new Color(0, 0, 0, 50));
              g2.fillOval(x + theme.scale(2), y + theme.scale(2), size, size);

              // Draw dial silver base (brushed aluminum gradient)
              GradientPaint dialGrad =
                  new GradientPaint(
                      x, y, new Color(0xE0E0E0), x + size, y + size, new Color(0x8A8A8A));
              g2.setPaint(dialGrad);
              g2.fillOval(x, y, size, size);

              // Dial border
              g2.setColor(new Color(0x444444));
              g2.setStroke(new BasicStroke(theme.scale(1.0f)));
              g2.drawOval(x, y, size, size);

              // Inner black cap
              int capSize = (int) (size * 0.7);
              int cx_cap = cx - capSize / 2;
              int cy_cap = cy - capSize / 2;
              g2.setColor(new Color(0x222222));
              g2.fillOval(cx_cap, cy_cap, capSize, capSize);

              // Red value arc (LED indicator ring inside)
              g2.setColor(new Color(0xFF3333));
              g2.setStroke(new BasicStroke(theme.scale(1.5f)));
              g2.drawArc(
                  cx_cap + theme.scale(2),
                  cy_cap + theme.scale(2),
                  capSize - theme.scale(4),
                  capSize - theme.scale(4),
                  225,
                  (int) (-270 * value));

              // Dial pointer line
              double angle = Math.toRadians(225 - 270 * value);
              int px = cx + (int) ((size / 2 - theme.scale(2)) * Math.cos(angle));
              int py = cy - (int) ((size / 2 - theme.scale(2)) * Math.sin(angle));
              g2.setColor(Color.WHITE);
              g2.setStroke(
                  new BasicStroke(
                      theme.scale(2.0f), BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND));
              g2.drawLine(cx, cy, px, py);

              g2.dispose();
            }
          };
      knobCanvas.setOpaque(false);
      knobCanvas.setPreferredSize(new Dimension(theme.scale(32), theme.scale(32)));
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
              for (ChangeListener l : listeners) {
                l.stateChanged(new ChangeEvent(AcidKnobPanel.this));
              }
            }
          });
      add(knobCanvas, BorderLayout.CENTER);

      valLabel = new JLabel(formatter.format(initialValue), SwingConstants.CENTER);
      valLabel.setForeground(new Color(0x333333));
      valLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
      add(valLabel, BorderLayout.SOUTH);
    }

    double getValue() {
      return value;
    }

    void setValue(double v) {
      this.value = v;
      valLabel.setText(formatter.format(v));
      knobCanvas.repaint();
    }

    void addChangeListener(ChangeListener l) {
      listeners.add(l);
    }
  }

  // ── Custom Painted Glowing Red LED Helper & Panel ──────────────
  static void drawLed(Graphics2D g2, int cx, int cy, int size, boolean lit) {
    g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

    // Outer metallic ring
    g2.setColor(new Color(0x777777));
    g2.setStroke(new BasicStroke(1.0f));
    g2.drawOval(cx - size / 2, cy - size / 2, size, size);

    // Core color
    Color ledColor = lit ? new Color(0xFF3333) : new Color(0x550000);
    g2.setColor(ledColor);
    g2.fillOval(cx - size / 2 + 1, cy - size / 2 + 1, size - 2, size - 2);

    // Glow highlight / reflection
    if (lit) {
      // Red glow
      RadialGradientPaint rgp =
          new RadialGradientPaint(
              new Point(cx - size / 4, cy - size / 4),
              size * 0.8f,
              new float[] {0.0f, 0.4f, 1.0f},
              new Color[] {
                new Color(255, 255, 255, 220),
                new Color(255, 51, 51, 240),
                new Color(255, 51, 51, 0)
              });
      g2.setPaint(rgp);
      g2.fillOval(cx - size, cy - size, size * 2, size * 2);
    } else {
      // Small matte reflection dot
      g2.setColor(new Color(255, 255, 255, 60));
      g2.fillOval(cx - size / 3, cy - size / 3, size / 3, size / 3);
    }
  }

  private static class LedPanel extends JPanel {
    private boolean lit;
    private final int size;

    LedPanel(boolean lit, int size) {
      this.lit = lit;
      this.size = size;
      setOpaque(false);
      setPreferredSize(new Dimension(size + 6, size + 6));
    }

    void setLit(boolean lit) {
      if (this.lit != lit) {
        this.lit = lit;
        repaint();
      }
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      drawLed(g2, getWidth() / 2, getHeight() / 2, size, lit);
      g2.dispose();
    }
  }

  // ── Custom TB-303 Styled MIDI Keyboard Panel ───────────────────
  private class MidiKeyboardPanel extends JPanel {
    private final Theme theme;
    private final int baseMidiNoteOffset = 24; // base constant offset
    private int octave = 3; // C3 = note 60 (3 * 12 + 24)
    private boolean slideEnabled = false;
    private boolean accentEnabled = false;
    private int activeMidiNote = -1; // Currently triggered semitone offset [0..11]

    MidiKeyboardPanel(Theme theme) {
      this.theme = theme;
      setOpaque(false);
      setPreferredSize(new Dimension(theme.scale(500), theme.scale(95)));

      // Mouse handler for virtual MIDI triggers and control buttons
      MouseAdapter mouseHandler =
          new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
              int mx = e.getX();
              int my = e.getY();

              int w = getWidth();
              int spacing = theme.scale(32);
              int kw = theme.scale(14);
              int startX = (w - (6 * spacing + kw)) / 2;
              int keysLeft = startX;
              int keysRight = startX + 6 * spacing;

              int octD_cx = keysLeft - theme.scale(85);
              int octD_cy = theme.scale(45);
              int octU_cx = keysLeft - theme.scale(25);
              int octU_cy = theme.scale(45);

              int slide_cx = keysRight + theme.scale(30);
              int slide_cy = theme.scale(48);
              int accent_cx = keysRight + theme.scale(70);
              int accent_cy = theme.scale(48);

              // Check control buttons first
              if (isInsideCircle(mx, my, octD_cx, octD_cy, 9)) {
                triggerNoteOff();
                if (octave > 1) {
                  double newVal = ((octave - 1) - 1) / 4.0;
                  params[PARAM_TRANSPOSE] = newVal;
                  sendParam(PARAM_TRANSPOSE, newVal);
                  octave--;
                  repaint();
                }
                return;
              }
              if (isInsideCircle(mx, my, octU_cx, octU_cy, 9)) {
                triggerNoteOff();
                if (octave < 5) {
                  double newVal = ((octave - 1) + 1) / 4.0;
                  params[PARAM_TRANSPOSE] = newVal;
                  sendParam(PARAM_TRANSPOSE, newVal);
                  octave++;
                  repaint();
                }
                return;
              }
              if (isInsideCircle(mx, my, slide_cx, slide_cy, 9)) {
                double newVal = slideEnabled ? 0.0 : 1.0;
                params[PARAM_SLIDE] = newVal;
                sendParam(PARAM_SLIDE, newVal);
                slideEnabled = !slideEnabled;
                repaint();
                return;
              }
              if (isInsideCircle(mx, my, accent_cx, accent_cy, 9)) {
                double newVal = accentEnabled ? 0.0 : 1.0;
                params[PARAM_ACCENT_SWITCH] = newVal;
                sendParam(PARAM_ACCENT_SWITCH, newVal);
                accentEnabled = !accentEnabled;
                repaint();
                return;
              }

              // Check keys
              int note = getNoteAt(mx, my);
              if (note != -1) {
                triggerNoteOn(note);
              }
            }

            @Override
            public void mouseReleased(MouseEvent e) {
              triggerNoteOff();
            }

            @Override
            public void mouseDragged(MouseEvent e) {
              int mx = e.getX();
              int my = e.getY();
              int note = getNoteAt(mx, my);
              if (note != activeMidiNote) {
                int prevNote = activeMidiNote;
                activeMidiNote = note;
                if (note != -1) {
                  int baseMidiNote = 3 * 12 + baseMidiNoteOffset;
                  int vel = accentEnabled ? 127 : 90;
                  BackendManager.getInstance()
                      .sendVirtualMidi(trackIndex, baseMidiNote + note, vel, true);
                }
                if (prevNote != -1) {
                  int baseMidiNote = 3 * 12 + baseMidiNoteOffset;
                  BackendManager.getInstance()
                      .sendVirtualMidi(trackIndex, baseMidiNote + prevNote, 0, false);
                }
                repaint();
              }
            }

            @Override
            public void mouseExited(MouseEvent e) {
              triggerNoteOff();
            }
          };

      addMouseListener(mouseHandler);
      addMouseMotionListener(mouseHandler);
    }

    private boolean isInsideCircle(int mx, int my, int scaledCx, int scaledCy, int radius) {
      int scaledR = theme.scale(radius);
      int dx = mx - scaledCx;
      int dy = my - scaledCy;
      return dx * dx + dy * dy <= scaledR * scaledR;
    }

    void setTransposeParam(double value) {
      int newOctave = (int) Math.round(value * 4.0) + 1;
      if (newOctave >= 1 && newOctave <= 5) {
        this.octave = newOctave;
        repaint();
      }
    }

    void setSlideParam(double value) {
      this.slideEnabled = value >= 0.5;
      repaint();
    }

    void setAccentParam(double value) {
      this.accentEnabled = value >= 0.5;
      repaint();
    }

    private void triggerNoteOn(int note) {
      activeMidiNote = note;
      int baseMidiNote = 3 * 12 + baseMidiNoteOffset;
      int vel = accentEnabled ? 127 : 90;
      BackendManager.getInstance().sendVirtualMidi(trackIndex, baseMidiNote + note, vel, true);
      repaint();
    }

    private void triggerNoteOff() {
      if (activeMidiNote != -1) {
        int baseMidiNote = 3 * 12 + baseMidiNoteOffset;
        BackendManager.getInstance()
            .sendVirtualMidi(trackIndex, baseMidiNote + activeMidiNote, 0, false);
        activeMidiNote = -1;
        repaint();
      }
    }

    private int getNoteAt(int mx, int my) {
      int w = getWidth();
      int spacing = theme.scale(32);
      int kw = theme.scale(14);
      int kh = theme.scale(28);
      int startX = (w - (6 * spacing + kw)) / 2;

      // Check black keys first (overlapping in front)
      int[] blackOffsets = {1, 3, 6, 8, 10}; // C#, D#, F#, G#, A#
      double[] blackXMultiplier = {0.5, 1.5, 3.5, 4.5, 5.5};
      int blackY = theme.scale(28);

      for (int i = 0; i < blackOffsets.length; i++) {
        int kx = startX + (int) (blackXMultiplier[i] * spacing);
        int ky = blackY;
        int rx = kx - kw / 2;
        int ry = ky - kh / 2;
        if (mx >= rx && mx <= rx + kw && my >= ry && my <= ry + kh) {
          return blackOffsets[i];
        }
      }

      // Check white keys
      int[] whiteOffsets = {0, 2, 4, 5, 7, 9, 11}; // C, D, E, F, G, A, B
      int whiteY = theme.scale(62);

      for (int i = 0; i < whiteOffsets.length; i++) {
        int kx = startX + i * spacing;
        int ky = whiteY;
        int rx = kx - kw / 2;
        int ry = ky - kh / 2;
        if (mx >= rx && mx <= rx + kw && my >= ry && my <= ry + kh) {
          return whiteOffsets[i];
        }
      }

      return -1;
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

      int w = getWidth(), h = getHeight();

      // Silver keyboard chassis
      GradientPaint gp = new GradientPaint(0, 0, new Color(0xEAEAEA), 0, h, new Color(0xB5B5B5));
      g2.setPaint(gp);
      g2.fillRect(0, 0, w, h);

      // Top partition line separating keyboard from knobs
      g2.setColor(new Color(0x111111));
      g2.setStroke(new BasicStroke(1.5f));
      g2.drawLine(0, 0, w, 0);

      int spacing = theme.scale(32);
      int kw = theme.scale(14);
      int kh = theme.scale(28);
      int ledSize = theme.scale(5);
      int startX = (w - (6 * spacing + kw)) / 2;
      int keysLeft = startX;
      int keysRight = startX + 6 * spacing;

      // ─── Render Left Transpose Octave Section ───
      g2.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(8.0f)));
      g2.setColor(new Color(0x222222));
      FontMetrics fmBold = g2.getFontMetrics();
      g2.drawString(
          "OCTAVE",
          (keysLeft - theme.scale(55)) - fmBold.stringWidth("OCTAVE") / 2,
          theme.scale(20));

      // Octave Down Button (◀)
      int octD_cx = keysLeft - theme.scale(85), octD_cy = theme.scale(45), r = theme.scale(9);
      GradientPaint btnGradOct =
          new GradientPaint(
              octD_cx - r,
              octD_cy - r,
              new Color(0xEEEEEE),
              octD_cx + r,
              octD_cy + r,
              new Color(0xCCCCCC));
      g2.setPaint(btnGradOct);
      g2.fillOval(octD_cx - r, octD_cy - r, r * 2, r * 2);
      g2.setColor(new Color(0x333333));
      g2.drawOval(octD_cx - r, octD_cy - r, r * 2, r * 2);
      // Draw left arrow
      g2.setColor(new Color(0x111111));
      int[] xPointsL = {
        octD_cx + theme.scale(3), octD_cx + theme.scale(3), octD_cx - theme.scale(3)
      };
      int[] yPointsL = {octD_cy - theme.scale(4), octD_cy + theme.scale(4), octD_cy};
      g2.fillPolygon(xPointsL, yPointsL, 3);

      // Octave Up Button (▶)
      int octU_cx = keysLeft - theme.scale(25), octU_cy = theme.scale(45);
      g2.setPaint(btnGradOct);
      g2.fillOval(octU_cx - r, octU_cy - r, r * 2, r * 2);
      g2.setColor(new Color(0x333333));
      g2.drawOval(octU_cx - r, octU_cy - r, r * 2, r * 2);
      // Draw right arrow
      g2.setColor(new Color(0x111111));
      int[] xPointsR = {
        octU_cx - theme.scale(3), octU_cx - theme.scale(3), octU_cx + theme.scale(3)
      };
      int[] yPointsR = {octU_cy - theme.scale(4), octU_cy + theme.scale(4), octU_cy};
      g2.fillPolygon(xPointsR, yPointsR, 3);

      // Display Box for current Octave
      int dispX = keysLeft - theme.scale(68),
          dispY = theme.scale(66),
          dispW = theme.scale(36),
          dispH = theme.scale(16);
      g2.setColor(new Color(0x222222));
      g2.fillRoundRect(dispX, dispY, dispW, dispH, theme.scale(3), theme.scale(3));
      g2.setColor(new Color(0xFF3333)); // Red glowing digital look
      g2.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(8.0f)));
      String octStr = "C" + octave;
      FontMetrics fmDisp = g2.getFontMetrics();
      g2.drawString(
          octStr, dispX + (dispW - fmDisp.stringWidth(octStr)) / 2, dispY + theme.scale(11));

      // ─── Render Keys ───
      // 1. Black keys (upper row)
      int[] blackOffsets = {1, 3, 6, 8, 10}; // C#, D#, F#, G#, A#
      double[] blackXMultiplier = {0.5, 1.5, 3.5, 4.5, 5.5};
      String[] blackNames = {"C#", "D#", "F#", "G#", "A#"};
      int blackY = theme.scale(28);

      for (int i = 0; i < blackOffsets.length; i++) {
        int note = blackOffsets[i];
        int kx = startX + (int) (blackXMultiplier[i] * spacing);
        int ky = blackY;
        boolean isLit = (activeMidiNote == note);

        // LED trigger indicator placed vertically on top of key
        drawLed(g2, kx, ky - kh / 2 - theme.scale(8), ledSize, isLit);

        // Black surrounding box on the chassis
        int boxW = kw + theme.scale(6);
        int boxH = kh + theme.scale(6);
        int bx = kx - boxW / 2;
        int by = ky - boxH / 2;
        g2.setColor(new Color(0x0E0E0E));
        g2.fillRoundRect(bx, by, boxW, boxH, theme.scale(3), theme.scale(3));
        g2.setColor(new Color(0x444444));
        g2.setStroke(new BasicStroke(1.0f));
        g2.drawRoundRect(bx, by, boxW, boxH, theme.scale(3), theme.scale(3));

        // Rectangular silver key button
        int rx = kx - kw / 2;
        int ry = ky - kh / 2;
        GradientPaint btnGrad =
            isLit
                ? new GradientPaint(rx, ry, new Color(0xDDDDDD), rx, ry + kh, new Color(0x999999))
                : new GradientPaint(rx, ry, new Color(0xFAFAFA), rx, ry + kh, new Color(0xCCCCCC));
        g2.setPaint(btnGrad);
        g2.fillRoundRect(rx, ry, kw, kh, theme.scale(2), theme.scale(2));
        g2.setColor(new Color(0x333333));
        g2.setStroke(new BasicStroke(1.0f));
        g2.drawRoundRect(rx, ry, kw, kh, theme.scale(2), theme.scale(2));

        // Key labels
        g2.setFont(theme.FONT_UI.deriveFont(theme.scale(7.0f)));
        g2.setColor(new Color(0x333333));
        FontMetrics fm = g2.getFontMetrics();
        g2.drawString(
            blackNames[i], kx - fm.stringWidth(blackNames[i]) / 2, ky + kh / 2 + theme.scale(10));
      }

      // 2. White keys (lower row)
      int[] whiteOffsets = {0, 2, 4, 5, 7, 9, 11}; // C, D, E, F, G, A, B
      String[] whiteNames = {"C", "D", "E", "F", "G", "A", "B"};
      int whiteY = theme.scale(62);

      for (int i = 0; i < whiteOffsets.length; i++) {
        int note = whiteOffsets[i];
        int kx = startX + i * spacing;
        int ky = whiteY;
        boolean isLit = (activeMidiNote == note);

        // LED trigger indicator placed vertically on top of key
        drawLed(g2, kx, ky - kh / 2 - theme.scale(8), ledSize, isLit);

        // Rectangular silver key button
        int rx = kx - kw / 2;
        int ry = ky - kh / 2;
        GradientPaint btnGrad =
            isLit
                ? new GradientPaint(rx, ry, new Color(0xDDDDDD), rx, ry + kh, new Color(0x999999))
                : new GradientPaint(rx, ry, new Color(0xFAFAFA), rx, ry + kh, new Color(0xCCCCCC));
        g2.setPaint(btnGrad);
        g2.fillRoundRect(rx, ry, kw, kh, theme.scale(2), theme.scale(2));
        g2.setColor(new Color(0x555555));
        g2.setStroke(new BasicStroke(1.0f));
        g2.drawRoundRect(rx, ry, kw, kh, theme.scale(2), theme.scale(2));

        // Key labels
        g2.setFont(theme.FONT_UI.deriveFont(theme.scale(7.0f)));
        g2.setColor(new Color(0x333333));
        FontMetrics fm = g2.getFontMetrics();
        g2.drawString(
            whiteNames[i], kx - fm.stringWidth(whiteNames[i]) / 2, ky + kh / 2 + theme.scale(10));
      }

      // ─── Render Right Slide/Accent Buttons Section ───
      // Slide Button
      int slide_cx = keysRight + theme.scale(30), slide_cy = theme.scale(48);
      drawLed(g2, slide_cx, slide_cy - theme.scale(24), ledSize, slideEnabled);
      g2.setPaint(btnGradOct);
      g2.fillOval(slide_cx - r, slide_cy - r, r * 2, r * 2);
      g2.setColor(new Color(0x333333));
      g2.drawOval(slide_cx - r, slide_cy - r, r * 2, r * 2);
      g2.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(7.5f)));
      g2.setColor(new Color(0x222222));
      g2.drawString(
          "SLIDE",
          slide_cx - fmBold.stringWidth("SLIDE") / 2 + theme.scale(2),
          slide_cy + r + theme.scale(10));

      // Accent Button
      int accent_cx = keysRight + theme.scale(70), accent_cy = theme.scale(48);
      drawLed(g2, accent_cx, accent_cy - theme.scale(24), ledSize, accentEnabled);
      g2.setPaint(btnGradOct);
      g2.fillOval(accent_cx - r, accent_cy - r, r * 2, r * 2);
      g2.setColor(new Color(0x333333));
      g2.drawOval(accent_cx - r, accent_cy - r, r * 2, r * 2);
      g2.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(7.5f)));
      g2.setColor(new Color(0x222222));
      g2.drawString(
          "ACCENT",
          accent_cx - fmBold.stringWidth("ACCENT") / 2 + theme.scale(2),
          accent_cy + r + theme.scale(10));

      g2.dispose();
    }
  }
}
