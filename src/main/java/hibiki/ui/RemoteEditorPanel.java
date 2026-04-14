package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.PluginCmd;
import hibiki.pb.commands.Request;
import hibiki.pb.core.EntityRef;
import hibiki.pb.notifications.EditorFrameData;
import hibiki.pb.notifications.Notification;
import java.awt.*;
import java.awt.event.*;
import java.awt.image.BufferedImage;
import java.util.function.Consumer;
import javax.swing.*;

/**
 * Panel that displays a remote plugin's GUI by polling EditorFrame data from the backend and
 * forwarding mouse/keyboard events back.
 *
 * <p>Usage: {@code new RemoteEditorPanel(trackIndex, pluginIndex)} and add to a JDialog. The panel
 * starts polling when shown and stops when hidden.
 */
public class RemoteEditorPanel extends JPanel {
  private final int trackIndex;
  private final int pluginIndex;
  private volatile BufferedImage currentFrame;
  private Timer pollTimer;
  private final Consumer<Notification> listener;

  // Input type constants (must match C++ EditorInput::Type enum)
  private static final int INPUT_MOVE = 0;
  private static final int INPUT_DOWN = 1;
  private static final int INPUT_UP = 2;
  private static final int INPUT_KEY_DOWN = 3;
  private static final int INPUT_KEY_UP = 4;
  private static final int INPUT_WHEEL = 5;

  public RemoteEditorPanel(int trackIndex, int pluginIndex) {
    this.trackIndex = trackIndex;
    this.pluginIndex = pluginIndex;
    setPreferredSize(new Dimension(800, 600));
    setBackground(Color.BLACK);
    setFocusable(true);

    // Listen for editor frame notifications
    listener =
        notification -> {
          if (notification.hasEditorFrameData()) {
            EditorFrameData frame = notification.getEditorFrameData();
            if (frame.getTrackIndex() == trackIndex && frame.getPluginIndex() == pluginIndex) {
              updateFrame(frame);
            }
          }
        };

    // Mouse listeners for input forwarding
    addMouseListener(
        new MouseAdapter() {
          @Override
          public void mousePressed(MouseEvent e) {
            sendInput(INPUT_DOWN, e.getX(), e.getY(), e.getButton(), 0, 0);
          }

          @Override
          public void mouseReleased(MouseEvent e) {
            sendInput(INPUT_UP, e.getX(), e.getY(), e.getButton(), 0, 0);
          }
        });

    addMouseMotionListener(
        new MouseMotionAdapter() {
          @Override
          public void mouseMoved(MouseEvent e) {
            sendInput(INPUT_MOVE, e.getX(), e.getY(), 0, 0, 0);
          }

          @Override
          public void mouseDragged(MouseEvent e) {
            sendInput(INPUT_MOVE, e.getX(), e.getY(), e.getModifiersEx(), 0, 0);
          }
        });

    addMouseWheelListener(
        e -> {
          sendInput(INPUT_WHEEL, e.getX(), e.getY(), 0, 0, e.getWheelRotation());
        });

    addKeyListener(
        new KeyAdapter() {
          @Override
          public void keyPressed(KeyEvent e) {
            sendInput(INPUT_KEY_DOWN, 0, 0, 0, e.getKeyCode(), 0);
          }

          @Override
          public void keyReleased(KeyEvent e) {
            sendInput(INPUT_KEY_UP, 0, 0, 0, e.getKeyCode(), 0);
          }
        });
  }

  /** Start polling for frames and register notification listener. */
  public void startPolling() {
    BackendManager.getInstance().addNotificationListener(listener);

    // Tell backend to show editor (headless for framebuffer capture)
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setPlugin(
                    PluginCmd.newBuilder()
                        .setAction(PluginCmd.Action.ACTION_SHOW_GUI)
                        .setTarget(
                            EntityRef.newBuilder()
                                .setTrackIndex(trackIndex)
                                .setPluginIndex(pluginIndex)))
                .build());

    // Poll for frames at ~30 fps
    pollTimer = new Timer(33, e -> requestFrame());
    pollTimer.start();
  }

  /** Stop polling and release listener. */
  public void stopPolling() {
    if (pollTimer != null) {
      pollTimer.stop();
      pollTimer = null;
    }
    BackendManager.getInstance().removeNotificationListener(listener);

    // Tell backend to stop the editor
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setPlugin(
                    PluginCmd.newBuilder()
                        .setAction(PluginCmd.Action.ACTION_STOP_GUI)
                        .setTarget(
                            EntityRef.newBuilder()
                                .setTrackIndex(trackIndex)
                                .setPluginIndex(pluginIndex)))
                .build());
  }

  private void requestFrame() {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setPlugin(
                    PluginCmd.newBuilder()
                        .setAction(PluginCmd.Action.ACTION_GET_EDITOR_FRAME)
                        .setTarget(
                            EntityRef.newBuilder()
                                .setTrackIndex(trackIndex)
                                .setPluginIndex(pluginIndex)))
                .build());
  }

  private void updateFrame(EditorFrameData frame) {
    int w = frame.getWidth();
    int h = frame.getHeight();
    byte[] data = frame.getImageData().toByteArray();
    if (w <= 0 || h <= 0 || data.length < w * h * 4) return;

    BufferedImage img = new BufferedImage(w, h, BufferedImage.TYPE_INT_ARGB);
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        int i = (y * w + x) * 4;
        int r = data[i] & 0xFF;
        int g = data[i + 1] & 0xFF;
        int b = data[i + 2] & 0xFF;
        int a = data[i + 3] & 0xFF;
        img.setRGB(x, y, (a << 24) | (r << 16) | (g << 8) | b);
      }
    }
    currentFrame = img;

    // Resize panel to match plugin window if needed
    if (getPreferredSize().width != w || getPreferredSize().height != h) {
      setPreferredSize(new Dimension(w, h));
      SwingUtilities.invokeLater(
          () -> {
            revalidate();
            Window window = SwingUtilities.getWindowAncestor(this);
            if (window != null) window.pack();
          });
    }
    repaint();
  }

  private void sendInput(int type, int x, int y, int button, int key, int delta) {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setPlugin(
                    PluginCmd.newBuilder()
                        .setAction(PluginCmd.Action.ACTION_SEND_EDITOR_INPUT)
                        .setTarget(
                            EntityRef.newBuilder()
                                .setTrackIndex(trackIndex)
                                .setPluginIndex(pluginIndex))
                        .setInputType(type)
                        .setInputX(x)
                        .setInputY(y)
                        .setInputButton(button)
                        .setInputKey(key)
                        .setInputDelta(delta))
                .build());
  }

  @Override
  protected void paintComponent(Graphics g) {
    super.paintComponent(g);
    BufferedImage frame = currentFrame;
    if (frame != null) {
      g.drawImage(frame, 0, 0, getWidth(), getHeight(), null);
    } else {
      // Draw placeholder
      g.setColor(Color.DARK_GRAY);
      g.fillRect(0, 0, getWidth(), getHeight());
      g.setColor(Color.LIGHT_GRAY);
      g.setFont(new Font("SansSerif", Font.PLAIN, 14));
      String msg = "Waiting for plugin editor...";
      FontMetrics fm = g.getFontMetrics();
      int tx = (getWidth() - fm.stringWidth(msg)) / 2;
      int ty = getHeight() / 2;
      g.drawString(msg, tx, ty);
    }
  }

  /** Show this panel in a modal-less dialog. */
  public static JDialog showEditor(
      Component parent, int trackIndex, int pluginIndex, String pluginName) {
    JDialog dialog =
        new JDialog(SwingUtilities.getWindowAncestor(parent), pluginName + " (Remote Editor)");
    RemoteEditorPanel panel = new RemoteEditorPanel(trackIndex, pluginIndex);
    dialog.setContentPane(panel);
    dialog.pack();
    dialog.setLocationRelativeTo(parent);
    dialog.setDefaultCloseOperation(JDialog.DISPOSE_ON_CLOSE);

    dialog.addWindowListener(
        new WindowAdapter() {
          @Override
          public void windowOpened(WindowEvent e) {
            panel.startPolling();
            panel.requestFocusInWindow();
          }

          @Override
          public void windowClosing(WindowEvent e) {
            panel.stopPolling();
          }
        });

    dialog.setVisible(true);
    return dialog;
  }
}
