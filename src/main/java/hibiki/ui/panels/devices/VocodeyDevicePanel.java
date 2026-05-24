package hibiki.ui.panels.devices;

import hibiki.ui.PluginPane;
import hibiki.ui.Theme;
import hibiki.ui.panels.KnobPanel;
import java.awt.*;
import java.awt.event.*;
import java.util.Random;
import javax.swing.*;

/** Premium vocoder device panel with animated 24-band filterbank visualizer. */
public class VocodeyDevicePanel extends AbstractDevicePanel {
  private static final int PARAM_ATTACK = 0;
  private static final int PARAM_DECAY = 1;
  private static final int PARAM_BANDWIDTH = 2;
  private static final int PARAM_NOISE_BLEED = 3;
  private static final int PARAM_SYNTH_DETUNE = 4;
  private static final int PARAM_DRY = 5;
  private static final int PARAM_WET = 6;
  private static final int PARAM_VOLUME = 7;
  private static final int TOTAL_PARAMS = 8;

  private static final Color ACCENT = new Color(0xBB86FC); // Glowing Neon Purple

  private final KnobPanel[] knobs;
  private final SpectralVisualizerPanel visualizer;

  public VocodeyDevicePanel(int trackIndex, int pluginIndex) {
    super(trackIndex, pluginIndex, TOTAL_PARAMS);

    // Initial default parameters matching the C++ constructor
    params[PARAM_ATTACK] = 0.15;
    params[PARAM_DECAY] = 0.25;
    params[PARAM_BANDWIDTH] = 0.35;
    params[PARAM_NOISE_BLEED] = 0.3;
    params[PARAM_SYNTH_DETUNE] = 0.15;
    params[PARAM_DRY] = 0.0;
    params[PARAM_WET] = 1.0;
    params[PARAM_VOLUME] = 0.7;

    Theme theme = Theme.getInstance();
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(theme.scale(420), theme.scale(230)));
    setMaximumSize(new Dimension(theme.scale(420), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Header
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x4A148C)); // Deep Purple
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));

    JLabel nameLabel = new JLabel("Vocodey");
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
    scBtn.setToolTipText("Sidechain / Carrier Source");
    scBtn.addActionListener(e -> PluginPane.showSidechainPopup(scBtn, trackIndex, pluginIndex));
    btnPanel.add(scBtn);

    JButton delBtn = new JButton("\u274C");
    delBtn.addActionListener(e -> sendRemove());
    btnPanel.add(delBtn);
    header.add(btnPanel, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // Center: Interactive/Animated 24-band filterbank visualizer
    visualizer = new SpectralVisualizerPanel();
    add(visualizer, BorderLayout.CENTER);

    // Bottom: 8 Knobs Row
    JPanel knobRow = new JPanel(new GridLayout(1, 8, theme.scale(2), 0));
    knobRow.setBackground(theme.BG_DARK);
    knobRow.setPreferredSize(new Dimension(0, theme.scale(68)));
    knobRow.setBorder(
        BorderFactory.createEmptyBorder(
            theme.scale(4), theme.scale(6), theme.scale(4), theme.scale(6)));

    String[] names = {"Attack", "Decay", "B-Width", "Noise", "Detune", "Dry", "Wet", "Volume"};
    int[] paramIds = {
      PARAM_ATTACK,
      PARAM_DECAY,
      PARAM_BANDWIDTH,
      PARAM_NOISE_BLEED,
      PARAM_SYNTH_DETUNE,
      PARAM_DRY,
      PARAM_WET,
      PARAM_VOLUME
    };

    knobs = new KnobPanel[8];
    for (int k = 0; k < 8; ++k) {
      final int pid = paramIds[k];
      knobs[k] = new KnobPanel(names[k], params[pid], makeFormatter(pid), ACCENT);
      knobs[k].addChangeListener(
          e -> {
            if (updatingFromBackend) return;
            params[pid] = knobs[findKnobIndex(pid)].getValue();
            sendParam(pid, params[pid]);
          });
      knobRow.add(knobs[k]);
    }
    add(knobRow, BorderLayout.SOUTH);
  }

  private int findKnobIndex(int paramId) {
    int[] ids = {
      PARAM_ATTACK,
      PARAM_DECAY,
      PARAM_BANDWIDTH,
      PARAM_NOISE_BLEED,
      PARAM_SYNTH_DETUNE,
      PARAM_DRY,
      PARAM_WET,
      PARAM_VOLUME
    };
    for (int i = 0; i < ids.length; ++i) {
      if (ids[i] == paramId) return i;
    }
    return 0;
  }

  private ValueFormatter makeFormatter(int paramId) {
    return norm -> {
      switch (paramId) {
        case PARAM_ATTACK:
          double attMs = (0.001 + norm * 0.099) * 1000.0;
          return attMs < 10.0 ? String.format("%.1f ms", attMs) : String.format("%.0f ms", attMs);
        case PARAM_DECAY:
          double decMs = (0.01 + norm * 1.99) * 1000.0;
          return decMs < 1000.0
              ? String.format("%.0f ms", decMs)
              : String.format("%.2f s", decMs / 1000.0);
        case PARAM_BANDWIDTH:
          return String.format("%.1f Q", 0.5 + norm * 9.5);
        case PARAM_NOISE_BLEED:
          return String.format("%.0f%%", norm * 100.0);
        case PARAM_SYNTH_DETUNE:
          return String.format("%.1f st", norm * 4.0);
        case PARAM_DRY:
        case PARAM_WET:
          return String.format("%.0f%%", norm * 100.0);
        case PARAM_VOLUME:
          if (norm <= 0.001) return "-inf dB";
          return String.format("%.1f dB", 20.0 * Math.log10(norm));
        default:
          return String.format("%.2f", norm);
      }
    };
  }

  public void updateParam(int paramId, double value) {
    if (paramId < 0 || paramId >= TOTAL_PARAMS) return;
    updatingFromBackend = true;
    params[paramId] = value;
    int ki = findKnobIndex(paramId);
    if (ki >= 0 && ki < knobs.length && knobs[ki] != null) {
      knobs[ki].setValue(value);
    }
    updatingFromBackend = false;
  }

  @Override
  public void removeNotify() {
    visualizer.stopAnimation();
    super.removeNotify();
  }

  /** An animated 24-band real-time visualizer panel. */
  private class SpectralVisualizerPanel extends JPanel {
    private final float[] bandHeights = new float[24];
    private final float[] targetHeights = new float[24];
    private final Timer timer;
    private final Random random = new Random();
    private int animationTicks = 0;

    public SpectralVisualizerPanel() {
      setBackground(Theme.getInstance().BG_MEDIUM);

      // Animation loop at ~30 FPS (every 33ms)
      timer =
          new Timer(
              33,
              e -> {
                animationTicks++;
                double decayNorm = params[PARAM_DECAY];
                double decaySpeed = 0.05 + (1.0 - decayNorm) * 0.25;

                double noiseBleed = params[PARAM_NOISE_BLEED];
                double bandwidthNorm = params[PARAM_BANDWIDTH];

                // Change target heights periodically to simulate vocal formants and synth activity
                if (animationTicks % 6 == 0) {
                  // Simulate 3 formant peaks
                  int peak1 = 3 + random.nextInt(4);
                  int peak2 = 8 + random.nextInt(6);
                  int peak3 = 16 + random.nextInt(5);

                  for (int j = 0; j < 24; ++j) {
                    float val = 0.05f;
                    // Standard vocal formant resonance simulation
                    val += 0.6f * Math.exp(-Math.pow(j - peak1, 2) / 3.0);
                    val += 0.4f * Math.exp(-Math.pow(j - peak2, 2) / 4.0);
                    val += 0.2f * Math.exp(-Math.pow(j - peak3, 2) / 2.0);

                    // Noise sibilance simulation in upper bands
                    if (j >= 16) {
                      val += (float) (noiseBleed * 0.3 * random.nextDouble());
                    }

                    // Scaled by bandwidth setting
                    val *= (float) (0.5 + bandwidthNorm * 0.5);
                    targetHeights[j] = Math.min(1.0f, val);
                  }
                }

                // Interpolate band heights for smooth movements
                for (int j = 0; j < 24; ++j) {
                  bandHeights[j] += (float) ((targetHeights[j] - bandHeights[j]) * decaySpeed);
                  if (bandHeights[j] < 0.0f) bandHeights[j] = 0.0f;
                }
                repaint();
              });
      timer.start();
    }

    public void stopAnimation() {
      if (timer != null && timer.isRunning()) {
        timer.stop();
      }
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

      int w = getWidth(), h = getHeight();
      int padX = 10, padY = 8;
      int innerW = w - padX * 2, innerH = h - padY * 2;

      // Draw background grid lines
      g2.setColor(new Color(255, 255, 255, 10));
      g2.setStroke(new BasicStroke(1.0f));
      for (int i = 1; i < 4; ++i) {
        int yLine = padY + (innerH * i) / 4;
        g2.drawLine(padX, yLine, w - padX, yLine);
      }

      // Draw 24 logarithmic-like bands
      double barW = (double) innerW / 24.0;
      int gap = 2;

      for (int j = 0; j < 24; ++j) {
        int barH = (int) (bandHeights[j] * innerH);
        int bx = padX + (int) (j * barW);
        int by = h - padY - barH;
        int bw = (int) barW - gap;
        if (bw < 1) bw = 1;

        if (barH > 0) {
          // Purple glowing gradient
          GradientPaint gp =
              new GradientPaint(bx, by, new Color(0xBB86FC), bx, h - padY, new Color(0x3700B3));
          g2.setPaint(gp);
          g2.fillRect(bx, by, bw, barH);
        }
      }

      // Draw border lines around the spectrum box
      g2.setColor(new Color(255, 255, 255, 15));
      g2.drawRect(padX, padY, innerW, innerH);

      g2.dispose();
    }
  }
}
