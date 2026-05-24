package hibiki.ui.panels.devices;

public class Dr8TomDevicePanel extends Dr8DevicePanel {
  public Dr8TomDevicePanel(int trackIndex, int pluginIndex) {
    super(
        trackIndex,
        pluginIndex,
        DrumType.TOM,
        6,
        "DR8 Tom",
        new String[] {"Pitch", "Decay", "P-Decay", "P-Depth", "Click", "Volume"},
        new int[] {0, 1, 2, 3, 4, 5});

    // Default values matching the C++ plugin defaults
    params[0] = 0.3;
    params[1] = 0.4;
    params[2] = 0.4;
    params[3] = 0.4;
    params[4] = 0.15;
    params[5] = 0.7;
  }
}
