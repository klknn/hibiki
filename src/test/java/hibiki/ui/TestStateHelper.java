package hibiki.ui;

import static org.junit.Assert.*;

import hibiki.pb.notifications.ClipMidiData;
import java.util.List;

/**
 * Shared test helpers for building and comparing MidiDataModel state. Used across PianoRollTest,
 * MidiDataModelTest, and other UI E2E tests.
 */
class TestStateHelper {

  /** Create a MidiDataModel with notes at the given pitches (all at tick=0, duration=480). */
  static MidiDataModel createModelWithNotes(int... pitches) {
    MidiDataModel model = new MidiDataModel();
    try {
      model.sequence = new javax.sound.midi.Sequence(javax.sound.midi.Sequence.PPQ, 480);
      model.midiTrack = model.sequence.createTrack();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
    for (int i = 0; i < pitches.length; i++) {
      model.notes.add(new MidiDataModel.Note(pitches[i], i * 480L, 480, 100));
    }
    return model;
  }

  /** Create a MidiDataModel with CC events at given values (all at ccNumber, ticks spaced 240). */
  static MidiDataModel createModelWithCCEvents(int ccNumber, int... values) {
    MidiDataModel model = new MidiDataModel();
    try {
      model.sequence = new javax.sound.midi.Sequence(javax.sound.midi.Sequence.PPQ, 480);
      model.midiTrack = model.sequence.createTrack();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
    for (int i = 0; i < values.length; i++) {
      model.ccEvents.add(new MidiDataModel.CCEvent(ccNumber, i * 240L, values[i]));
    }
    return model;
  }

  /** Build a ClipMidiData proto from notes (for round-trip testing). */
  static ClipMidiData buildClipMidiData(int resolution, MidiDataModel.Note... notes) {
    ClipMidiData.Builder builder = ClipMidiData.newBuilder();
    builder.setResolution(resolution);
    for (MidiDataModel.Note n : notes) {
      builder.addEvents(
          hibiki.pb.core.MidiEvent.newBuilder()
              .setTick(n.startTick)
              .setPitch(n.pitch)
              .setDurationTicks(n.durationTicks)
              .setVelocity(n.velocity)
              .build());
    }
    return builder.build();
  }

  /** Build a ClipMidiData proto from notes and CC events. */
  static ClipMidiData buildClipMidiDataWithCC(
      int resolution, List<MidiDataModel.Note> notes, List<MidiDataModel.CCEvent> ccEvents) {
    ClipMidiData.Builder builder = ClipMidiData.newBuilder();
    builder.setResolution(resolution);
    for (MidiDataModel.Note n : notes) {
      builder.addEvents(
          hibiki.pb.core.MidiEvent.newBuilder()
              .setTick(n.startTick)
              .setPitch(n.pitch)
              .setDurationTicks(n.durationTicks)
              .setVelocity(n.velocity)
              .build());
    }
    for (MidiDataModel.CCEvent cc : ccEvents) {
      builder.addEvents(
          hibiki.pb.core.MidiEvent.newBuilder()
              .setTick(cc.tick)
              .setCcNumber(cc.ccNumber)
              .setCcValue(cc.value)
              .build());
    }
    return builder.build();
  }

  /** Assert that two note lists match on pitch, startTick, durationTicks, velocity. */
  static void assertNotesEqual(List<MidiDataModel.Note> expected, List<MidiDataModel.Note> actual) {
    assertEquals("Note count mismatch", expected.size(), actual.size());
    for (int i = 0; i < expected.size(); i++) {
      MidiDataModel.Note e = expected.get(i);
      MidiDataModel.Note a = actual.get(i);
      assertEquals("Note[" + i + "] pitch", e.pitch, a.pitch);
      assertEquals("Note[" + i + "] startTick", e.startTick, a.startTick);
      assertEquals("Note[" + i + "] durationTicks", e.durationTicks, a.durationTicks);
      assertEquals("Note[" + i + "] velocity", e.velocity, a.velocity);
    }
  }

  /** Assert that two CC event lists match on ccNumber, tick, value. */
  static void assertCCEventsEqual(
      List<MidiDataModel.CCEvent> expected, List<MidiDataModel.CCEvent> actual) {
    assertEquals("CCEvent count mismatch", expected.size(), actual.size());
    for (int i = 0; i < expected.size(); i++) {
      MidiDataModel.CCEvent e = expected.get(i);
      MidiDataModel.CCEvent a = actual.get(i);
      assertEquals("CC[" + i + "] ccNumber", e.ccNumber, a.ccNumber);
      assertEquals("CC[" + i + "] tick", e.tick, a.tick);
      assertEquals("CC[" + i + "] value", e.value, a.value);
    }
  }
}
