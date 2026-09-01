package hibiki.android.engine;

import android.util.Log;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;

/**
 * Android JNI wrapper and in-process audio engine coordinator for Hibiki DAW.
 * Communicates with the C++ high-performance engine via libhibiki_jni.so.
 */
public final class HibikiEngine {
    private static final String TAG = "HibikiEngine";
    private static final AtomicBoolean isInitialized = new AtomicBoolean(false);
    private static Thread notificationThread = null;
    private static final CopyOnWriteArrayList<Consumer<byte[]>> notificationListeners =
            new CopyOnWriteArrayList<>();

    static {
        try {
            System.loadLibrary("hibiki_jni");
            Log.i(TAG, "Successfully loaded libhibiki_jni.so");
        } catch (UnsatisfiedLinkError e) {
            Log.w(TAG, "libhibiki_jni.so not found in standard paths; running in simulated mode: " + e.getMessage());
        }
    }

    private HibikiEngine() {}

    /**
     * Initializes the native audio engine with specified sample rate and buffer latency.
     */
    public static synchronized boolean initEngine(int sampleRate, int bufferLatencyMs) {
        if (isInitialized.get()) {
            return true;
        }

        boolean ok = false;
        try {
            ok = nativeInit(sampleRate, bufferLatencyMs);
        } catch (UnsatisfiedLinkError e) {
            ok = true; // Simulated fallback for preview/testing
        }

        if (ok) {
            isInitialized.set(true);
            startNotificationPoller();
            Log.i(TAG, "Native audio engine initialized (" + sampleRate + " Hz, " + bufferLatencyMs + "ms)");
        }
        return ok;
    }

    public static boolean initEngine() {
        return initEngine(44100, 50);
    }

    /**
     * Shuts down native audio engine and terminates polling thread.
     */
    public static synchronized void destroyEngine() {
        if (!isInitialized.get()) {
            return;
        }
        isInitialized.set(false);
        if (notificationThread != null) {
            notificationThread.interrupt();
            notificationThread = null;
        }
        try {
            nativeDestroy();
        } catch (UnsatisfiedLinkError ignored) {}
    }

    /**
     * Sends a raw serialized Protobuf Request byte array to the engine.
     */
    public static boolean sendRequestBytes(byte[] requestBytes) {
        if (!isInitialized.get()) {
            return false;
        }
        try {
            return nativeSendRequest(requestBytes);
        } catch (UnsatisfiedLinkError e) {
            return false;
        }
    }

    /**
     * Starts or stops audio playback.
     */
    public static void setPlayback(boolean playing) {
        try {
            nativeSetPlayback(playing);
        } catch (UnsatisfiedLinkError ignored) {}
    }

    /**
     * Returns true if audio playback is currently active.
     */
    public static boolean isPlaying() {
        try {
            return nativeIsPlaying();
        } catch (UnsatisfiedLinkError e) {
            return false;
        }
    }

    /**
     * Returns current playhead position in seconds.
     */
    public static double getPlaybackPosition() {
        try {
            return nativeGetPlaybackPosition();
        } catch (UnsatisfiedLinkError e) {
            return 0.0;
        }
    }

    /**
     * Sets project tempo in BPM.
     */
    public static void setBpm(double bpm) {
        try {
            nativeSetBpm(bpm);
        } catch (UnsatisfiedLinkError ignored) {}
    }

    /**
     * Gets project tempo in BPM.
     */
    public static double getBpm() {
        try {
            return nativeGetBpm();
        } catch (UnsatisfiedLinkError e) {
            return 120.0;
        }
    }

    /**
     * Adds a listener for serialized protobuf Notifications emitted by the engine.
     */
    public static void addNotificationListener(Consumer<byte[]> listener) {
        if (listener != null) {
            notificationListeners.add(listener);
        }
    }

    /**
     * Removes a notification listener.
     */
    public static void removeNotificationListener(Consumer<byte[]> listener) {
        if (listener != null) {
            notificationListeners.remove(listener);
        }
    }

    private static void startNotificationPoller() {
        notificationThread = new Thread(() -> {
            while (isInitialized.get() && !Thread.currentThread().isInterrupted()) {
                try {
                    byte[] notifBytes = nativePollNotification();
                    if (notifBytes != null && notifBytes.length > 0) {
                        for (Consumer<byte[]> listener : notificationListeners) {
                            try {
                                listener.accept(notifBytes);
                            } catch (Exception e) {
                                Log.e(TAG, "Notification listener error", e);
                            }
                        }
                    } else {
                        Thread.sleep(10);
                    }
                } catch (InterruptedException | UnsatisfiedLinkError e) {
                    break;
                } catch (Exception e) {
                    Log.w(TAG, "Error polling notifications", e);
                }
            }
        }, "Hibiki-NotificationPoller");
        notificationThread.setDaemon(true);
        notificationThread.start();
    }

    // Native JNI functions
    private static native boolean nativeInit(int sampleRate, int bufferLatencyMs);
    private static native void nativeDestroy();
    private static native boolean nativeSendRequest(byte[] requestBytes);
    private static native byte[] nativePollNotification();
    private static native void nativeSetPlayback(boolean play);
    private static native boolean nativeIsPlaying();
    private static native double nativeGetPlaybackPosition();
    private static native void nativeSetBpm(double bpm);
    private static native double nativeGetBpm();
}
