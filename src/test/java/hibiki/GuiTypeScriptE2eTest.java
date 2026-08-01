package hibiki;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.concurrent.TimeUnit;
import org.junit.Test;

/** Exercises TypeScript execution through the same Java launcher used by {@code bazel run}. */
public class GuiTypeScriptE2eTest {
  @Test
  public void runsTypeScriptFileThroughGuiLauncher() throws Exception {
    Path runfiles = Path.of(System.getenv("TEST_SRCDIR"));
    Path launcher = runfiles.resolve("_main/hibiki-gui-java");
    Path script = runfiles.resolve("_main/testdata/typescript-smoke.ts");
    ProcessBuilder builder =
        new ProcessBuilder(launcher.toString(), "--run-typescript", script.toString());
    builder.redirectErrorStream(true);
    builder.environment().put("JAVA_RUNFILES", runfiles.toString());
    Process process = builder.start();

    boolean completed = process.waitFor(20, TimeUnit.SECONDS);
    if (!completed) {
      process.destroyForcibly();
      process.waitFor(5, TimeUnit.SECONDS);
    }
    assertTrue("The GUI script mode must exit within 20 seconds", completed);
    String output = new String(process.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
    assertEquals("The GUI script mode must succeed:\n" + output, 0, process.exitValue());
    assertTrue(
        "The TypeScript smoke script must execute:\n" + output,
        output.contains("typescript-e2e-ok"));
  }
}
