package hibiki.ui.panels.devices;

import hibiki.ui.PluginPane;
import hibiki.ui.Theme;
import hibiki.ui.panels.KnobPanel;
import java.awt.*;
import java.awt.event.*;
import java.util.Random;
import javax.swing.*;

/**
 * Unified device panel for all DR8 drum synthesizers (Kick, Snare, Hat, Tom, Clap, Cowbell, Crash,
 * Rimshot, Conga). Shows the simulated 1-shot waveform on top and the parameter knobs on the
 * bottom.
 */
public class Dr8DevicePanel extends AbstractDevicePanel {

  public enum DrumType {
    KICK,
    SNARE,
    HAT,
    TOM,
    CLAP,
    COWBELL,
    CRASH,
    RIMSHOT,
    CONGA
  }

  private final DrumType type;
  private final KnobPanel[] knobs;
  private final WaveformDisplayPanel waveformPanel;
  private final Color accentColor;
  protected final int[] paramIds;

  protected Dr8DevicePanel(
      int trackIndex,
      int pluginIndex,
      DrumType type,
      int totalParams,
      String name,
      String[] paramNames,
      int[] paramIds) {
    super(trackIndex, pluginIndex, totalParams);
    this.type = type;
    this.paramIds = paramIds;

    // TR-808 authentic color scheme (Black/Red/Orange/Yellow/White)
    switch (type) {
      case KICK:
        this.accentColor = new Color(0xE53935);
        break; // Red
      case SNARE:
        this.accentColor = new Color(0xFF5722);
        break; // Orange-Red
      case HAT:
        this.accentColor = new Color(0xFFD600);
        break; // Yellow
      case TOM:
        this.accentColor = new Color(0xF57C00);
        break; // Orange
      case CLAP:
        this.accentColor = new Color(0xD32F2F);
        break; // Darker Red
      case COWBELL:
        this.accentColor = new Color(0xFFC107);
        break; // Amber Yellow
      case CRASH:
        this.accentColor = new Color(0xE0E0E0);
        break; // Light Gray/White
      case RIMSHOT:
        this.accentColor = new Color(0xFF3D00);
        break; // Neon Orange-Red
      case CONGA:
        this.accentColor = new Color(0xFFEA00);
        break; // Neon Yellow
      default:
        this.accentColor = Color.ORANGE;
    }

    Theme theme = Theme.getInstance();
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(theme.scale(320), theme.scale(220)));
    setMaximumSize(new Dimension(theme.scale(320), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Header
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x1F1F1F)); // Matte black/dark casing
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));

    JLabel nameLabel = new JLabel(name);
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
    header.add(btnPanel, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // Waveform Display on Top
    waveformPanel = new WaveformDisplayPanel();
    add(waveformPanel, BorderLayout.CENTER);

    // Knobs on Bottom
    int numKnobs = paramNames.length;
    JPanel knobRow = new JPanel(new GridLayout(1, numKnobs, theme.scale(2), 0));
    knobRow.setBackground(theme.BG_DARK);
    knobRow.setPreferredSize(new Dimension(0, theme.scale(68)));
    knobRow.setBorder(
        BorderFactory.createEmptyBorder(
            theme.scale(4), theme.scale(6), theme.scale(4), theme.scale(6)));

    knobs = new KnobPanel[numKnobs];
    for (int k = 0; k < numKnobs; ++k) {
      final int pid = paramIds[k];
      knobs[k] = new KnobPanel(paramNames[k], params[pid], makeFormatter(pid), accentColor);
      knobs[k].addChangeListener(
          e -> {
            if (updatingFromBackend) return;
            int idx = findKnobIndex(pid, paramIds);
            params[pid] = knobs[idx].getValue();
            sendParam(pid, params[pid]);
            waveformPanel.repaint();
          });
      knobRow.add(knobs[k]);
    }
    add(knobRow, BorderLayout.SOUTH);
  }

  private int findKnobIndex(int paramId, int[] paramIds) {
    for (int i = 0; i < paramIds.length; ++i) {
      if (paramIds[i] == paramId) return i;
    }
    return 0;
  }

  public void updateParam(int paramId, double value) {
    if (paramId < 0 || paramId >= params.length) return;
    updatingFromBackend = true;
    params[paramId] = value;
    // Map backend paramId to the UI knob index
    int ki = -1;
    for (int k = 0; k < knobs.length; ++k) {
      if (getParamIdAtKnobIndex(k) == paramId) {
        ki = k;
        break;
      }
    }
    if (ki >= 0 && ki < knobs.length && knobs[ki] != null) {
      knobs[ki].setValue(value);
    }
    updatingFromBackend = false;
    waveformPanel.repaint();
  }

  protected int getParamIdAtKnobIndex(int knobIndex) {
    if (knobIndex >= 0 && knobIndex < paramIds.length) {
      return paramIds[knobIndex];
    }
    return 0;
  }

  protected AbstractDevicePanel.ValueFormatter makeFormatter(int paramId) {
    return norm -> {
      switch (type) {
        case KICK:
          switch (paramId) {
            case 0:
              return String.format("%.0f Hz", 40.0 + norm * 40.0);
            case 1:
              return formatMsOrS(0.05 * Math.pow(20.0, norm));
            case 2:
              return String.format("%.0f ms", (0.01 + norm * 0.14) * 1000.0);
            case 3:
              return String.format("%.0f Hz", norm * 300.0);
            case 4:
            case 5:
              return String.format("%.0f%%", norm * 100.0);
            case 6:
              return formatDb(norm);
          }
          break;
        case SNARE:
          switch (paramId) {
            case 0:
              return String.format("%.0f Hz", 100.0 + norm * 150.0);
            case 1:
              return formatMsOrS(0.05 * Math.pow(10.0, norm));
            case 2:
              return String.format("%.0f%%", norm * 100.0);
            case 3:
              return formatMsOrS(0.05 * Math.pow(20.0, norm));
            case 4:
              return String.format("%.0f Hz", 800.0 * Math.pow(10.0, norm));
            case 5:
              return String.format("%.0f%%", norm * 100.0);
            case 6:
              return formatDb(norm);
          }
          break;
        case HAT:
          switch (paramId) {
            case 0:
              return formatMsOrS(0.02 * Math.pow(40.0, norm));
            case 1:
              return String.format("%.0f Hz", 3000.0 + norm * 9000.0);
            case 2:
              return String.format("%.0f Hz", 6000.0 + norm * 9000.0);
            case 3:
              return String.format("%.0f%%", norm * 100.0);
            case 4:
              return formatDb(norm);
          }
          break;
        case TOM:
          switch (paramId) {
            case 0:
              return String.format("%.0f Hz", 70.0 + norm * 130.0);
            case 1:
              return formatMsOrS(0.1 * Math.pow(15.0, norm));
            case 2:
              return String.format("%.0f ms", (0.02 + norm * 0.28) * 1000.0);
            case 3:
              return String.format("%.0f Hz", norm * 100.0);
            case 4:
              return String.format("%.0f%%", norm * 100.0);
            case 5:
              return formatDb(norm);
          }
          break;
        case CLAP:
          switch (paramId) {
            case 0:
              return formatMsOrS(0.05 * Math.pow(20.0, norm));
            case 1:
              return String.format("%.0f Hz", 500.0 * Math.pow(6.0, norm));
            case 2:
              return String.format("%.1f ms", (0.005 + norm * 0.015) * 1000.0);
            case 3:
              return formatDb(norm);
          }
          break;
        case COWBELL:
          switch (paramId) {
            case 0:
              return String.format("%.0f Hz", 400.0 + norm * 300.0);
            case 1:
              return formatMsOrS(0.05 * Math.pow(10.0, norm));
            case 2:
              return String.format("%.2fx", 1.40 + norm * 0.20);
            case 3:
              return formatDb(norm);
          }
          break;
        case CRASH:
          switch (paramId) {
            case 0:
              return formatMsOrS(0.2 * Math.pow(15.0, norm));
            case 1:
              return String.format("%.0f%%", norm * 100.0);
            case 2:
              return String.format("%.0f Hz", 150.0 + norm * 250.0);
            case 3:
              return formatDb(norm);
          }
          break;
        case RIMSHOT:
          switch (paramId) {
            case 0:
              return String.format("%.0f Hz", 200.0 + norm * 300.0);
            case 1:
              return String.format("%.1f ms", (0.01 + norm * 0.09) * 1000.0);
            case 2:
              return formatDb(norm);
          }
          break;
        case CONGA:
          switch (paramId) {
            case 0:
              return String.format("%.0f Hz", 150.0 + norm * 200.0);
            case 1:
              return formatMsOrS(0.05 * Math.pow(16.0, norm));
            case 2:
              return String.format("%.0f ms", (0.01 + norm * 0.14) * 1000.0);
            case 3:
              return String.format("%.0f Hz", norm * 120.0);
            case 4:
              return formatDb(norm);
          }
          break;
      }
      return String.format("%.2f", norm);
    };
  }

  private String formatMsOrS(double seconds) {
    double ms = seconds * 1000.0;
    if (ms < 1000.0) {
      return String.format("%.0f ms", ms);
    } else {
      return String.format("%.2f s", seconds);
    }
  }

  private String formatDb(double norm) {
    if (norm <= 0.001) return "-inf dB";
    return String.format("%.1f dB", 20.0 * Math.log10(norm));
  }

  /** Renders the simulated 1-shot waveform instantly reacting to parameters. */
  private class WaveformDisplayPanel extends JPanel {
    private final float[] simBuffer = new float[300];
    private final Random random = new Random();

    public WaveformDisplayPanel() {
      setBackground(Theme.getInstance().BG_DARKER);
    }

    private void generateSimulatedWaveform() {
      double phase = 0.0;
      for (int i = 0; i < 300; ++i) {
        double t = (double) i / 300.0;
        double val = 0.0;

        switch (type) {
          case KICK:
            {
              double pitch = params[0];
              double decay = params[1];
              double pDecay = params[2];
              double pDepth = params[3];
              double click = params[4];
              double drive = params[5];

              double env = Math.exp(-t * (2.0 + (1.0 - decay) * 12.0));
              double pEnv = Math.exp(-t * (10.0 + (1.0 - pDecay) * 40.0));
              // Demo frequency C4 (261.63 Hz)
              double baseFreq = 261.63 * Math.pow(2.0, (pitch - 0.5));
              double f = baseFreq + pEnv * pDepth * 300.0;
              phase += f * 0.12 * (1.0 / 300.0);

              double sine = Math.sin(2.0 * Math.PI * phase);
              double clickNoise = (random.nextFloat() * 2.0f - 1.0f) * click * Math.exp(-t * 80.0);
              double raw = sine * env + clickNoise;
              val = Math.tanh(raw * (1.0 + drive * 4.0));
              break;
            }
          case SNARE:
            {
              double pitch = params[0];
              double decay = params[1];
              double noiseLvl = params[2];
              double noiseDec = params[3];
              double mix = params[5];

              double envSkin = Math.exp(-t * (4.0 + (1.0 - decay) * 20.0));
              double envNoise = Math.exp(-t * (2.0 + (1.0 - noiseDec) * 15.0));

              // Demo frequency C4 (261.63 Hz)
              double baseFreq = 261.63 * Math.pow(2.0, (pitch - 0.5));
              double tone =
                  Math.sin(2.0 * Math.PI * baseFreq * 0.005 * t)
                      + 0.5 * Math.sin(2.0 * Math.PI * baseFreq * 1.6 * 0.005 * t);
              double noise = (random.nextFloat() * 2.0f - 1.0f) * envNoise * noiseLvl;
              val = (1.0 - mix) * tone * envSkin + mix * noise;
              break;
            }
          case HAT:
            {
              double decay = params[0];
              double env = Math.exp(-t * (10.0 + (1.0 - decay) * 40.0));
              double noise = (random.nextFloat() * 2.0f - 1.0f) * env;
              double metal = (Math.sin(2.0 * Math.PI * 400.0 * t) > 0.0 ? 1.0 : -1.0) * env;
              val = 0.7 * noise + 0.3 * metal;
              break;
            }
          case TOM:
            {
              double pitch = params[0];
              double decay = params[1];
              double pDecay = params[2];
              double pDepth = params[3];
              double click = params[4];

              double env = Math.exp(-t * (2.0 + (1.0 - decay) * 10.0));
              double pEnv = Math.exp(-t * (5.0 + (1.0 - pDecay) * 25.0));
              // Demo frequency C4 (261.63 Hz)
              double baseFreq = 261.63 * Math.pow(2.0, (pitch - 0.5));
              double f = baseFreq + pEnv * pDepth * 100.0;
              phase += f * 0.08 * (1.0 / 300.0);

              double sine = Math.sin(2.0 * Math.PI * phase);
              double clickNoise = (random.nextFloat() * 2.0f - 1.0f) * click * Math.exp(-t * 90.0);
              val = sine * env + clickNoise;
              break;
            }
          case CLAP:
            {
              double decay = params[0];
              double envMain = Math.exp(-t * (2.0 + (1.0 - decay) * 15.0));
              double trig = 0.0;
              if (t < 0.15) {
                trig += Math.exp(-t * 150.0);
                trig += (t > 0.03) ? Math.exp(-(t - 0.03) * 150.0) : 0.0;
                trig += (t > 0.06) ? Math.exp(-(t - 0.06) * 150.0) : 0.0;
              }
              double noise = (random.nextFloat() * 2.0f - 1.0f);
              val = noise * (trig * 0.8 + envMain * 0.5);
              break;
            }
          case COWBELL:
            {
              double pitch = params[0];
              double decay = params[1];
              double detune = params[2];

              double env = Math.exp(-t * (3.0 + (1.0 - decay) * 15.0));
              // Demo frequency C4 (261.63 Hz)
              double f1 = 261.63 * Math.pow(2.0, (pitch - 0.5));
              double detuneRatio = 1.40 + detune * 0.20;
              double f2 = f1 * detuneRatio;

              double saw1 = Math.sin(2.0 * Math.PI * f1 * 0.015 * t) > 0.0 ? 1.0 : -1.0;
              double saw2 = Math.sin(2.0 * Math.PI * f2 * 0.015 * t) > 0.0 ? 1.0 : -1.0;
              val = 0.5 * (saw1 + saw2) * env;
              break;
            }
          case CRASH:
            {
              double decay = params[0];
              double tone = params[1];
              double tension = params[2];

              double env = Math.exp(-t * (0.5 + (1.0 - decay) * 6.0));
              // Demo frequency C4 (261.63 Hz)
              double baseFreq = 261.63 * Math.pow(2.0, (tension - 0.5));

              double metal = 0.0;
              double ratios[] = {1.0, 1.8, 1.48, 2.54, 3.89, 4.15};
              for (double ratio : ratios) {
                metal += (Math.sin(2.0 * Math.PI * baseFreq * ratio * 0.01 * t) > 0.0 ? 1.0 : -1.0);
              }
              metal /= 6.0;

              double noise = (random.nextFloat() * 2.0f - 1.0f);
              val = ((1.0 - tone) * metal + tone * noise) * env;
              break;
            }
          case RIMSHOT:
            {
              double pitch = params[0];
              double decay = params[1];

              double env = Math.exp(-t * (15.0 + (1.0 - decay) * 60.0));
              // Demo frequency C4 (261.63 Hz)
              double f = 261.63 * Math.pow(2.0, (pitch - 0.5));

              double component =
                  Math.sin(2.0 * Math.PI * f * 0.015 * t)
                      + 0.8 * Math.sin(2.0 * Math.PI * f * 2.6 * 0.015 * t)
                      + 0.4 * Math.sin(2.0 * Math.PI * f * 4.6 * 0.015 * t);
              val = component * env;
              break;
            }
          case CONGA:
            {
              double pitch = params[0];
              double decay = params[1];
              double pDecay = params[2];
              double pDepth = params[3];

              double env = Math.exp(-t * (3.0 + (1.0 - decay) * 15.0));
              double pEnv = Math.exp(-t * (8.0 + (1.0 - pDecay) * 35.0));
              // Demo frequency C4 (261.63 Hz)
              double baseFreq = 261.63 * Math.pow(2.0, (pitch - 0.5));
              double f = baseFreq + pEnv * pDepth * 120.0;
              phase += f * 0.08 * (1.0 / 300.0);

              double sine = Math.sin(2.0 * Math.PI * phase);
              double clickNoise = (random.nextFloat() * 2.0f - 1.0f) * 0.12 * Math.exp(-t * 100.0);
              val = sine * env + clickNoise;
              break;
            }
        }
        simBuffer[i] = (float) val;
      }
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      generateSimulatedWaveform();

      Graphics2D g2 = (Graphics2D) g;
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

      int w = getWidth();
      int h = getHeight();
      int centerY = h / 2;
      int innerH = h - 16;

      g2.setColor(new Color(255, 255, 255, 12));
      g2.drawLine(0, centerY, w, centerY);

      g2.setColor(accentColor);
      g2.setStroke(new BasicStroke(1.5f));

      for (int i = 0; i < simBuffer.length - 1; ++i) {
        int x1 = i * w / simBuffer.length;
        int x2 = (i + 1) * w / simBuffer.length;
        int y1 = (int) (simBuffer[i] * (innerH / 2));
        int y2 = (int) (simBuffer[i + 1] * (innerH / 2));

        g2.drawLine(x1, centerY - y1, x2, centerY - y2);
      }
    }
  }
}
