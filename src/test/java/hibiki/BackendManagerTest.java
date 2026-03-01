package hibiki;

import hibiki.ipc.Response;
import com.google.flatbuffers.FlatBufferBuilder;
import org.junit.Test;
import static org.junit.Assert.*;
import java.io.File;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

public class BackendManagerTest {
  @Test
  public void testBackendCommunication() throws Exception {
    BackendManager backend = BackendManager.getInstance();
    backend.start();
    System.err.println("DEBUG_TEST: CWD=" + new File(".").getAbsolutePath());
    System.err.println("DEBUG_TEST: RUNFILES_DIR=" + System.getenv("RUNFILES_DIR"));
    
    // Debug: List testdata
    File td = new File("testdata");
    if (td.exists()) {
        System.err.println("DEBUG_TEST: testdata exists. Contents:");
        for (String f : td.list()) System.err.println("  " + f);
    } else {
        System.err.println("DEBUG_TEST: testdata does not exist at CWD");
    }

    CompletableFuture<hibiki.ipc.ParamList> paramListFuture = new CompletableFuture<>();
    CompletableFuture<String> logFuture = new CompletableFuture<>();

    backend.addNotificationListener(notification -> {
      System.out.println("Received notification type: " + notification.responseType());
      if (notification.responseType() == Response.ParamList) {
        paramListFuture.complete((hibiki.ipc.ParamList) notification.response(new hibiki.ipc.ParamList()));
      } else if (notification.responseType() == Response.Log) {
        hibiki.ipc.Log log = (hibiki.ipc.Log) notification.response(new hibiki.ipc.Log());
        System.out.println("Backend Log: " + log.message());
        logFuture.complete(log.message());
      } else if (notification.responseType() == Response.Acknowledge) {
        hibiki.ipc.Acknowledge ack = (hibiki.ipc.Acknowledge) notification.response(new hibiki.ipc.Acknowledge());
        System.out.println("Backend Ack: " + ack.commandType() + " success=" + ack.success());
      }
    });

    // Wait for backend to start
    Thread.sleep(1000);

    // Send LoadPlugin request for Dexed
    File vstFile = findTestData("testdata/Dexed.vst3");
    assertTrue("Dexed VST3 should exist", vstFile != null && vstFile.exists());
    String vstPath = vstFile.getAbsolutePath();

    System.out.println("Sending LoadPlugin request for " + vstPath);
    FlatBufferBuilder builder = new FlatBufferBuilder(1024);
    int pathOff = builder.createString(vstPath);
    int loadPluginOff = hibiki.ipc.LoadPlugin.createLoadPlugin(builder, 0, pathOff, 0);
    int requestOff = hibiki.ipc.Request.createRequest(builder, hibiki.ipc.Command.LoadPlugin, loadPluginOff);
    builder.finish(requestOff);

    backend.sendRequest(builder);

    try {
      // Verify receipt of ParamList
      hibiki.ipc.ParamList params = paramListFuture.get(10, TimeUnit.SECONDS);
      assertNotNull("Should receive ParamList notification", params);
      assertTrue("Should have at least one parameter", params.paramsLength() > 0);
      assertEquals("Track index should match", 0, params.trackIndex());
      System.out.println("Successfully loaded plugin with " + params.paramsLength() + " parameters.");
    } catch (TimeoutException e) {
      if (logFuture.isDone()) {
        fail("Timed out waiting for ParamList. Backend said: " + logFuture.get());
      } else {
        fail("Timed out waiting for ParamList or Log.");
      }
    }
  }

  private File findTestData(String path) {
    if (new File(path).exists()) return new File(path);
    
    File dir = new File(".").getAbsoluteFile();
    for (int i = 0; i < 10; i++) {
        if (dir == null) break;
        File f = new File(dir, path);
        if (f.exists()) return f;
        
        // Try in runfiles
        File rf1 = new File(dir, "backend_manager_test.runfiles/_main/" + path);
        if (rf1.exists()) return rf1;
        File rf2 = new File(dir, "backend_manager_test.runfiles/hibiki/" + path);
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
