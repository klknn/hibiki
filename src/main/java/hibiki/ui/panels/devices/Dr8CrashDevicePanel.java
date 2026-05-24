package hibiki.ui.panels.devices;

public class Dr8CrashDevicePanel extends Dr8DevicePanel {
  public Dr8CrashDevicePanel(int trackIndex, int pluginIndex) {
    super(
        trackIndex,
        pluginIndex,
        DrumType.CRASH,
        4,
        "DR8 Crash",
        new String[] {"Decay", "Tone", "Tension", "Volume"},
        new int[] {0, 1, 2, 3});

    // Default values matching the C++ plugin defaults
    params[0] = 0.4;
    params[1] = 0.4;
    params[2] = 0.3;
    params[3] = 0.7;
  }
}
