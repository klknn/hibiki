package hibiki.ui.panels.devices;

public class Dr8CongaDevicePanel extends Dr8DevicePanel {
  public Dr8CongaDevicePanel(int trackIndex, int pluginIndex) {
    super(
        trackIndex,
        pluginIndex,
        DrumType.CONGA,
        5,
        "DR8 Conga",
        new String[] {"Pitch", "Decay", "P-Decay", "P-Depth", "Volume"},
        new int[] {0, 1, 2, 3, 4});

    // Default values matching the C++ plugin defaults
    params[0] = 0.3;
    params[1] = 0.4;
    params[2] = 0.4;
    params[3] = 0.33;
    params[4] = 0.7;
  }
}
