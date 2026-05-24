package hibiki.ui.panels.devices;

public class Dr8CowbellDevicePanel extends Dr8DevicePanel {
  public Dr8CowbellDevicePanel(int trackIndex, int pluginIndex) {
    super(
        trackIndex,
        pluginIndex,
        DrumType.COWBELL,
        4,
        "DR8 Cowbell",
        new String[] {"Pitch", "Decay", "Detune", "Volume"},
        new int[] {0, 1, 2, 3});

    // Default values matching the C++ plugin defaults
    params[0] = 0.466667;
    params[1] = 0.3;
    params[2] = 0.4;
    params[3] = 0.7;
  }
}
