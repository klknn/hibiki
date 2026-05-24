package hibiki.ui.panels.devices;

public class Dr8RimshotDevicePanel extends Dr8DevicePanel {
  public Dr8RimshotDevicePanel(int trackIndex, int pluginIndex) {
    super(
        trackIndex,
        pluginIndex,
        DrumType.RIMSHOT,
        3,
        "DR8 Rimshot",
        new String[] {"Pitch", "Decay", "Volume"},
        new int[] {0, 1, 2});

    // Default values matching the C++ plugin defaults
    params[0] = 0.4;
    params[1] = 0.3;
    params[2] = 0.7;
  }
}
