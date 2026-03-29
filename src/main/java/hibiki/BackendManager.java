package hibiki;

import hibiki.pb.HibikiProto;
import hibiki.pb.HibikiProto.Notification;
import hibiki.pb.HibikiProto.Request;
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
    private boolean isPlaying = false; // Track playback state for toggle

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

    public void removeNotificationListener(Consumer<Notification> listener) {
        synchronized (listeners) {
            listeners.remove(listener);
        }
    }

    private static final int IPC_MAGIC = 0x48424B49; // "HBKI" - must match C++ side

    private void readStdout() {
        try (DataInputStream in = new DataInputStream(backendProcess.getInputStream())) {
            int msgCount = 0;
            while (true) {
                // Read and verify magic header
                int magic = Integer.reverseBytes(in.readInt());
                if (magic != IPC_MAGIC) {
                    System.err.println("[READSTDOUT ERROR] Invalid magic: 0x" + Integer.toHexString(magic) + " at msg#"
                            + msgCount + ", resyncing...");
                    // Resync: search for magic header byte by byte
                    int resyncCount = 0;
                    int buf = Integer.reverseBytes(magic);
                    while (resyncCount < 10000) {
                        int b = in.readByte() & 0xFF;
                        buf = (buf << 8) | b;
                        if (buf == 0x494B4248) {
                            System.err.println("[READSTDOUT] Resynced after " + resyncCount + " bytes");
                            break;
                        }
                        resyncCount++;
                    }
                    if (resyncCount >= 10000) {
                        System.err.println("[READSTDOUT ERROR] Could not resync after 10000 bytes, skipping...");
                        continue;
                    }
                }

                int size = Integer.reverseBytes(in.readInt()); // Little endian
                msgCount++;

                // Sanity check: messages should never be larger than 1MB
                if (size < 0 || size > 1024 * 1024) {
                    System.err.println(
                            "[READSTDOUT ERROR] Invalid size: " + size + " at msg#" + msgCount + ", skipping...");
                    continue;
                }

                byte[] data = new byte[size];
                in.readFully(data);

                Notification notification = Notification.parseFrom(data);
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
                try {
                    listener.accept(notification);
                } catch (Exception e) {
                    // Log listener errors but don't crash
                    e.printStackTrace();
                }
            }
        }
    }

    public synchronized void sendRequest(Request request) {
        if (out == null) {
            System.err.println("Warning: Backend not ready, request dropped.");
            return;
        }
        try {
            byte[] data = request.toByteArray();
            int size = data.length;
            // Send size as little-endian 4-byte int
            out.writeInt(Integer.reverseBytes(size));
            out.write(data);
            out.flush();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    public void startPlayback() {
        sendRequest(Request.newBuilder()
                .setPlay(HibikiProto.Play.getDefaultInstance())
                .build());
        isPlaying = true;
    }

    public void stopPlayback() {
        sendRequest(Request.newBuilder()
                .setStop(HibikiProto.Stop.getDefaultInstance())
                .build());
        isPlaying = false;
    }

    /** Toggle play/stop state - triggered by Space key */
    public void togglePlay() {
        if (isPlaying) {
            stopPlayback();
        } else {
            startPlayback();
        }
    }

    public void seek(float position) {
        sendRequest(Request.newBuilder()
                .setSeek(HibikiProto.Seek.newBuilder().setPosition(position))
                .build());
    }

    public void addTimelineClip(int trackIndex, String path, float startTime, float durationBeats) {
        sendRequest(Request.newBuilder()
                .setAddTimelineClip(HibikiProto.AddTimelineClip.newBuilder()
                        .setTrackIndex(trackIndex)
                        .setPath(path)
                        .setStartTime(startTime)
                        .setDurationBeats(durationBeats))
                .build());
    }

    public void removeTimelineClip(int trackIndex, int clipIndex) {
        sendRequest(Request.newBuilder()
                .setRemoveTimelineClip(HibikiProto.RemoveTimelineClip.newBuilder()
                        .setTrackIndex(trackIndex)
                        .setClipIndex(clipIndex))
                .build());
    }

    public void resizeTimelineClip(int trackIndex, int clipIndex, float durationBeats) {
        sendRequest(Request.newBuilder()
                .setResizeTimelineClip(HibikiProto.ResizeTimelineClip.newBuilder()
                        .setTrackIndex(trackIndex)
                        .setClipIndex(clipIndex)
                        .setDurationBeats(durationBeats))
                .build());
    }

    /**
     * Request MIDI data for a clip (for Piano Roll editing)
     * Use slotIdx >= 0 for session clips, clipIdx >= 0 for timeline clips
     */
    public void requestClipMidi(int trackIdx, int slotIdx, int clipIdx) {
        sendRequest(Request.newBuilder()
                .setGetClipMidi(HibikiProto.GetClipMidi.newBuilder()
                        .setTrackIndex(trackIdx)
                        .setSlotIndex(slotIdx)
                        .setClipIndex(clipIdx))
                .build());
    }

    /**
     * Update clip's MIDI data (from Piano Roll edits)
     * Use slotIdx >= 0 for session clips, clipIdx >= 0 for timeline clips
     */
    public void updateClipMidi(int trackIdx, int slotIdx, int clipIdx, int resolution, long[] ticks, int[] pitches,
            long[] durationTicks, int[] velocities) {
        HibikiProto.UpdateClipMidi.Builder cmdBuilder = HibikiProto.UpdateClipMidi.newBuilder()
                .setTrackIndex(trackIdx)
                .setSlotIndex(slotIdx)
                .setClipIndex(clipIdx)
                .setResolution(resolution);
        for (int i = 0; i < ticks.length; i++) {
            cmdBuilder.addEvents(HibikiProto.MidiEvent.newBuilder()
                    .setTick(ticks[i])
                    .setPitch(pitches[i])
                    .setDurationTicks(durationTicks[i])
                    .setVelocity(velocities[i]));
        }
        sendRequest(Request.newBuilder()
                .setUpdateClipMidi(cmdBuilder)
                .build());
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
