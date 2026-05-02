package hibiki.ui;

import static org.junit.Assert.*;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import javax.sound.midi.*;
import org.junit.Test;

/**
 * Tests for PianoRoll-related logic: Note data model, GridMode intervals, MidiDataModel parsing,
 * PianoRollRenderer BPB resolution, and PianoRollMouseHandler tick conversion. Headless-compatible
 * tests (no JDialog) come first; instance tests are guarded.
 */
public class PianoRollTest {

  // ═══════════════════════════════════════════════════════════════════════
  // Note data model
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testNoteConstructor() {
    PianoRoll.Note note = new PianoRoll.Note(60, 480, 240, 100);
    assertEquals(60, note.pitch);
    assertEquals(480, note.startTick);
    assertEquals(240, note.durationTicks);
    assertEquals(100, note.velocity);
  }

  @Test
  public void testNoteDefaultFields() {
    PianoRoll.Note note = new PianoRoll.Note(36, 0, 96, 64);
    assertNull(note.onEvent);
    assertNull(note.offEvent);
  }

  @Test
  public void testNoteExtendsMidiDataModelNote() {
    PianoRoll.Note note = new PianoRoll.Note(60, 480, 240, 100);
    assertTrue(note instanceof MidiDataModel.Note);
  }

  @Test
  public void testNoteMutation() {
    PianoRoll.Note note = new PianoRoll.Note(60, 0, 96, 100);
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
  public void testNoteVelocityBounds() {
    PianoRoll.Note note = new PianoRoll.Note(60, 0, 96, 0);
    assertEquals(0, note.velocity);
    note.velocity = 127;
    assertEquals(127, note.velocity);
  }

  @Test
  public void testNoteHighPitch() {
    PianoRoll.Note note = new PianoRoll.Note(127, 0, 48, 127);
    assertEquals(127, note.pitch);
    assertEquals(127, note.velocity);
  }

  @Test
  public void testNoteLowPitch() {
    PianoRoll.Note note = new PianoRoll.Note(0, 0, 48, 1);
    assertEquals(0, note.pitch);
    assertEquals(1, note.velocity);
  }

  @Test
  public void testNoteMultipleInstances() {
    PianoRoll.Note n1 = new PianoRoll.Note(60, 0, 96, 100);
    PianoRoll.Note n2 = new PianoRoll.Note(64, 96, 48, 80);
    assertNotSame(n1, n2);
    assertEquals(60, n1.pitch);
    assertEquals(64, n2.pitch);
  }

  @Test
  public void testNumKeysConstant() {
    assertEquals("Standard MIDI has 128 keys", 128, PianoRoll.NUM_KEYS);
  }

  // ═══════════════════════════════════════════════════════════════════════
  // GridMode — isBlackKey
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testIsBlackKey() {
    assertFalse(GridMode.isBlackKey(0)); // C
    assertTrue(GridMode.isBlackKey(1)); // C#
    assertFalse(GridMode.isBlackKey(2)); // D
    assertTrue(GridMode.isBlackKey(3)); // D#
    assertFalse(GridMode.isBlackKey(4)); // E
    assertFalse(GridMode.isBlackKey(5)); // F
    assertTrue(GridMode.isBlackKey(6)); // F#
    assertFalse(GridMode.isBlackKey(7)); // G
    assertTrue(GridMode.isBlackKey(8)); // G#
    assertFalse(GridMode.isBlackKey(9)); // A
    assertTrue(GridMode.isBlackKey(10)); // A#
    assertFalse(GridMode.isBlackKey(11)); // B
    assertFalse(GridMode.isBlackKey(60)); // C4 (middle C)
    assertTrue(GridMode.isBlackKey(61)); // C#4
  }

  @Test
  public void testIsBlackKey_allOctaves() {
    boolean[] expected = {
      false, true, false, true, false, false, true, false, true, false, true, false
    };
    for (int octave = 0; octave < 11; octave++) {
      for (int note = 0; note < 12; note++) {
        int pitch = octave * 12 + note;
        if (pitch >= 128) break;
        assertEquals(expected[note], GridMode.isBlackKey(pitch));
      }
    }
  }

  // ═══════════════════════════════════════════════════════════════════════
  // GridMode — getTickInterval
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testGridModeTickInterval() {
    assertEquals(96, GridMode.QUARTER.getTickInterval(96));
    assertEquals(96 * 4, GridMode.BAR.getTickInterval(96));
    assertEquals(48, GridMode.EIGHTH.getTickInterval(96));
  }

  @Test
  public void testGetTickInterval_allModes_res480() {
    int r = 480;
    assertEquals(1920, GridMode.BAR.getTickInterval(r));
    assertEquals(960, GridMode.HALF.getTickInterval(r));
    assertEquals(480, GridMode.QUARTER.getTickInterval(r));
    assertEquals(240, GridMode.EIGHTH.getTickInterval(r));
    assertEquals(120, GridMode.SIXTEENTH.getTickInterval(r));
    assertEquals(60, GridMode.THIRTY_SECOND.getTickInterval(r));
    assertEquals(640, GridMode.TRIPLET_QUARTER.getTickInterval(r));
    assertEquals(320, GridMode.TRIPLET_EIGHTH.getTickInterval(r));
    assertEquals(160, GridMode.TRIPLET_16TH.getTickInterval(r));
    assertEquals(80, GridMode.TRIPLET_32ND.getTickInterval(r));
  }

  // ═══════════════════════════════════════════════════════════════════════
  // GridMode — autoTickInterval
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testAutoTickInterval_zoomedIn() {
    assertEquals(60, GridMode.autoTickInterval(480, 2.0f, 15));
  }

  @Test
  public void testAutoTickInterval_zoomedOut() {
    assertEquals(1920, GridMode.autoTickInterval(480, 0.01f, 15));
  }

  @Test
  public void testAutoTickInterval_mediumZoom() {
    // 1/8 = 240 * 0.1 = 24 >= 15
    assertEquals(240, GridMode.autoTickInterval(480, 0.1f, 15));
  }

  @Test
  public void testAutoTickInterval_exactBoundary() {
    // 1/16 = 120 * 0.125 = 15.0 exactly
    assertEquals(120, GridMode.autoTickInterval(480, 0.125f, 15));
  }

  // ═══════════════════════════════════════════════════════════════════════
  // GridMode — getSecondsInterval
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testGetSecondsInterval() {
    float spb = 0.5f; // 120 BPM
    assertEquals(0.5f, GridMode.QUARTER.getSecondsInterval(spb), 0.001f);
    assertEquals(0.25f, GridMode.EIGHTH.getSecondsInterval(spb), 0.001f);
    assertEquals(2.0f, GridMode.BAR.getSecondsInterval(spb), 0.001f);
    assertEquals(1.0f, GridMode.SECONDS.getSecondsInterval(spb), 0.001f);
  }

  // ═══════════════════════════════════════════════════════════════════════
  // GridMode — autoSecondsInterval
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testAutoSecondsInterval_zoomedIn() {
    assertEquals(0.5f / 8, GridMode.autoSecondsInterval(0.5f, 500.0f, 15), 0.001f);
  }

  @Test
  public void testAutoSecondsInterval_zoomedOut() {
    assertEquals(2.0f, GridMode.autoSecondsInterval(0.5f, 1.0f, 15), 0.001f);
  }

  // ═══════════════════════════════════════════════════════════════════════
  // GridMode — labels and enum
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testGridModeLabels() {
    assertEquals("Auto", GridMode.AUTO.toString());
    assertEquals("1/4", GridMode.QUARTER.toString());
    assertEquals("1/8", GridMode.EIGHTH.toString());
    assertEquals("1/16", GridMode.SIXTEENTH.toString());
    assertEquals("1/1", GridMode.BAR.toString());
    assertEquals("Seconds", GridMode.SECONDS.toString());
  }

  @Test
  public void testGridMode_allValues() {
    assertEquals(12, GridMode.values().length);
  }

  @Test
  public void testGridMode_valueOf() {
    assertEquals(GridMode.AUTO, GridMode.valueOf("AUTO"));
    assertEquals(GridMode.QUARTER, GridMode.valueOf("QUARTER"));
    assertEquals(GridMode.TRIPLET_EIGHTH, GridMode.valueOf("TRIPLET_EIGHTH"));
  }

  // ═══════════════════════════════════════════════════════════════════════
  // MidiDataModel — parseTrack
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testParseTrack_singleNote() throws Exception {
    Sequence seq = new Sequence(Sequence.PPQ, 480);
    Track track = seq.createTrack();
    track.add(new MidiEvent(new ShortMessage(ShortMessage.NOTE_ON, 0, 60, 100), 0));
    track.add(new MidiEvent(new ShortMessage(ShortMessage.NOTE_OFF, 0, 60, 0), 480));

    MidiDataModel model = new MidiDataModel();
    model.parseTrack(track);

    assertEquals(1, model.notes.size());
    assertEquals(60, model.notes.get(0).pitch);
    assertEquals(0, model.notes.get(0).startTick);
    assertEquals(480, model.notes.get(0).durationTicks);
    assertEquals(100, model.notes.get(0).velocity);
  }

  @Test
  public void testParseTrack_multipleNotes() throws Exception {
    Sequence seq = new Sequence(Sequence.PPQ, 480);
    Track track = seq.createTrack();
    track.add(new MidiEvent(new ShortMessage(ShortMessage.NOTE_ON, 0, 60, 80), 0));
    track.add(new MidiEvent(new ShortMessage(ShortMessage.NOTE_OFF, 0, 60, 0), 480));
    track.add(new MidiEvent(new ShortMessage(ShortMessage.NOTE_ON, 0, 64, 120), 480));
    track.add(new MidiEvent(new ShortMessage(ShortMessage.NOTE_OFF, 0, 64, 0), 720));

    MidiDataModel model = new MidiDataModel();
    model.parseTrack(track);

    assertEquals(2, model.notes.size());
    assertEquals(60, model.notes.get(0).pitch);
    assertEquals(64, model.notes.get(1).pitch);
  }

  @Test
  public void testParseTrack_velocityZeroAsNoteOff() throws Exception {
    Sequence seq = new Sequence(Sequence.PPQ, 480);
    Track track = seq.createTrack();
    track.add(new MidiEvent(new ShortMessage(ShortMessage.NOTE_ON, 0, 60, 100), 0));
    track.add(new MidiEvent(new ShortMessage(ShortMessage.NOTE_ON, 0, 60, 0), 240));

    MidiDataModel model = new MidiDataModel();
    model.parseTrack(track);

    assertEquals(1, model.notes.size());
    assertEquals(240, model.notes.get(0).durationTicks);
  }

  @Test
  public void testParseTrack_emptyTrack() throws Exception {
    Sequence seq = new Sequence(Sequence.PPQ, 480);
    Track track = seq.createTrack();

    MidiDataModel model = new MidiDataModel();
    model.parseTrack(track);
    assertTrue(model.notes.isEmpty());
  }

  @Test
  public void testParseTrack_consecutiveSamePitch() throws Exception {
    Sequence seq = new Sequence(Sequence.PPQ, 480);
    Track track = seq.createTrack();
    track.add(new MidiEvent(new ShortMessage(ShortMessage.NOTE_ON, 0, 60, 100), 0));
    track.add(new MidiEvent(new ShortMessage(ShortMessage.NOTE_OFF, 0, 60, 0), 240));
    track.add(new MidiEvent(new ShortMessage(ShortMessage.NOTE_ON, 0, 60, 90), 480));
    track.add(new MidiEvent(new ShortMessage(ShortMessage.NOTE_OFF, 0, 60, 0), 720));

    MidiDataModel model = new MidiDataModel();
    model.parseTrack(track);

    assertEquals(2, model.notes.size());
    assertEquals(240, model.notes.get(0).durationTicks);
    assertEquals(240, model.notes.get(1).durationTicks);
  }

  @Test
  public void testParseTrack_setsOnOffEvents() throws Exception {
    Sequence seq = new Sequence(Sequence.PPQ, 480);
    Track track = seq.createTrack();
    track.add(new MidiEvent(new ShortMessage(ShortMessage.NOTE_ON, 0, 60, 100), 0));
    track.add(new MidiEvent(new ShortMessage(ShortMessage.NOTE_OFF, 0, 60, 0), 480));

    MidiDataModel model = new MidiDataModel();
    model.parseTrack(track);

    assertNotNull(model.notes.get(0).onEvent);
    assertNotNull(model.notes.get(0).offEvent);
  }

  // ═══════════════════════════════════════════════════════════════════════
  // MidiDataModel — hasNotes
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testHasNotes_true() throws Exception {
    Sequence seq = new Sequence(Sequence.PPQ, 480);
    Track track = seq.createTrack();
    track.add(new MidiEvent(new ShortMessage(ShortMessage.NOTE_ON, 0, 60, 100), 0));

    MidiDataModel model = new MidiDataModel();
    assertTrue(model.hasNotes(track));
  }

  @Test
  public void testHasNotes_empty() throws Exception {
    Sequence seq = new Sequence(Sequence.PPQ, 480);
    Track track = seq.createTrack();

    MidiDataModel model = new MidiDataModel();
    assertFalse(model.hasNotes(track));
  }

  @Test
  public void testHasNotes_ccOnly() throws Exception {
    Sequence seq = new Sequence(Sequence.PPQ, 480);
    Track track = seq.createTrack();
    track.add(new MidiEvent(new ShortMessage(ShortMessage.CONTROL_CHANGE, 0, 7, 100), 0));

    MidiDataModel model = new MidiDataModel();
    assertFalse(model.hasNotes(track));
  }

  // ═══════════════════════════════════════════════════════════════════════
  // MidiDataModel — loadFromBackendData
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testLoadFromBackendData() {
    hibiki.pb.notifications.ClipMidiData data =
        hibiki.pb.notifications.ClipMidiData.newBuilder()
            .setResolution(480)
            .addEvents(
                hibiki.pb.core.MidiEvent.newBuilder()
                    .setPitch(60)
                    .setTick(0)
                    .setDurationTicks(480)
                    .setVelocity(100))
            .addEvents(
                hibiki.pb.core.MidiEvent.newBuilder()
                    .setPitch(64)
                    .setTick(480)
                    .setDurationTicks(240)
                    .setVelocity(80))
            .build();

    MidiDataModel model = new MidiDataModel();
    model.loadFromBackendData(data);

    assertEquals(2, model.notes.size());
    assertEquals(60, model.notes.get(0).pitch);
    assertEquals(480, model.notes.get(0).durationTicks);
    assertEquals(64, model.notes.get(1).pitch);
    assertEquals(480, model.sequence.getResolution());
  }

  @Test
  public void testLoadFromBackendData_replacesExisting() {
    MidiDataModel model = new MidiDataModel();
    model.notes.add(new MidiDataModel.Note(50, 0, 100, 60));
    assertEquals(1, model.notes.size());

    hibiki.pb.notifications.ClipMidiData data =
        hibiki.pb.notifications.ClipMidiData.newBuilder()
            .setResolution(960)
            .addEvents(
                hibiki.pb.core.MidiEvent.newBuilder()
                    .setPitch(72)
                    .setTick(0)
                    .setDurationTicks(960)
                    .setVelocity(127))
            .build();
    model.loadFromBackendData(data);

    assertEquals(1, model.notes.size());
    assertEquals(72, model.notes.get(0).pitch);
    assertEquals(960, model.sequence.getResolution());
  }

  @Test
  public void testLoadFromBackendData_emptyEvents() {
    hibiki.pb.notifications.ClipMidiData data =
        hibiki.pb.notifications.ClipMidiData.newBuilder().setResolution(480).build();
    MidiDataModel model = new MidiDataModel();
    model.loadFromBackendData(data);
    assertTrue(model.notes.isEmpty());
  }

  // ═══════════════════════════════════════════════════════════════════════
  // PianoRollMouseHandler — tickToAbsoluteSeconds (static, headless-safe)
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testTickToAbsoluteSeconds_atOrigin() {
    assertEquals(0.0f, PianoRollMouseHandler.tickToAbsoluteSeconds(0, 480, 120.0f, 0.0f), 0.001f);
  }

  @Test
  public void testTickToAbsoluteSeconds_oneBeat() {
    // 480 ticks at 120 BPM = 0.5 seconds
    assertEquals(0.5f, PianoRollMouseHandler.tickToAbsoluteSeconds(480, 480, 120.0f, 0.0f), 0.001f);
  }

  @Test
  public void testTickToAbsoluteSeconds_withClipOffset() {
    assertEquals(5.5f, PianoRollMouseHandler.tickToAbsoluteSeconds(480, 480, 120.0f, 5.0f), 0.001f);
  }

  @Test
  public void testTickToAbsoluteSeconds_slowTempo() {
    // 480 ticks at 60 BPM = 1.0 second
    assertEquals(1.0f, PianoRollMouseHandler.tickToAbsoluteSeconds(480, 480, 60.0f, 0.0f), 0.001f);
  }

  @Test
  public void testTickToAbsoluteSeconds_fastTempo() {
    // 480 ticks at 240 BPM = 0.25 seconds
    assertEquals(
        0.25f, PianoRollMouseHandler.tickToAbsoluteSeconds(480, 480, 240.0f, 0.0f), 0.001f);
  }

  @Test
  public void testTickToAbsoluteSeconds_lowResolution() {
    // 96 ticks at 120 BPM, res=96 = 1 beat = 0.5 seconds
    assertEquals(0.5f, PianoRollMouseHandler.tickToAbsoluteSeconds(96, 96, 120.0f, 0.0f), 0.001f);
  }

  @Test
  public void testTickToAbsoluteSeconds_fourBars() {
    // 16 beats = 7680 ticks at 120 BPM = 8.0 seconds
    assertEquals(
        8.0f, PianoRollMouseHandler.tickToAbsoluteSeconds(7680, 480, 120.0f, 0.0f), 0.001f);
  }

  // ═══════════════════════════════════════════════════════════════════════
  // PianoRollRenderer — getEffectiveBpbAtTick (static, headless-safe)
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testGetEffectiveBpbAtTick_noMarkers() {
    List<TimelineView.TimelineMarker> empty = Collections.emptyList();
    assertEquals(4, PianoRollRenderer.getEffectiveBpbAtTick(0, 480, 0.0f, 120.0f, 4, empty));
  }

  @Test
  public void testGetEffectiveBpbAtTick_differentGlobal() {
    List<TimelineView.TimelineMarker> empty = Collections.emptyList();
    assertEquals(3, PianoRollRenderer.getEffectiveBpbAtTick(0, 480, 0.0f, 120.0f, 3, empty));
  }

  @Test
  public void testGetEffectiveBpbAtTick_beforeMarker() {
    List<TimelineView.TimelineMarker> markers = new ArrayList<>();
    TimelineView.TimelineMarker m = new TimelineView.TimelineMarker("marker", 2.0f);
    m.beatsPerBar = 3;
    markers.add(m);

    assertEquals(4, PianoRollRenderer.getEffectiveBpbAtTick(0, 480, 0.0f, 120.0f, 4, markers));
  }

  @Test
  public void testGetEffectiveBpbAtTick_afterMarker() {
    List<TimelineView.TimelineMarker> markers = new ArrayList<>();
    TimelineView.TimelineMarker m = new TimelineView.TimelineMarker("marker", 2.0f);
    m.beatsPerBar = 3;
    markers.add(m);

    // tick 2400 at 120BPM, res=480 → 2.5s > 2.0s → uses marker
    assertEquals(3, PianoRollRenderer.getEffectiveBpbAtTick(2400, 480, 0.0f, 120.0f, 4, markers));
  }

  @Test
  public void testGetEffectiveBpbAtTick_multipleMarkers() {
    List<TimelineView.TimelineMarker> markers = new ArrayList<>();
    TimelineView.TimelineMarker m1 = new TimelineView.TimelineMarker("marker1", 1.0f);
    m1.beatsPerBar = 3;
    markers.add(m1);
    TimelineView.TimelineMarker m2 = new TimelineView.TimelineMarker("marker2", 4.0f);
    m2.beatsPerBar = 7;
    markers.add(m2);

    // Before both
    assertEquals(4, PianoRollRenderer.getEffectiveBpbAtTick(0, 480, 0.0f, 120.0f, 4, markers));
    // Between markers (tick 1440 → 1.5s)
    assertEquals(3, PianoRollRenderer.getEffectiveBpbAtTick(1440, 480, 0.0f, 120.0f, 4, markers));
    // After both (tick 4800 → 5.0s)
    assertEquals(7, PianoRollRenderer.getEffectiveBpbAtTick(4800, 480, 0.0f, 120.0f, 4, markers));
  }

  @Test
  public void testGetEffectiveBpbAtTick_withClipOffset() {
    List<TimelineView.TimelineMarker> markers = new ArrayList<>();
    TimelineView.TimelineMarker m = new TimelineView.TimelineMarker("marker", 5.0f);
    m.beatsPerBar = 6;
    markers.add(m);

    // Clip starts at 4.0s, tick 960 → 1.0s relative → 5.0s abs = marker pos
    assertEquals(6, PianoRollRenderer.getEffectiveBpbAtTick(960, 480, 4.0f, 120.0f, 4, markers));
  }

  // ═══════════════════════════════════════════════════════════════════════
  // PianoRoll instance tests (headless-guarded)
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testGridTickInterval_autoMode() {
    if (java.awt.GraphicsEnvironment.isHeadless()) return;
    java.io.File dummyFile = new java.io.File("dummy.mid");
    PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);
    assertTrue(pr.getGridTickInterval() > 0);
    pr.dispose();
  }

  @Test
  public void testSnapTickInterval() {
    if (java.awt.GraphicsEnvironment.isHeadless()) return;
    java.io.File dummyFile = new java.io.File("dummy.mid");
    PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);
    assertEquals(pr.getGridTickInterval(), pr.getSnapTickInterval());
    pr.dispose();
  }

  @Test
  public void testGetTickWidth() {
    if (java.awt.GraphicsEnvironment.isHeadless()) return;
    java.io.File dummyFile = new java.io.File("dummy.mid");
    PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);
    assertTrue(pr.getTickWidth() > 0);
    pr.dispose();
  }

  @Test
  public void testLoadMidi_nonExistentFile() {
    if (java.awt.GraphicsEnvironment.isHeadless()) return;
    java.io.File dummyFile = new java.io.File("nonexistent_file.mid");
    PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);
    assertNotNull(pr.sequence);
    assertNotNull(pr.notes);
    pr.dispose();
  }

  @Test
  public void testPlayheadFields() {
    if (java.awt.GraphicsEnvironment.isHeadless()) return;
    java.io.File dummyFile = new java.io.File("dummy.mid");
    PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 5.0f);
    assertEquals(5.0f, pr.clipStartTime, 0.01f);
    assertEquals(0.0f, pr.playheadPos, 0.01f);
    assertEquals(120.0f, pr.bpm, 0.01f);
    pr.dispose();
  }

  @Test
  public void testInitialization() {
    if (java.awt.GraphicsEnvironment.isHeadless()) return;
    java.io.File dummyFile = new java.io.File("dummy.mid");
    PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);
    assertEquals("Piano Roll - dummy.mid", pr.getTitle());
    pr.dispose();
  }

  @Test
  public void testGridPanelCreated() {
    if (java.awt.GraphicsEnvironment.isHeadless()) return;
    java.io.File dummyFile = new java.io.File("dummy.mid");
    PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);
    assertNotNull(pr.gridPanel);
    assertNotNull(pr.keysPanel);
    assertNotNull(pr.velocityPanel);
    pr.dispose();
  }

  @Test
  public void testDraggingStateDefaults() {
    if (java.awt.GraphicsEnvironment.isHeadless()) return;
    java.io.File dummyFile = new java.io.File("dummy.mid");
    PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);
    assertNull(pr.draggingNote);
    assertNull(pr.resizingNote);
    assertFalse(pr.isDraggingNote);
    assertFalse(pr.editingVelocity);
    pr.dispose();
  }

  @Test
  public void testDispose() {
    if (java.awt.GraphicsEnvironment.isHeadless()) return;
    java.io.File dummyFile = new java.io.File("dummy.mid");
    PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);
    pr.dispose();
    pr.dispose(); // Double dispose should not throw
  }
}
