package hibiki;

import static org.junit.Assert.*;

import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import hibiki.pb.notifications.*;
import java.io.File;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

/**
 * Integration tests for plugin hosting modes. Tests that SetPluginHostMode commands are accepted by
 * the backend and that plugins can be loaded in each mode. Requires Dexed.vst3 in testdata/.
 */
public class PluginWorkerTest {
  private BackendManager backend;

  @Before
  public void setUp() throws Exception {
    backend = BackendManager.getInstance();
    backend.start();
    Thread.sleep(1000); // Wait for backend to start
  }

  @After
  public void tearDown() {
    // Send quit to cleanly stop backend
    backend.sendRequest(
        Request.newBuilder()
            .setProject(
                ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_QUIT))
            .build());
  }

  // ─── In-Process Mode ──────────────────────────────────────────────

  @Test
  public void testSetPluginHostModeInProcess() throws Exception {
    CompletableFuture<Acknowledge> ackFuture = listenForAck("SET_PLUGIN_HOST_MODE");

    backend.sendRequest(
        Request.newBuilder()
            .setSetPluginHostMode(
                SetPluginHostMode.newBuilder()
                    .setMode(PluginHostMode.PLUGIN_HOST_IN_PROCESS))
            .build());

    Acknowledge ack = ackFuture.get(5, TimeUnit.SECONDS);
    assertNotNull("Should receive ACK for SET_PLUGIN_HOST_MODE", ack);
    assertTrue("SetPluginHostMode should succeed", ack.getSuccess());
  }

  @Test
  public void testLoadPluginInProcess() throws Exception {
    // Set mode to in-process
    sendAndWaitAck(
        Request.newBuilder()
            .setSetPluginHostMode(
                SetPluginHostMode.newBuilder()
                    .setMode(PluginHostMode.PLUGIN_HOST_IN_PROCESS))
            .build(),
        "SET_PLUGIN_HOST_MODE");

    // Load plugin
    File vstFile = findTestData("testdata/Dexed.vst3");
    if (vstFile == null || !vstFile.exists()) {
      System.out.println("SKIP: Dexed.vst3 not found, skipping in-process load test");
      return;
    }

    CompletableFuture<ParamList> paramFuture = new CompletableFuture<>();
    backend.addNotificationListener(
        n -> {
          if (n.getResponseCase() == Notification.ResponseCase.PARAM_LIST) {
            paramFuture.complete(n.getParamList());
          }
        });

    backend.sendRequest(
        Request.newBuilder()
            .setPlugin(
                PluginCmd.newBuilder()
                    .setAction(PluginCmd.Action.ACTION_LOAD)
                    .setTarget(EntityRef.newBuilder().setTrackIndex(0).setPluginIndex(0))
                    .setPath(vstFile.getAbsolutePath()))
            .build());

    try {
      ParamList params = paramFuture.get(10, TimeUnit.SECONDS);
      assertNotNull("Should receive ParamList for in-process plugin", params);
      assertTrue("Should have parameters", params.getParamsCount() > 0);
      assertEquals("Track index should match", 0, params.getTrackIndex());
      System.out.println(
          "In-process: loaded with " + params.getParamsCount() + " parameters");
    } catch (TimeoutException e) {
      fail("Timed out waiting for in-process plugin load");
    }
  }

  // ─── Sandbox Mode ─────────────────────────────────────────────────

  @Test
  public void testSetPluginHostModeSandbox() throws Exception {
    CompletableFuture<Acknowledge> ackFuture = listenForAck("SET_PLUGIN_HOST_MODE");

    backend.sendRequest(
        Request.newBuilder()
            .setSetPluginHostMode(
                SetPluginHostMode.newBuilder()
                    .setMode(PluginHostMode.PLUGIN_HOST_LOCAL_SANDBOX))
            .build());

    Acknowledge ack = ackFuture.get(5, TimeUnit.SECONDS);
    assertNotNull("Should receive ACK for sandbox mode", ack);
    assertTrue("SetPluginHostMode(SANDBOX) should succeed", ack.getSuccess());
  }

  @Test
  public void testLoadPluginSandbox() throws Exception {
    // Set mode to sandbox
    sendAndWaitAck(
        Request.newBuilder()
            .setSetPluginHostMode(
                SetPluginHostMode.newBuilder()
                    .setMode(PluginHostMode.PLUGIN_HOST_LOCAL_SANDBOX))
            .build(),
        "SET_PLUGIN_HOST_MODE");

    // Load plugin
    File vstFile = findTestData("testdata/Dexed.vst3");
    if (vstFile == null || !vstFile.exists()) {
      System.out.println("SKIP: Dexed.vst3 not found, skipping sandbox load test");
      return;
    }

    CompletableFuture<ParamList> paramFuture = new CompletableFuture<>();
    CompletableFuture<String> logFuture = new CompletableFuture<>();
    backend.addNotificationListener(
        n -> {
          if (n.getResponseCase() == Notification.ResponseCase.PARAM_LIST) {
            paramFuture.complete(n.getParamList());
          } else if (n.getResponseCase() == Notification.ResponseCase.LOG) {
            logFuture.complete(n.getLog().getMessage());
          }
        });

    backend.sendRequest(
        Request.newBuilder()
            .setPlugin(
                PluginCmd.newBuilder()
                    .setAction(PluginCmd.Action.ACTION_LOAD)
                    .setTarget(EntityRef.newBuilder().setTrackIndex(1).setPluginIndex(0))
                    .setPath(vstFile.getAbsolutePath()))
            .build());

    try {
      ParamList params = paramFuture.get(15, TimeUnit.SECONDS);
      assertNotNull("Should receive ParamList for sandbox plugin", params);
      assertTrue("Should have parameters", params.getParamsCount() > 0);
      assertEquals("Track index should match", 1, params.getTrackIndex());
      System.out.println("Sandbox: loaded with " + params.getParamsCount() + " parameters");
    } catch (TimeoutException e) {
      String msg = logFuture.isDone() ? logFuture.get() : "(no log)";
      fail("Timed out waiting for sandbox plugin load. Backend log: " + msg);
    }
  }

  // ─── Remote Mode (Real Daemon) ─────────────────────────────────────

  @Test
  public void testLoadPluginViaDaemon() throws Exception {
    // Find hbk-worker-daemon binary
    String daemonPath = findBinary("hbk-worker-daemon");
    if (daemonPath == null) {
      System.out.println("SKIP: hbk-worker-daemon binary not found");
      return;
    }

    File vstFile = findTestData("testdata/Dexed.vst3");
    if (vstFile == null || !vstFile.exists()) {
      System.out.println("SKIP: Dexed.vst3 not found, skipping daemon load test");
      return;
    }

    // Pick a free port
    int port;
    try (java.net.ServerSocket ss = new java.net.ServerSocket(0)) {
      port = ss.getLocalPort();
    }

    // Start hbk-worker-daemon
    Process daemon = null;
    try {
      ProcessBuilder pb = new ProcessBuilder(daemonPath, "--port", String.valueOf(port));
      pb.redirectErrorStream(true);
      daemon = pb.start();

      // Wait for daemon to start listening
      boolean connected = false;
      for (int attempt = 0; attempt < 20; attempt++) {
        Thread.sleep(250);
        try (java.net.Socket probe = new java.net.Socket("localhost", port)) {
          connected = true;
          break;
        } catch (java.io.IOException e) {
          // Not ready yet
        }
      }
      assertTrue("Daemon should accept connections on port " + port, connected);
      System.out.println("hbk-worker-daemon started on port " + port);

      // Tell backend to use remote mode pointing at our daemon
      sendAndWaitAck(
          Request.newBuilder()
              .setSetPluginHostMode(
                  SetPluginHostMode.newBuilder()
                      .setMode(PluginHostMode.PLUGIN_HOST_REMOTE)
                      .addRemoteHosts("localhost:" + port))
              .build(),
          "SET_PLUGIN_HOST_MODE");

      // Load a plugin through the daemon
      CompletableFuture<ParamList> paramFuture = new CompletableFuture<>();
      CompletableFuture<String> logFuture = new CompletableFuture<>();
      backend.addNotificationListener(
          n -> {
            if (n.getResponseCase() == Notification.ResponseCase.PARAM_LIST) {
              paramFuture.complete(n.getParamList());
            } else if (n.getResponseCase() == Notification.ResponseCase.LOG) {
              logFuture.complete(n.getLog().getMessage());
            }
          });

      backend.sendRequest(
          Request.newBuilder()
              .setPlugin(
                  PluginCmd.newBuilder()
                      .setAction(PluginCmd.Action.ACTION_LOAD)
                      .setTarget(EntityRef.newBuilder().setTrackIndex(2).setPluginIndex(0))
                      .setPath(vstFile.getAbsolutePath()))
              .build());

      try {
        ParamList params = paramFuture.get(15, TimeUnit.SECONDS);
        assertNotNull("Should receive ParamList via daemon", params);
        assertTrue("Should have parameters", params.getParamsCount() > 0);
        assertEquals("Track index should match", 2, params.getTrackIndex());
        System.out.println(
            "Remote daemon: loaded plugin with " + params.getParamsCount() + " parameters");
      } catch (TimeoutException e) {
        String msg = logFuture.isDone() ? logFuture.get() : "(no log from backend)";
        fail("Timed out loading plugin via daemon. Backend log: " + msg);
      }

    } finally {
      if (daemon != null) {
        daemon.destroyForcibly();
        daemon.waitFor(5, TimeUnit.SECONDS);
        System.out.println("hbk-worker-daemon stopped");
      }
    }
  }

  @Test
  public void testDaemonConnectFailureGraceful() throws Exception {
    // Set remote mode pointing at a port where nothing is listening.
    // The SetPluginHostMode command itself should still succeed (it just
    // stores the config). The failure happens at plugin load time.
    sendAndWaitAck(
        Request.newBuilder()
            .setSetPluginHostMode(
                SetPluginHostMode.newBuilder()
                    .setMode(PluginHostMode.PLUGIN_HOST_REMOTE)
                    .addRemoteHosts("localhost:1"))
            .build(),
        "SET_PLUGIN_HOST_MODE");

    File vstFile = findTestData("testdata/Dexed.vst3");
    if (vstFile == null || !vstFile.exists()) {
      System.out.println("SKIP: Dexed.vst3 not found");
      return;
    }

    CompletableFuture<String> logFuture = new CompletableFuture<>();
    backend.addNotificationListener(
        n -> {
          if (n.getResponseCase() == Notification.ResponseCase.LOG) {
            String msg = n.getLog().getMessage();
            // Skip progress logs — only complete on failure messages
            if (msg.toLowerCase().contains("fail")) {
              logFuture.complete(msg);
            }
          }
        });

    backend.sendRequest(
        Request.newBuilder()
            .setPlugin(
                PluginCmd.newBuilder()
                    .setAction(PluginCmd.Action.ACTION_LOAD)
                    .setTarget(EntityRef.newBuilder().setTrackIndex(3).setPluginIndex(0))
                    .setPath(vstFile.getAbsolutePath()))
            .build());

    // Should get a failure log rather than a crash
    try {
      String log = logFuture.get(10, TimeUnit.SECONDS);
      System.out.println("Graceful failure log: " + log);
      assertTrue("Should mention failure", log.toLowerCase().contains("fail"));
    } catch (TimeoutException e) {
      // Acceptable — backend might not send a log for connection failures,
      // but it must NOT crash. The fact that we got here means it survived.
      System.out.println("No failure log received (backend survived — OK)");
    }

    // Verify backend is still alive by switching back to in-process
    sendAndWaitAck(
        Request.newBuilder()
            .setSetPluginHostMode(
                SetPluginHostMode.newBuilder()
                    .setMode(PluginHostMode.PLUGIN_HOST_IN_PROCESS))
            .build(),
        "SET_PLUGIN_HOST_MODE");
    System.out.println("Backend survived connection failure gracefully");
  }

  // ─── Helpers ──────────────────────────────────────────────────────

  private CompletableFuture<Acknowledge> listenForAck(String commandType) {
    CompletableFuture<Acknowledge> future = new CompletableFuture<>();
    backend.addNotificationListener(
        n -> {
          if (n.getResponseCase() == Notification.ResponseCase.ACKNOWLEDGE
              && n.getAcknowledge().getCommandType().equals(commandType)) {
            future.complete(n.getAcknowledge());
          }
        });
    return future;
  }

  private void sendAndWaitAck(Request request, String commandType) throws Exception {
    CompletableFuture<Acknowledge> ackFuture = listenForAck(commandType);
    backend.sendRequest(request);
    Acknowledge ack = ackFuture.get(5, TimeUnit.SECONDS);
    assertTrue(commandType + " should succeed", ack.getSuccess());
  }

  private String findBinary(String binaryName) {
    if (new File("./" + binaryName).exists()) return "./" + binaryName;

    File dir = new File(".").getAbsoluteFile();
    for (int i = 0; i < 10; i++) {
      if (dir == null) break;
      File bin = new File(dir, "bazel-bin/" + binaryName);
      if (bin.exists()) return bin.getAbsolutePath();
      File rf = new File(dir, binaryName + ".runfiles/_main/" + binaryName);
      if (rf.exists()) return rf.getAbsolutePath();
      dir = dir.getParentFile();
    }

    String runfilesDir = System.getenv("RUNFILES_DIR");
    if (runfilesDir != null) {
      File f1 = new File(runfilesDir, "_main/" + binaryName);
      if (f1.exists()) return f1.getAbsolutePath();
      File f2 = new File(runfilesDir, "hibiki/" + binaryName);
      if (f2.exists()) return f2.getAbsolutePath();
    }

    // In java_test, data deps land relative to the test's runfiles
    String testRunfiles = System.getenv("TEST_SRCDIR");
    if (testRunfiles != null) {
      File f = new File(testRunfiles, "_main/" + binaryName);
      if (f.exists()) return f.getAbsolutePath();
    }

    return null;
  }

  private File findTestData(String path) {
    if (new File(path).exists()) return new File(path);

    File dir = new File(".").getAbsoluteFile();
    for (int i = 0; i < 10; i++) {
      if (dir == null) break;
      File f = new File(dir, path);
      if (f.exists()) return f;
      File rf1 = new File(dir, "plugin_worker_test.runfiles/_main/" + path);
      if (rf1.exists()) return rf1;
      File rf2 = new File(dir, "plugin_worker_test.runfiles/hibiki/" + path);
      if (rf2.exists()) return rf2;
      dir = dir.getParentFile();
    }

    String runfilesDir = System.getenv("RUNFILES_DIR");
    if (runfilesDir != null) {
      File f1 = new File(runfilesDir, "_main/" + path);
      if (f1.exists()) return f1;
      File f2 = new File(runfilesDir, "hibiki/" + path);
      if (f2.exists()) return f2;
    }
    return new File(path);
  }
}

