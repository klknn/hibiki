package hibiki;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Logger;

public class EngineProcess {
  private static final Logger LOG = Logger.getLogger(EngineProcess.class.getName());
  private Process backendProcess;
  private List<String> engineFlags = new ArrayList<>();

  public void setEngineFlags(List<String> flags) {
    this.engineFlags = new ArrayList<>(flags);
  }

  private List<String> buildEngineCommand(String binaryPath) {
    List<String> cmd = new ArrayList<>();
    cmd.add(binaryPath);
    cmd.addAll(engineFlags);
    boolean hasThreshold = engineFlags.stream().anyMatch(f -> f.startsWith("--stderrthreshold"));
    if (!hasThreshold) {
      cmd.add("--stderrthreshold=0");
    }
    return cmd;
  }

  public void start() throws IOException {
    String os = System.getProperty("os.name").toLowerCase();
    boolean isWindows = os.contains("win");
    String binaryName = isWindows ? "hbk-play.exe" : "hbk-play";

    String hbkPlayPath = findBinary(binaryName);
    if (hbkPlayPath == null) {
      LOG.warning(binaryName + " not found, defaulting to ./" + binaryName);
      hbkPlayPath = "./" + binaryName;
    } else {
      LOG.info("Found " + binaryName + " at " + hbkPlayPath);
    }

    ProcessBuilder pb = new ProcessBuilder(buildEngineCommand(hbkPlayPath));
    backendProcess = pb.start();
  }

  public synchronized void terminateProcess() {
    if (backendProcess != null && backendProcess.isAlive()) {
      LOG.info("Terminating backend process...");
      backendProcess.destroy();
      try {
        if (!backendProcess.waitFor(2, java.util.concurrent.TimeUnit.SECONDS)) {
          backendProcess.destroyForcibly();
        }
      } catch (InterruptedException e) {
        backendProcess.destroyForcibly();
      }
    }
    backendProcess = null;
  }

  public InputStream getInputStream() {
    return backendProcess != null ? backendProcess.getInputStream() : null;
  }

  public OutputStream getOutputStream() {
    return backendProcess != null ? backendProcess.getOutputStream() : null;
  }

  public InputStream getErrorStream() {
    return backendProcess != null ? backendProcess.getErrorStream() : null;
  }

  private String findBinary(String binaryName) {
    if (new File("./engine/" + binaryName).exists()) return "./engine/" + binaryName;
    if (new File("./" + binaryName).exists()) return "./" + binaryName;

    File dir = new File(".").getAbsoluteFile();
    for (int i = 0; i < 10; i++) {
      if (dir == null) break;

      File binEngine = new File(dir, "bazel-bin/engine/" + binaryName);
      if (binEngine.exists()) return binEngine.getAbsolutePath();
      File bin = new File(dir, "bazel-bin/" + binaryName);
      if (bin.exists()) return bin.getAbsolutePath();

      File outWin = new File(dir, "bazel-out/x64_windows-opt/bin/engine/" + binaryName);
      if (outWin.exists()) return outWin.getAbsolutePath();
      File outLinux = new File(dir, "bazel-out/k8-opt/bin/engine/" + binaryName);
      if (outLinux.exists()) return outLinux.getAbsolutePath();

      File rf = new File(dir, binaryName + ".runfiles/_main/" + binaryName);
      if (rf.exists()) return rf.getAbsolutePath();

      dir = dir.getParentFile();
    }

    String runfilesDir = System.getenv("RUNFILES_DIR");
    if (runfilesDir != null) {
      File f1 = new File(runfilesDir, "_main/engine/" + binaryName);
      if (f1.exists()) return f1.getAbsolutePath();
      File f2 = new File(runfilesDir, "hibiki/engine/" + binaryName);
      if (f2.exists()) return f2.getAbsolutePath();
    }

    return null;
  }
}
