package hibiki;

import hibiki.ipc.Request;
import hibiki.ipc.Notification;
import com.google.flatbuffers.FlatBufferBuilder;
import java.io.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;

public class BackendManager {
    private static BackendManager instance;
    private Process backendProcess;
    private DataOutputStream out;
    private final ExecutorService executor = Executors.newCachedThreadPool();
    private final List<Consumer<Notification>> listeners = new ArrayList<>();

    private BackendManager() {
    }

    public static synchronized BackendManager getInstance() {
        if (instance == null) {
            instance = new BackendManager();
        }
        return instance;
    }

    public void start() {
        try {
            // Path to hbk-play binary
            String os = System.getProperty("os.name").toLowerCase();
            boolean isWindows = os.contains("win");
            String binaryName = isWindows ? "hbk-play.exe" : "hbk-play";
            
            String hbkPlayPath = findBinary(binaryName);
            if (hbkPlayPath == null) {
                System.err.println("Warning: " + binaryName + " not found, defaulting to ./" + binaryName);
                hbkPlayPath = "./" + binaryName;
            } else {
                System.err.println("Found " + binaryName + " at " + hbkPlayPath);
            }

            ProcessBuilder pb = new ProcessBuilder(hbkPlayPath);
            backendProcess = pb.start();
            out = new DataOutputStream(backendProcess.getOutputStream());

            // Ensure backend shuts down when GUI exits
            Runtime.getRuntime().addShutdownHook(new Thread(this::stop));

            // Start thread to read stdout (binary notifications)
            executor.submit(this::readStdout);
            // Start thread to read stderr (text logs)
            executor.submit(this::readStderr);

        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    public void stop() {
        if (backendProcess != null && backendProcess.isAlive()) {
            System.err.println("Stopping backend process...");
            backendProcess.destroy();
            try {
                if (!backendProcess.waitFor(2, java.util.concurrent.TimeUnit.SECONDS)) {
                    backendProcess.destroyForcibly();
                }
            } catch (InterruptedException e) {
                backendProcess.destroyForcibly();
            }
        }
        executor.shutdownNow();
    }

    public void addNotificationListener(Consumer<Notification> listener) {
        synchronized (listeners) {
            listeners.add(listener);
        }
    }

    private void readStdout() {
        try (DataInputStream in = new DataInputStream(backendProcess.getInputStream())) {
            while (true) {
                int size = Integer.reverseBytes(in.readInt()); // Little endian
                byte[] buf = new byte[size];
                in.readFully(buf);

                ByteBuffer bb = ByteBuffer.wrap(buf);
                bb.order(ByteOrder.LITTLE_ENDIAN);
                Notification notification = Notification.getRootAsNotification(bb);
                handleNotification(notification);
            }
        } catch (IOException e) {
            System.err.println("Backend stdout closed: " + e.getMessage());
        }
    }

    private void readStderr() {
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(backendProcess.getErrorStream()))) {
            String line;
            while ((line = reader.readLine()) != null) {
                System.err.println("[Backend] " + line);
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    private void handleNotification(Notification notification) {
        synchronized (listeners) {
            for (Consumer<Notification> listener : listeners) {
                listener.accept(notification);
            }
        }
    }

    public synchronized void sendRequest(FlatBufferBuilder builder) {
        try {
            byte[] data = builder.sizedByteArray();
            int size = data.length;
            // Send size as little-endian 4-byte int
            out.writeInt(Integer.reverseBytes(size));
            out.write(data);
            out.flush();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }


    private String findBinary(String binaryName) {
        // Try simple relative
        if (new File("./" + binaryName).exists()) return "./" + binaryName;
        
        // Search up for bazel-bin or root
        File dir = new File(".").getAbsoluteFile();
        for (int i = 0; i < 10; i++) {
            if (dir == null) break;
            
            // Try in bazel-bin
            File bin = new File(dir, "bazel-bin/" + binaryName);
            if (bin.exists()) return bin.getAbsolutePath();
            
            // Try in bazel-out
            File outWin = new File(dir, "bazel-out/x64_windows-opt/bin/" + binaryName);
            if (outWin.exists()) return outWin.getAbsolutePath();
            File outLinux = new File(dir, "bazel-out/k8-opt/bin/" + binaryName);
            if (outLinux.exists()) return outLinux.getAbsolutePath();

            // Try in runfiles sibling to jar (if executed via java_binary)
            File rf = new File(dir, binaryName + ".runfiles/_main/" + binaryName);
            if (rf.exists()) return rf.getAbsolutePath();

            dir = dir.getParentFile();
        }
        
        // Try environment
        String runfilesDir = System.getenv("RUNFILES_DIR");
        if (runfilesDir != null) {
            File f1 = new File(runfilesDir, "_main/" + binaryName);
            if (f1.exists()) return f1.getAbsolutePath();
            File f2 = new File(runfilesDir, "hibiki/" + binaryName);
            if (f2.exists()) return f2.getAbsolutePath();
        }
        
        return null;
    }
}
