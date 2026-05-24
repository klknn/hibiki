package hibiki.ui.panels.devices;

public class Dr8ClapDevicePanel extends Dr8DevicePanel {
  public Dr8ClapDevicePanel(int trackIndex, int pluginIndex) {
    super(
        trackIndex,
        pluginIndex,
        DrumType.CLAP,
        4,
        "DR8 Clap",
        new String[] {"Decay", "Filter", "Spread", "Volume"},
        new int[] {0, 1, 2, 3});

    // Default values matching the C++ plugin defaults
    params[0] = 0.3;
    params[1] = 0.3;
    params[2] = 0.466667;
    params[3] = 0.7;
  }
}
