package hibiki.ui;

import static org.junit.Assert.*;

import hibiki.pb.commands.PluginCmd;
import hibiki.pb.commands.Request;
import hibiki.pb.core.EntityRef;
import hibiki.pb.notifications.EditorFrameData;
import hibiki.pb.notifications.Notification;
import com.google.protobuf.ByteString;
import org.junit.Test;

/**
 * Unit tests for RemoteEditorPanel.
 * Tests proto construction and frame decoding without needing a running backend.
 */
public class RemoteEditorPanelTest {

  @Test
  public void testEditorFrameRequestConstruction() {
    // Verify the frame request is constructed correctly
    Request req = Request.newBuilder()
        .setPlugin(
            PluginCmd.newBuilder()
                .setAction(PluginCmd.Action.ACTION_GET_EDITOR_FRAME)
                .setTarget(
                    EntityRef.newBuilder()
                        .setTrackIndex(2)
                        .setPluginIndex(1)))
        .build();

    assertTrue(req.hasPlugin());
    assertEquals(PluginCmd.Action.ACTION_GET_EDITOR_FRAME,
        req.getPlugin().getAction());
    assertEquals(2, req.getPlugin().getTarget().getTrackIndex());
    assertEquals(1, req.getPlugin().getTarget().getPluginIndex());
  }

  @Test
  public void testEditorInputRequestConstruction() {
    Request req = Request.newBuilder()
        .setPlugin(
            PluginCmd.newBuilder()
                .setAction(PluginCmd.Action.ACTION_SEND_EDITOR_INPUT)
                .setTarget(
                    EntityRef.newBuilder()
                        .setTrackIndex(0)
                        .setPluginIndex(0))
                .setInputType(1)   // MOUSE_DOWN
                .setInputX(100)
                .setInputY(200)
                .setInputButton(1)
                .setInputKey(0)
                .setInputDelta(0))
        .build();

    assertTrue(req.hasPlugin());
    assertEquals(PluginCmd.Action.ACTION_SEND_EDITOR_INPUT,
        req.getPlugin().getAction());
    assertEquals(1, req.getPlugin().getInputType());
    assertEquals(100, req.getPlugin().getInputX());
    assertEquals(200, req.getPlugin().getInputY());
    assertEquals(1, req.getPlugin().getInputButton());
  }

  @Test
  public void testStopGuiRequestConstruction() {
    Request req = Request.newBuilder()
        .setPlugin(
            PluginCmd.newBuilder()
                .setAction(PluginCmd.Action.ACTION_STOP_GUI)
                .setTarget(
                    EntityRef.newBuilder()
                        .setTrackIndex(0)
                        .setPluginIndex(0)))
        .build();

    assertTrue(req.hasPlugin());
    assertEquals(PluginCmd.Action.ACTION_STOP_GUI,
        req.getPlugin().getAction());
  }

  @Test
  public void testEditorFrameDataParsing() {
    // Build a 2x2 RGBA test frame
    byte[] rgba = new byte[] {
        (byte)255, 0, 0, (byte)255,     // Red pixel
        0, (byte)255, 0, (byte)255,     // Green pixel
        0, 0, (byte)255, (byte)255,     // Blue pixel
        (byte)255, (byte)255, 0, (byte)255 // Yellow pixel
    };

    EditorFrameData frame = EditorFrameData.newBuilder()
        .setTrackIndex(0)
        .setPluginIndex(0)
        .setWidth(2)
        .setHeight(2)
        .setImageData(ByteString.copyFrom(rgba))
        .build();

    assertEquals(2, frame.getWidth());
    assertEquals(2, frame.getHeight());
    assertEquals(16, frame.getImageData().size());

    // Wrap in notification
    Notification notif = Notification.newBuilder()
        .setEditorFrameData(frame)
        .build();

    assertTrue(notif.hasEditorFrameData());
    assertEquals(0, notif.getEditorFrameData().getTrackIndex());
    assertEquals(2, notif.getEditorFrameData().getWidth());
  }

  @Test
  public void testEditorFrameDataEmpty() {
    EditorFrameData frame = EditorFrameData.newBuilder()
        .setTrackIndex(0)
        .setPluginIndex(0)
        .setWidth(0)
        .setHeight(0)
        .build();

    assertEquals(0, frame.getWidth());
    assertEquals(0, frame.getHeight());
    assertTrue(frame.getImageData().isEmpty());
  }

  @Test
  public void testAllInputTypes() {
    // Verify all input types can be set
    int[] types = {0, 1, 2, 3, 4, 5}; // MOVE, DOWN, UP, KEY_DOWN, KEY_UP, WHEEL
    for (int type : types) {
      Request req = Request.newBuilder()
          .setPlugin(
              PluginCmd.newBuilder()
                  .setAction(PluginCmd.Action.ACTION_SEND_EDITOR_INPUT)
                  .setTarget(
                      EntityRef.newBuilder()
                          .setTrackIndex(0)
                          .setPluginIndex(0))
                  .setInputType(type))
          .build();

      assertEquals(type, req.getPlugin().getInputType());
    }
  }
}
