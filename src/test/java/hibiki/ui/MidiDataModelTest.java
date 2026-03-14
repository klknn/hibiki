package hibiki.ui;

import hibiki.ipc.ClipMidiData;
import hibiki.ipc.MidiEventData;
import com.google.flatbuffers.FlatBufferBuilder;
import org.junit.Test;
import static org.junit.Assert.*;

import javax.sound.midi.*;
import java.io.File;
import java.nio.ByteBuffer;

public class MidiDataModelTest {

    @Test
    public void testNoteConstructor() {
        MidiDataModel.Note note = new MidiDataModel.Note(60, 480, 240, 100);
        assertEquals(60, note.pitch);
        assertEquals(480, note.startTick);
        assertEquals(240, note.durationTicks);
        assertEquals(100, note.velocity);
        assertNull(note.onEvent);
        assertNull(note.offEvent);
    }

    @Test
    public void testNoteFieldMutation() {
        MidiDataModel.Note note = new MidiDataModel.Note(60, 0, 96, 100);
        note.pitch = 72;
        note.startTick = 480;
        note.durationTicks = 192;
        note.velocity = 50;
        assertEquals(72, note.pitch);
        assertEquals(480, note.startTick);
        assertEquals(192, note.durationTicks);
        assertEquals(50, note.velocity);
    }

    @Test
    public void testEmptyModelDefaults() {
        MidiDataModel model = new MidiDataModel();
        assertNotNull(model.notes);
        assertTrue(model.notes.isEmpty());
        assertNull(model.sequence);
        assertNull(model.midiTrack);
    }

    @Test
    public void testLoadMidi_nonExistentFile() {
        MidiDataModel model = new MidiDataModel();
        model.loadMidi(new File("nonexistent.mid"));
        // Should create empty sequence as fallback
        assertNotNull(model.sequence);
        assertNotNull(model.midiTrack);
        assertEquals(96, model.sequence.getResolution());
        assertTrue(model.notes.isEmpty());
    }

    @Test
    public void testLoadMidi_realFile() {
        File testMidi = new File("testdata/test.mid");
        if (!testMidi.exists()) return; // Skip if testdata not available
        MidiDataModel model = new MidiDataModel();
        model.loadMidi(testMidi);
        assertNotNull(model.sequence);
        assertNotNull(model.midiTrack);
    }

    @Test
    public void testHasNotes_emptyTrack() throws Exception {
        Sequence seq = new Sequence(Sequence.PPQ, 96);
        Track track = seq.createTrack();
        MidiDataModel model = new MidiDataModel();
        assertFalse(model.hasNotes(track));
    }

    @Test
    public void testHasNotes_withNoteOn() throws Exception {
        Sequence seq = new Sequence(Sequence.PPQ, 96);
        Track track = seq.createTrack();
        // Add a NOTE_ON event
        ShortMessage noteOn = new ShortMessage();
        noteOn.setMessage(ShortMessage.NOTE_ON, 0, 60, 100);
        track.add(new MidiEvent(noteOn, 0));
        MidiDataModel model = new MidiDataModel();
        assertTrue(model.hasNotes(track));
    }

    @Test
    public void testHasNotes_withNonNoteMessages() throws Exception {
        Sequence seq = new Sequence(Sequence.PPQ, 96);
        Track track = seq.createTrack();
        // Add a CONTROL_CHANGE (not a note)
        ShortMessage cc = new ShortMessage();
        cc.setMessage(ShortMessage.CONTROL_CHANGE, 0, 7, 100);
        track.add(new MidiEvent(cc, 0));
        MidiDataModel model = new MidiDataModel();
        assertFalse(model.hasNotes(track));
    }

    @Test
    public void testParseTrack_noteOnOff() throws Exception {
        Sequence seq = new Sequence(Sequence.PPQ, 96);
        Track track = seq.createTrack();
        // Add NOTE_ON at tick 0
        ShortMessage noteOn = new ShortMessage();
        noteOn.setMessage(ShortMessage.NOTE_ON, 0, 60, 100);
        track.add(new MidiEvent(noteOn, 0));
        // Add NOTE_OFF at tick 96
        ShortMessage noteOff = new ShortMessage();
        noteOff.setMessage(ShortMessage.NOTE_OFF, 0, 60, 0);
        track.add(new MidiEvent(noteOff, 96));

        MidiDataModel model = new MidiDataModel();
        model.parseTrack(track);
        assertEquals(1, model.notes.size());

        MidiDataModel.Note n = model.notes.get(0);
        assertEquals(60, n.pitch);
        assertEquals(0, n.startTick);
        assertEquals(96, n.durationTicks);
        assertEquals(100, n.velocity);
        assertNotNull(n.onEvent);
        assertNotNull(n.offEvent);
    }

    @Test
    public void testParseTrack_velocityZeroAsNoteOff() throws Exception {
        Sequence seq = new Sequence(Sequence.PPQ, 96);
        Track track = seq.createTrack();
        // NOTE_ON vel=100
        ShortMessage noteOn = new ShortMessage();
        noteOn.setMessage(ShortMessage.NOTE_ON, 0, 64, 100);
        track.add(new MidiEvent(noteOn, 0));
        // NOTE_ON vel=0 (acts as NOTE_OFF)
        ShortMessage noteOff = new ShortMessage();
        noteOff.setMessage(ShortMessage.NOTE_ON, 0, 64, 0);
        track.add(new MidiEvent(noteOff, 48));

        MidiDataModel model = new MidiDataModel();
        model.parseTrack(track);
        assertEquals(1, model.notes.size());
        assertEquals(64, model.notes.get(0).pitch);
        assertEquals(48, model.notes.get(0).durationTicks);
    }

    @Test
    public void testParseTrack_multipleNotes() throws Exception {
        Sequence seq = new Sequence(Sequence.PPQ, 96);
        Track track = seq.createTrack();
        // Note 1: C4 (60) at tick 0-96
        addNote(track, 60, 100, 0, 96);
        // Note 2: E4 (64) at tick 96-192
        addNote(track, 64, 80, 96, 192);
        // Note 3: G4 (67) at tick 192-288
        addNote(track, 67, 90, 192, 288);

        MidiDataModel model = new MidiDataModel();
        model.parseTrack(track);
        assertEquals(3, model.notes.size());
    }

    @Test
    public void testParseTrack_duplicateNoteOnIgnored() throws Exception {
        Sequence seq = new Sequence(Sequence.PPQ, 96);
        Track track = seq.createTrack();
        // Two NOTE_ON for same pitch without intervening NOTE_OFF
        ShortMessage on1 = new ShortMessage();
        on1.setMessage(ShortMessage.NOTE_ON, 0, 60, 100);
        track.add(new MidiEvent(on1, 0));
        ShortMessage on2 = new ShortMessage();
        on2.setMessage(ShortMessage.NOTE_ON, 0, 60, 90);
        track.add(new MidiEvent(on2, 48));
        ShortMessage off = new ShortMessage();
        off.setMessage(ShortMessage.NOTE_OFF, 0, 60, 0);
        track.add(new MidiEvent(off, 96));

        MidiDataModel model = new MidiDataModel();
        model.parseTrack(track);
        // Should only create one note (first NOTE_ON wins)
        assertEquals(1, model.notes.size());
        assertEquals(100, model.notes.get(0).velocity);
        assertEquals(96, model.notes.get(0).durationTicks);
    }

    @Test
    public void testParseTrack_noteOffWithoutNoteOn() throws Exception {
        Sequence seq = new Sequence(Sequence.PPQ, 96);
        Track track = seq.createTrack();
        // NOTE_OFF without prior NOTE_ON
        ShortMessage off = new ShortMessage();
        off.setMessage(ShortMessage.NOTE_OFF, 0, 60, 0);
        track.add(new MidiEvent(off, 96));

        MidiDataModel model = new MidiDataModel();
        model.parseTrack(track);
        assertTrue(model.notes.isEmpty());
    }

    @Test
    public void testLoadFromBackendData() {
        // Build a FlatBuffer ClipMidiData with 2 notes
        FlatBufferBuilder builder = new FlatBufferBuilder(512);
        int[] eventOffsets = new int[2];
        // Note 1: pitch=60, tick=0, durationTicks=96, velocity=100
        eventOffsets[0] = MidiEventData.createMidiEventData(builder, 0, 60, 96, 100);
        // Note 2: pitch=64, tick=96, durationTicks=48, velocity=80
        eventOffsets[1] = MidiEventData.createMidiEventData(builder, 96, 64, 48, 80);

        int eventsVec = ClipMidiData.createEventsVector(builder, eventOffsets);
        ClipMidiData.startClipMidiData(builder);
        ClipMidiData.addTrackIndex(builder, 0);
        ClipMidiData.addSlotIndex(builder, -1);
        ClipMidiData.addClipIndex(builder, 0);
        ClipMidiData.addResolution(builder, 480);
        ClipMidiData.addEvents(builder, eventsVec);
        int dataOff = ClipMidiData.endClipMidiData(builder);
        builder.finish(dataOff);

        ByteBuffer buf = builder.dataBuffer();
        ClipMidiData data = ClipMidiData.getRootAsClipMidiData(buf);

        MidiDataModel model = new MidiDataModel();
        model.loadFromBackendData(data);

        assertEquals(2, model.notes.size());
        assertEquals(60, model.notes.get(0).pitch);
        assertEquals(0, model.notes.get(0).startTick);
        assertEquals(96, model.notes.get(0).durationTicks);
        assertEquals(100, model.notes.get(0).velocity);

        assertEquals(64, model.notes.get(1).pitch);
        assertEquals(96, model.notes.get(1).startTick);
        assertEquals(48, model.notes.get(1).durationTicks);
        assertEquals(80, model.notes.get(1).velocity);

        // Resolution should be updated
        assertNotNull(model.sequence);
        assertEquals(480, model.sequence.getResolution());
    }

    @Test
    public void testLoadFromBackendData_zeroResolution() {
        FlatBufferBuilder builder = new FlatBufferBuilder(256);
        int eventsVec = ClipMidiData.createEventsVector(builder, new int[0]);
        ClipMidiData.startClipMidiData(builder);
        ClipMidiData.addTrackIndex(builder, 0);
        ClipMidiData.addResolution(builder, 0); // zero resolution
        ClipMidiData.addEvents(builder, eventsVec);
        int dataOff = ClipMidiData.endClipMidiData(builder);
        builder.finish(dataOff);

        ClipMidiData data = ClipMidiData.getRootAsClipMidiData(builder.dataBuffer());
        MidiDataModel model = new MidiDataModel();
        model.loadFromBackendData(data);

        assertTrue(model.notes.isEmpty());
        // sequence should remain null since resolution=0
        assertNull(model.sequence);
    }

    @Test
    public void testLoadFromBackendData_replacesExisting() {
        MidiDataModel model = new MidiDataModel();
        model.notes.add(new MidiDataModel.Note(60, 0, 96, 100));
        assertEquals(1, model.notes.size());

        // Load empty data should clear
        FlatBufferBuilder builder = new FlatBufferBuilder(256);
        int eventsVec = ClipMidiData.createEventsVector(builder, new int[0]);
        ClipMidiData.startClipMidiData(builder);
        ClipMidiData.addTrackIndex(builder, 0);
        ClipMidiData.addResolution(builder, 480);
        ClipMidiData.addEvents(builder, eventsVec);
        int dataOff = ClipMidiData.endClipMidiData(builder);
        builder.finish(dataOff);

        ClipMidiData data = ClipMidiData.getRootAsClipMidiData(builder.dataBuffer());
        model.loadFromBackendData(data);
        assertTrue(model.notes.isEmpty());
    }

    private void addNote(Track track, int pitch, int velocity, long startTick, long endTick) throws Exception {
        ShortMessage noteOn = new ShortMessage();
        noteOn.setMessage(ShortMessage.NOTE_ON, 0, pitch, velocity);
        track.add(new MidiEvent(noteOn, startTick));
        ShortMessage noteOff = new ShortMessage();
        noteOff.setMessage(ShortMessage.NOTE_OFF, 0, pitch, 0);
        track.add(new MidiEvent(noteOff, endTick));
    }
}
