package hibiki.ui;

import static org.junit.Assert.*;

import org.junit.Test;

public class SessionViewIpcTest {

  @Test
  public void testConstruction() {
    SessionView sv = new SessionView();
    SessionViewIpc ipc = new SessionViewIpc(sv);
    assertNotNull(ipc);
  }

  @Test
  public void testSendLoadClip() {
    SessionView sv = new SessionView();
    SessionViewIpc ipc = new SessionViewIpc(sv);
    // This will build the FlatBuffer and attempt to send it
    // BackendManager is initialized but has no active process
    // so send will be a no-op, but FlatBuffer construction is exercised
    try {
      ipc.sendLoadClip(0, 0, "test.mid", true);
    } catch (Exception e) {
      // Expected - backend may not accept
    }
    // Verify path was stored in sessionview
    assertEquals("test.mid", sv.slotPaths.get(0)[0]);
  }

  @Test
  public void testSendSetClipLoop() {
    SessionView sv = new SessionView();
    SessionViewIpc ipc = new SessionViewIpc(sv);
    try {
      ipc.sendSetClipLoop(0, 0, true);
    } catch (Exception e) {
      // Backend may not accept
    }
  }

  @Test
  public void testSendPlayClip() {
    SessionView sv = new SessionView();
    SessionViewIpc ipc = new SessionViewIpc(sv);
    try {
      ipc.sendPlayClip(0, 0);
    } catch (Exception e) {
      // Backend may not accept
    }
  }

  @Test
  public void testSendStopTrack() {
    SessionView sv = new SessionView();
    SessionViewIpc ipc = new SessionViewIpc(sv);
    try {
      ipc.sendStopTrack(0);
    } catch (Exception e) {
      // Backend may not accept
    }
  }

  @Test
  public void testSendPlayScene() {
    SessionView sv = new SessionView();
    SessionViewIpc ipc = new SessionViewIpc(sv);
    try {
      ipc.sendPlayScene(0);
    } catch (Exception e) {
      // Backend may not accept
    }
  }

  @Test
  public void testSendDeleteClip() {
    SessionView sv = new SessionView();
    SessionViewIpc ipc = new SessionViewIpc(sv);
    // First set a path
    sv.slotPaths.get(0)[0] = "test.mid";
    try {
      ipc.sendDeleteClip(0, 0);
    } catch (Exception e) {
      // Backend may not accept
    }
    // Verify optimistic clear
    assertNull(sv.slotPaths.get(0)[0]);
  }
}
