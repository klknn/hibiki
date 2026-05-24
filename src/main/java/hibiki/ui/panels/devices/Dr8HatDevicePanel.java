package hibiki.ui.panels.devices;

public class Dr8HatDevicePanel extends Dr8DevicePanel {
  public Dr8HatDevicePanel(int trackIndex, int pluginIndex) {
    super(
        trackIndex,
        pluginIndex,
        DrumType.HAT,
        5,
        "DR8 Hat",
        new String[] {"Decay", "HPF", "BPF", "Tension", "Volume"},
        new int[] {0, 1, 2, 3, 4});

    // Default values matching the C++ plugin defaults
    params[0] = 0.3;
    params[1] = 0.5;
    params[2] = 0.5;
    params[3] = 0.4;
    params[4] = 0.7;
  }
}
