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

    @FunctionalInterface
    public interface MidiEventListener {
        void onMidiEvent(int trackIndex, int note, int velocity, boolean noteOn);
    }
    private static final CopyOnWriteArrayList<MidiEventListener> midiListeners =
            new CopyOnWriteArrayList<>();

    private static volatile boolean isNativeLoaded = false;
    private static volatile boolean simulatedPlaying = false;
    private static volatile boolean isLooping = true;
    private static volatile double loopBars = 16.0;
    private static volatile double simulatedBpm = 120.0;
    private static volatile double simulatedPlayheadSec = 0.0;
    private static volatile long lastNanoTime = 0L;

    private static void logInfo(String msg) {
        try {
            Log.i(TAG, msg);
        } catch (Throwable ignored) {
            System.out.println("[INFO] " + TAG + ": " + msg);
        }
    }

    private static void logWarn(String msg) {
        try {
            Log.w(TAG, msg);
        } catch (Throwable ignored) {
            System.err.println("[WARN] " + TAG + ": " + msg);
        }
    }

    private static void logError(String msg, Throwable t) {
        try {
            Log.e(TAG, msg, t);
        } catch (Throwable ignored) {
            System.err.println("[ERROR] " + TAG + ": " + msg);
        }
    }

    static {
        boolean loaded = false;
        try {
            System.loadLibrary("hibiki_jni");
            logInfo("Successfully loaded libhibiki_jni.so");
            loaded = true;
        } catch (UnsatisfiedLinkError e) {
            logWarn("libhibiki_jni.so not found in standard paths; running in simulated mode: " + e.getMessage());
        }
        isNativeLoaded = loaded;
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
            if (!isNativeLoaded) {
                FallbackSynth.init();
            }
            startNotificationPoller();
            logInfo("Native audio engine initialized (" + sampleRate + " Hz, " + bufferLatencyMs + "ms)");
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
        simulatedPlaying = false;
        simulatedPlayheadSec = 0.0;
        FallbackSynth.destroy();
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
        if (isNativeLoaded) {
            try {
                nativeSetPlayback(playing);
                return;
            } catch (UnsatisfiedLinkError ignored) {}
        }
        synchronized (HibikiEngine.class) {
            simulatedPlaying = playing;
            if (playing) {
                lastNanoTime = System.nanoTime();
            }
        }
    }

    /**
     * Returns true if audio playback is currently active.
     */
    public static boolean isPlaying() {
        if (isNativeLoaded) {
            try {
                return nativeIsPlaying();
            } catch (UnsatisfiedLinkError ignored) {}
        }
        return simulatedPlaying;
    }

    /**
     * Returns current playhead position in seconds.
     */
    public static double getPlaybackPosition() {
        if (isNativeLoaded) {
            try {
                return nativeGetPlaybackPosition();
            } catch (UnsatisfiedLinkError ignored) {}
        }
        synchronized (HibikiEngine.class) {
            if (simulatedPlaying) {
                long now = System.nanoTime();
                if (lastNanoTime > 0) {
                    double dt = (now - lastNanoTime) / 1_000_000_000.0;
                    simulatedPlayheadSec += dt;
                    if (isLooping) {
                        double loopSec = (loopBars * 4.0) * (60.0 / simulatedBpm);
                        if (simulatedPlayheadSec >= loopSec && loopSec > 0.0) {
                            simulatedPlayheadSec = simulatedPlayheadSec % loopSec;
                        }
                    }
                }
                lastNanoTime = now;
            }
            return simulatedPlayheadSec;
        }
    }

    /**
     * Sets playback position in seconds.
     */
    public static synchronized void setPlaybackPosition(double sec) {
        simulatedPlayheadSec = Math.max(0.0, sec);
        lastNanoTime = System.nanoTime();
    }

    /**
     * Enables or disables loop playback.
     */
    public static void setLooping(boolean looping) {
        isLooping = looping;
    }

    /**
     * Returns whether loop playback is active.
     */
    public static boolean isLooping() {
        return isLooping;
    }

    /**
     * Sets the loop duration in musical bars.
     */
    public static void setLoopBars(double bars) {
        if (bars > 0.0) {
            loopBars = bars;
        }
    }

    /**
     * Gets the loop duration in musical bars.
     */
    public static double getLoopBars() {
        return loopBars;
    }


    /**
     * Resets playhead position to 0.0s upon stop.
     */
    public static synchronized void resetPlaybackPosition() {
        simulatedPlayheadSec = 0.0;
        lastNanoTime = System.nanoTime();
    }

    /**
     * Sets project tempo in BPM.
     */
    public static void setBpm(double bpm) {
        if (bpm > 20.0 && bpm < 999.0) {
            simulatedBpm = bpm;
        }
        if (isNativeLoaded) {
            try {
                nativeSetBpm(bpm);
            } catch (UnsatisfiedLinkError ignored) {}
        }
    }

    /**
     * Gets project tempo in BPM.
     */
    public static double getBpm() {
        if (isNativeLoaded) {
            try {
                return nativeGetBpm();
            } catch (UnsatisfiedLinkError ignored) {}
        }
        return simulatedBpm;
    }

    /**
     * Polls a serialized protobuf notification directly from the engine queue.
     */
    public static byte[] pollNotification() {
        try {
            return nativePollNotification();
        } catch (UnsatisfiedLinkError e) {
            return new byte[0];
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
                                logError("Notification listener error", e);
                            }
                        }
                    } else {
                        Thread.sleep(10);
                    }
                } catch (InterruptedException | UnsatisfiedLinkError e) {
                    break;
                } catch (Exception e) {
                    logWarn("Error polling notifications: " + e.getMessage());
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
    private static native boolean nativeSendMidiNote(int trackIndex, int note, int velocity, boolean noteOn);
    private static native void nativeSetTrackVolume(int trackIndex, float volume);
    private static native void nativeSetTrackPan(int trackIndex, float pan);
    private static native void nativeSetTrackMute(int trackIndex, boolean muted);
    private static native void nativeSetTrackSolo(int trackIndex, boolean soloed);

    /**
     * Adds a listener for real-time MIDI events dispatched through the engine.
     */
    public static void addMidiEventListener(MidiEventListener listener) {
        if (listener != null) {
            midiListeners.add(listener);
        }
    }

    /**
     * Removes a MIDI event listener.
     */
    public static void removeMidiEventListener(MidiEventListener listener) {
        if (listener != null) {
            midiListeners.remove(listener);
        }
    }

    /**
     * Sends a real-time MIDI note event directly to a track.
     */
    public static boolean sendMidiNote(int trackIndex, int note, int velocity, boolean noteOn) {
        boolean ok = false;
        if (isNativeLoaded) {
            try {
                ok = nativeSendMidiNote(trackIndex, note, velocity, noteOn);
            } catch (UnsatisfiedLinkError ignored) {
                ok = true;
            }
        } else {
            ok = true;
        }

        // Fallback acoustic tone synthesis when running without native C++ engine
        if (!isNativeLoaded && noteOn && velocity > 0) {
            FallbackSynth.playTone(note, velocity, trackIndex == 0 ? 65 : 110);
        }

        // Notify active MIDI listeners (e.g. visual feedback, sound synthesizers)
        for (MidiEventListener listener : midiListeners) {
            try {
                listener.onMidiEvent(trackIndex, note, velocity, noteOn);
            } catch (Exception ignored) {}
        }
        return ok;
    }

    /**
     * Triggers a drum pad by sending a MIDI note-on event and scheduling a note-off.
     */
    public static void triggerDrumPad(int padIndex, int midiNote, int velocity) {
        sendMidiNote(0, midiNote, velocity, true);
        new Thread(() -> {
            try {
                Thread.sleep(80);
            } catch (InterruptedException ignored) {}
            sendMidiNote(0, midiNote, 0, false);
        }).start();
    }

    /**
     * Sets volume for a track [0.0, 2.0].
     */
    public static void setTrackVolume(int trackIndex, float volume) {
        try {
            nativeSetTrackVolume(trackIndex, volume);
        } catch (UnsatisfiedLinkError ignored) {}
    }

    /**
     * Sets stereo panning for a track [-1.0, 1.0].
     */
    public static void setTrackPan(int trackIndex, float pan) {
        try {
            nativeSetTrackPan(trackIndex, pan);
        } catch (UnsatisfiedLinkError ignored) {}
    }

    /**
     * Mutes or unmutes a track.
     */
    public static void setTrackMute(int trackIndex, boolean muted) {
        try {
            nativeSetTrackMute(trackIndex, muted);
        } catch (UnsatisfiedLinkError ignored) {}
    }

    /**
     * Solos or unsolos a track.
     */
    public static void setTrackSolo(int trackIndex, boolean soloed) {
        try {
            nativeSetTrackSolo(trackIndex, soloed);
        } catch (UnsatisfiedLinkError ignored) {}
    }

    /**
     * Lightweight acoustic synthesizer fallback using Android AudioTrack when native
     * libhibiki_jni.so is unavailable.
     */
    private static class FallbackSynth {
        private static android.media.AudioTrack audioTrack;
        private static final int SAMPLE_RATE = 22050;

        static synchronized void init() {
            try {
                int minBuf = android.media.AudioTrack.getMinBufferSize(
                        SAMPLE_RATE,
                        android.media.AudioFormat.CHANNEL_OUT_MONO,
                        android.media.AudioFormat.ENCODING_PCM_16BIT);
                audioTrack = new android.media.AudioTrack(
                        android.media.AudioManager.STREAM_MUSIC,
                        SAMPLE_RATE,
                        android.media.AudioFormat.CHANNEL_OUT_MONO,
                        android.media.AudioFormat.ENCODING_PCM_16BIT,
                        Math.max(minBuf, 4096),
                        android.media.AudioTrack.MODE_STREAM);
                audioTrack.play();
            } catch (Throwable ignored) {}
        }

        static synchronized void playTone(int midiNote, int velocity, int durationMs) {
            if (audioTrack == null || audioTrack.getState() != android.media.AudioTrack.STATE_INITIALIZED) {
                return;
            }
            try {
                double freq = 440.0 * Math.pow(2.0, (midiNote - 69) / 12.0);
                int numSamples = (SAMPLE_RATE * durationMs) / 1000;
                short[] pcm = new short[numSamples];
                float vol = Math.min(1.0f, Math.max(0.1f, velocity / 127.0f));
                for (int i = 0; i < numSamples; i++) {
                    double t = (double) i / SAMPLE_RATE;
                    double env = 1.0 - ((double) i / numSamples);
                    double sample = Math.sin(2.0 * Math.PI * freq * t) * env * vol;
                    pcm[i] = (short) (sample * 24000);
                }
                audioTrack.write(pcm, 0, numSamples);
            } catch (Throwable ignored) {}
        }

        static synchronized void destroy() {
            try {
                if (audioTrack != null) {
                    audioTrack.stop();
                    audioTrack.release();
                    audioTrack = null;
                }
            } catch (Throwable ignored) {}
        }
    }
}
