package hibiki.android.model;

import java.util.List;

/**
 * Coordinates step playback across channels, translating tracker cells into real-time note events.
 */
public class StepSequencer {

    @FunctionalInterface
    public interface NoteConsumer {
        void onNoteEvent(int channelIdx, int midiNote, int velocity, boolean isNoteOn);
    }

    // Map of channelIndex -> List of active MIDI notes currently playing on that channel
    private final java.util.Map<Integer, java.util.List<Integer>> activeNotesByChannel = new java.util.HashMap<>();
    private int lastStepIndex = -1;

    public StepSequencer() {}

    /**
     * Processes step advancement and dispatches note events to consumer.
     */
    public synchronized void processStep(int stepIndex, List<ChannelState> channels, NoteConsumer consumer) {
        if (channels == null || consumer == null) {
            return;
        }

        // Check if any channel is soloed
        boolean hasSolo = false;
        for (ChannelState ch : channels) {
            if (ch != null && ch.isSoloed()) {
                hasSolo = true;
                break;
            }
        }

        // 1. Send Note-Offs for previously active notes on step transition
        for (java.util.Map.Entry<Integer, java.util.List<Integer>> entry : activeNotesByChannel.entrySet()) {
            int chIdx = entry.getKey();
            for (int note : entry.getValue()) {
                consumer.onNoteEvent(chIdx, note, 0, false);
            }
        }
        activeNotesByChannel.clear();
        lastStepIndex = stepIndex;

        // 2. Dispatch Note-Ons for current step
        for (int chIdx = 0; chIdx < channels.size(); chIdx++) {
            ChannelState ch = channels.get(chIdx);
            if (ch == null || ch.isMuted()) {
                continue;
            }
            if (hasSolo && !ch.isSoloed()) {
                continue;
            }

            List<TrackerCell> steps = ch.getSteps();
            if (stepIndex >= 0 && stepIndex < steps.size()) {
                TrackerCell cell = steps.get(stepIndex);
                if (cell != null && cell.isActive()) {
                    int midiNote = cell.getMidiNote();
                    if (midiNote >= 0 && midiNote <= 127) {
                        int vel = Math.max(1, Math.min(127, cell.getVelocity()));
                        consumer.onNoteEvent(chIdx, midiNote, vel, true);
                        activeNotesByChannel.computeIfAbsent(chIdx, k -> new java.util.ArrayList<>()).add(midiNote);
                    }
                }
            }
        }
    }

    /**
     * Resets sequencer state (stops all currently playing notes).
     */
    public synchronized void reset(NoteConsumer consumer) {
        if (consumer != null) {
            for (java.util.Map.Entry<Integer, java.util.List<Integer>> entry : activeNotesByChannel.entrySet()) {
                int chIdx = entry.getKey();
                for (int note : entry.getValue()) {
                    consumer.onNoteEvent(chIdx, note, 0, false);
                }
            }
        }
        activeNotesByChannel.clear();
        lastStepIndex = -1;
    }

    public int getLastStepIndex() {
        return lastStepIndex;
    }
}
