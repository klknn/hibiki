package hibiki.ui;

import static org.junit.Assert.*;

import hibiki.pb.commands.*;
import hibiki.pb.notifications.*;
import java.io.File;
import javax.sound.midi.*;
import org.junit.Test;

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
    // Build a protobuf ClipMidiData with 2 notes
    ClipMidiData data =
        ClipMidiData.newBuilder()
            .setTrackIndex(0)
            .setSlotIndex(-1)
            .setClipIndex(0)
            .setResolution(480)
            .addEvents(
                hibiki.pb.core.MidiEvent.newBuilder()
                    .setTick(0)
                    .setPitch(60)
                    .setDurationTicks(96)
                    .setVelocity(100))
            .addEvents(
                hibiki.pb.core.MidiEvent.newBuilder()
                    .setTick(96)
                    .setPitch(64)
                    .setDurationTicks(48)
                    .setVelocity(80))
            .build();

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
    ClipMidiData data = ClipMidiData.newBuilder().setTrackIndex(0).setResolution(0).build();

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
    ClipMidiData data = ClipMidiData.newBuilder().setTrackIndex(0).setResolution(480).build();
    model.loadFromBackendData(data);
    assertTrue(model.notes.isEmpty());
  }

  // ── Round-trip tests (proto → model → verify) ─────────────────────

  @Test
  public void testNoteEditRoundTrip() {
    // Create model with notes, build proto, load back → should match
    MidiDataModel original = TestStateHelper.createModelWithNotes(60, 64, 67);
    ClipMidiData data =
        TestStateHelper.buildClipMidiData(
            480, original.notes.get(0), original.notes.get(1), original.notes.get(2));

    MidiDataModel restored = new MidiDataModel();
    restored.loadFromBackendData(data);

    TestStateHelper.assertNotesEqual(original.notes, restored.notes);
  }

  @Test
  public void testCCEventRoundTrip() {
    // CC#1 (modulation) events
    MidiDataModel original = TestStateHelper.createModelWithCCEvents(1, 0, 64, 127);
    ClipMidiData data =
        TestStateHelper.buildClipMidiDataWithCC(
            480, java.util.Collections.emptyList(), original.ccEvents);

    MidiDataModel restored = new MidiDataModel();
    restored.loadFromBackendData(data);

    assertTrue(restored.notes.isEmpty());
    TestStateHelper.assertCCEventsEqual(original.ccEvents, restored.ccEvents);
  }

  @Test
  public void testPitchBendRoundTrip() {
    // ccNumber=128 for pitch bend, value range [-8192..+8191]
    MidiDataModel original = TestStateHelper.createModelWithCCEvents(128, -8192, 0, 8191);
    ClipMidiData data =
        TestStateHelper.buildClipMidiDataWithCC(
            480, java.util.Collections.emptyList(), original.ccEvents);

    MidiDataModel restored = new MidiDataModel();
    restored.loadFromBackendData(data);

    TestStateHelper.assertCCEventsEqual(original.ccEvents, restored.ccEvents);
  }

  @Test
  public void testMixedNotesAndCCRoundTrip() {
    // Build model with both notes and CC events
    MidiDataModel original = TestStateHelper.createModelWithNotes(60, 67);
    original.ccEvents.add(new MidiDataModel.CCEvent(1, 0, 64));
    original.ccEvents.add(new MidiDataModel.CCEvent(128, 480, -4096));

    ClipMidiData data =
        TestStateHelper.buildClipMidiDataWithCC(480, original.notes, original.ccEvents);

    MidiDataModel restored = new MidiDataModel();
    restored.loadFromBackendData(data);

    TestStateHelper.assertNotesEqual(original.notes, restored.notes);
    TestStateHelper.assertCCEventsEqual(original.ccEvents, restored.ccEvents);
  }

  @Test
  public void testEmptyModelRoundTrip() {
    ClipMidiData data = ClipMidiData.newBuilder().setResolution(480).build();

    MidiDataModel restored = new MidiDataModel();
    restored.loadFromBackendData(data);

    assertTrue(restored.notes.isEmpty());
    assertTrue(restored.ccEvents.isEmpty());
  }

  @Test
  public void testParseTrack_ccEvents() throws Exception {
    Sequence seq = new Sequence(Sequence.PPQ, 96);
    Track track = seq.createTrack();
    // Add CC#7 at tick 0
    ShortMessage cc = new ShortMessage();
    cc.setMessage(ShortMessage.CONTROL_CHANGE, 0, 7, 100);
    track.add(new MidiEvent(cc, 0));
    // Add CC#1 at tick 96
    ShortMessage mod = new ShortMessage();
    mod.setMessage(ShortMessage.CONTROL_CHANGE, 0, 1, 64);
    track.add(new MidiEvent(mod, 96));

    MidiDataModel model = new MidiDataModel();
    model.parseTrack(track);

    assertTrue(model.notes.isEmpty());
    assertEquals(2, model.ccEvents.size());
    assertEquals(7, model.ccEvents.get(0).ccNumber);
    assertEquals(100, model.ccEvents.get(0).value);
    assertEquals(1, model.ccEvents.get(1).ccNumber);
    assertEquals(64, model.ccEvents.get(1).value);
  }

  @Test
  public void testParseTrack_pitchBend() throws Exception {
    Sequence seq = new Sequence(Sequence.PPQ, 96);
    Track track = seq.createTrack();
    // Pitch bend center (MSB=64, LSB=0 → raw=8192 → value=0)
    ShortMessage pb = new ShortMessage();
    pb.setMessage(ShortMessage.PITCH_BEND, 0, 0, 64);
    track.add(new MidiEvent(pb, 0));

    MidiDataModel model = new MidiDataModel();
    model.parseTrack(track);

    assertEquals(1, model.ccEvents.size());
    assertEquals(128, model.ccEvents.get(0).ccNumber);
    assertEquals(0, model.ccEvents.get(0).value); // center = 0
  }

  private void addNote(Track track, int pitch, int velocity, long startTick, long endTick)
      throws Exception {
    ShortMessage noteOn = new ShortMessage();
    noteOn.setMessage(ShortMessage.NOTE_ON, 0, pitch, velocity);
    track.add(new MidiEvent(noteOn, startTick));
    ShortMessage noteOff = new ShortMessage();
    noteOff.setMessage(ShortMessage.NOTE_OFF, 0, pitch, 0);
    track.add(new MidiEvent(noteOff, endTick));
  }
}
