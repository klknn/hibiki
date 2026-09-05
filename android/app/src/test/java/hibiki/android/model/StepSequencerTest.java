package hibiki.android.model;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import java.util.ArrayList;
import java.util.List;
import org.junit.Before;
import org.junit.Test;

/**
 * Unit tests verifying step sequencer note triggering during playback.
 * Ensures that tracker cells actually produce sound events when the playhead moves.
 */
public class StepSequencerTest {

    private static class NoteEventRecord {
        final int channel;
        final int note;
        final int velocity;
        final boolean isNoteOn;

        NoteEventRecord(int channel, int note, int velocity, boolean isNoteOn) {
            this.channel = channel;
            this.note = note;
            this.velocity = velocity;
            this.isNoteOn = isNoteOn;
        }
    }

    private List<ChannelState> channels;
    private StepSequencer sequencer;
    private List<NoteEventRecord> dispatchedEvents;

    @Before
    public void setUp() {
        sequencer = new StepSequencer();
        dispatchedEvents = new ArrayList<>();

        List<TrackerCell> t0Steps = new ArrayList<>();
        List<TrackerCell> t1Steps = new ArrayList<>();

        for (int i = 0; i < 16; i++) {
            if (i == 0) {
                // Step 0: Kick
                t0Steps.add(new TrackerCell("C-", 3, 120, 0, "00", true));
                // Step 0: Bass
                t1Steps.add(new TrackerCell("A-", 2, 100, 2, "00", true));
            } else {
                t0Steps.add(new TrackerCell());
                t1Steps.add(new TrackerCell());
            }
        }

        channels = new ArrayList<>();
        channels.add(new ChannelState(0, "DRUMS", 0xFFFF0055, 0.85f, 0f, false, false, t0Steps));
        channels.add(new ChannelState(1, "BASS", 0xFF00FFCC, 0.80f, 0f, false, false, t1Steps));
    }

    @Test
    public void testStepAdvancesAndTriggersActiveNotes() {
        sequencer.processStep(0, channels, (ch, note, vel, on) -> {
            dispatchedEvents.add(new NoteEventRecord(ch, note, vel, on));
        });

        // Must trigger Kick on Ch 0 and Bass on Ch 1
        long noteOnCount = dispatchedEvents.stream().filter(e -> e.isNoteOn).count();
        assertTrue("FAIL: processStep(0) must trigger note-on events for active tracker steps, but triggered: " + noteOnCount,
                noteOnCount >= 2);
    }

    @Test
    public void testStepTransitionSendsNoteOffs() {
        // Step 0
        sequencer.processStep(0, channels, (ch, note, vel, on) -> dispatchedEvents.add(new NoteEventRecord(ch, note, vel, on)));
        dispatchedEvents.clear();

        // Advance to Step 1 (which is empty)
        sequencer.processStep(1, channels, (ch, note, vel, on) -> dispatchedEvents.add(new NoteEventRecord(ch, note, vel, on)));

        long noteOffCount = dispatchedEvents.stream().filter(e -> !e.isNoteOn).count();
        assertTrue("FAIL: Moving to empty step must send note-off for previous active notes, but sent: " + noteOffCount,
                noteOffCount >= 2);
    }
}
