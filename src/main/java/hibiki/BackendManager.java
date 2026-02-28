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
            
            String hbkPlayPath = "./" + binaryName;
            if (!new File(hbkPlayPath).exists()) {
                // Try search in bazel-bin or bazel-out
                String[] potentials = {
                    "bazel-bin/" + binaryName,
                    "bazel-out/x64_windows-opt/bin/" + binaryName,
                    "bazel-out/k8-opt/bin/" + binaryName
                };
                for (String p : potentials) {
                    if (new File(p).exists()) {
                        hbkPlayPath = p;
                        break;
                    }
                }
            }
            if (!new File(hbkPlayPath).exists()) {
                System.err.println("Warning: " + binaryName + " not found");
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
}
