package hibiki.ui.panels.devices;

public class Dr8KickDevicePanel extends Dr8DevicePanel {
  public Dr8KickDevicePanel(int trackIndex, int pluginIndex) {
    super(
        trackIndex,
        pluginIndex,
        DrumType.KICK,
        7,
        "DR8 Kick",
        new String[] {"Pitch", "Decay", "P-Decay", "P-Depth", "Click", "Drive", "Volume"},
        new int[] {0, 1, 2, 3, 4, 5, 6});

    // Default values matching the C++ plugin defaults
    params[0] = 0.25;
    params[1] = 0.4;
    params[2] = 0.3;
    params[3] = 0.5;
    params[4] = 0.2;
    params[5] = 0.1;
    params[6] = 0.7;
  }
}
