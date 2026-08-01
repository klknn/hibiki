package hibiki.ui;

import static org.junit.Assert.assertTrue;

import hibiki.BackendManager;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import org.junit.Test;

/** Verifies that the shipped acid-house SDK example produces audible bounced audio. */
public class AcidHouseSdkE2eTest {
  @Test
  public void acidHouseExampleCreatesAudibleBounce() throws Exception {
    BackendManager backend = BackendManager.getInstance();
    CompletableFuture<String> bounce = new CompletableFuture<>();
    backend.addNotificationListener(
        notification -> {
          if (notification.getResponseCase()
              == hibiki.pb.notifications.Notification.ResponseCase.BOUNCE_FINISHED) {
            var finished = notification.getBounceFinished();
            if (finished.getSuccess()) bounce.complete(finished.getPath());
            else
              bounce.completeExceptionally(
                  new AssertionError("Bounce failed: " + finished.getPath()));
          }
        });
    backend.start();
    try {
      Path script = Path.of(System.getenv("TEST_SRCDIR"), "_main/examples/sdk/acid-house.ts");
      JsScriptRunner.run(script);
      File output = new File(bounce.get(15, TimeUnit.SECONDS));
      assertTrue(
          "Acid-house example must create a WAV file", output.isFile() && output.length() > 44);
      byte[] wav = Files.readAllBytes(output.toPath());
      boolean audible = false;
      for (int i = 44; i < wav.length; i++) {
        if (wav[i] != 0) {
          audible = true;
          break;
        }
      }
      assertTrue("Acid-house example must produce non-silent audio", audible);
    } finally {
      backend.stop();
    }
  }
}
