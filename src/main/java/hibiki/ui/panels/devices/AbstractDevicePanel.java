package hibiki.ui.panels.devices;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import java.awt.*;
import javax.swing.*;

/**
 * Base class for all built-in device panels. Provides common fields (trackIndex, pluginIndex,
 * params[], updatingFromBackend), backend communication (sendParam/sendRemove), and arc-knob
 * rendering.
 */
public abstract class AbstractDevicePanel extends JPanel {

  protected final int trackIndex;
  protected final int pluginIndex;
  public final double[] params;
  protected boolean updatingFromBackend = false;

  /** Callback invoked when user clicks Mod button; set by PluginPane wrapper. */
  public Runnable modToggleCallback;

  @FunctionalInterface
  public interface ParamSender {
    void send(int paramId, double value);
  }

  protected ParamSender customParamSender = null;

  public void setCustomParamSender(ParamSender sender) {
    this.customParamSender = sender;
  }

  protected AbstractDevicePanel(int trackIndex, int pluginIndex, int totalParams) {
    this.trackIndex = trackIndex;
    this.pluginIndex = pluginIndex;
    this.params = new double[totalParams];
  }

  public void handleParamChange(int paramId, double value) {
    updateParam(paramId, value);
  }

  public void updateParam(int paramId, double value) {
    if (paramId >= 0 && paramId < params.length) {
      params[paramId] = value;
    }
  }

  // ─── Backend communication ──────────────────────────────────────

  public void sendParam(int paramId, double value) {
    if (customParamSender != null) {
      customParamSender.send(paramId, value);
      return;
    }
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setPlugin(
                    PluginCmd.newBuilder()
                        .setAction(PluginCmd.Action.ACTION_SET_PARAM)
                        .setTarget(
                            EntityRef.newBuilder()
                                .setTrackIndex(trackIndex)
                                .setPluginIndex(pluginIndex))
                        .setParamId(paramId)
                        .setParamValue((float) value))
                .build());
  }

  public void sendRemove() {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setPlugin(
                    PluginCmd.newBuilder()
                        .setAction(PluginCmd.Action.ACTION_REMOVE)
                        .setTarget(
                            EntityRef.newBuilder()
                                .setTrackIndex(trackIndex)
                                .setPluginIndex(pluginIndex)))
                .build());
  }

  // ─── Arc knob rendering ─────────────────────────────────────────

  /**
   * Paint a standard arc knob. Shared by all device panels. Only the accent color differs per
   * device.
   */
  public static void paintArcKnob(Graphics2D g2, int w, int h, double value, Color accentColor) {
    g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
    int sz = Math.min(w, h) - 4;
    int kx = (w - sz) / 2, ky = (h - sz) / 2;
    g2.setColor(new Color(0x3A3A3A));
    g2.setStroke(new BasicStroke(3.0f, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND));
    g2.drawArc(kx, ky, sz, sz, 225, -270);
    g2.setColor(accentColor);
    g2.drawArc(kx, ky, sz, sz, 225, (int) (-270 * value));
    g2.setColor(new Color(0xEEEEEE));
    double angle = Math.toRadians(225 - 270 * value);
    int cx = kx + sz / 2 + (int) ((sz / 2 - 2) * Math.cos(angle));
    int cy = ky + sz / 2 - (int) ((sz / 2 - 2) * Math.sin(angle));
    g2.fillOval(cx - 2, cy - 2, 5, 5);
    g2.dispose();
  }

  /** Functional interface for formatting normalized knob values to display strings. */
  @FunctionalInterface
  public interface ValueFormatter {
    String format(double value);
  }
}
