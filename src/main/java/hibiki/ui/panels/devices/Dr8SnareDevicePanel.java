package hibiki.ui.panels.devices;

public class Dr8SnareDevicePanel extends Dr8DevicePanel {
  public Dr8SnareDevicePanel(int trackIndex, int pluginIndex) {
    super(
        trackIndex,
        pluginIndex,
        DrumType.SNARE,
        7,
        "DR8 Snare",
        new String[] {"Pitch", "Decay", "Noise", "N-Decay", "N-HPF", "Mix", "Volume"},
        new int[] {0, 1, 2, 3, 4, 5, 6});

    // Default values matching the C++ plugin defaults
    params[0] = 0.333333;
    params[1] = 0.3;
    params[2] = 0.5;
    params[3] = 0.4;
    params[4] = 0.4;
    params[5] = 0.5;
    params[6] = 0.7;
  }
}
