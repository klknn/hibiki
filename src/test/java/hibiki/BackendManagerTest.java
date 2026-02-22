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
    File vstFile = new File("testdata/Dexed.vst3");
    assertTrue("Dexed VST3 should exist at " + vstFile.getAbsolutePath(), vstFile.exists());
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
}
