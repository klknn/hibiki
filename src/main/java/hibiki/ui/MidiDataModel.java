package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.notifications.ClipMidiData;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Level;
import java.util.logging.Logger;
import javax.sound.midi.*;
import javax.swing.*;

/**
 * Pure data model for MIDI note storage, parsing, and IPC synchronization. Separates MIDI data
 * logic from PianoRoll UI rendering.
 */
class MidiDataModel {
  private static final Logger LOG = Logger.getLogger(MidiDataModel.class.getName());

  /** A single MIDI note with pitch, timing, and velocity. */
  static class Note {
    int pitch;
    long startTick;
    long durationTicks;
    int velocity;
    javax.sound.midi.MidiEvent onEvent;
    javax.sound.midi.MidiEvent offEvent;

    Note(int pitch, long startTick, long durationTicks, int velocity) {
      this.pitch = pitch;
      this.startTick = startTick;
      this.durationTicks = durationTicks;
      this.velocity = velocity;
    }
  }

  /** A CC or pitch bend event at a specific tick. */
  static class CCEvent {
    int ccNumber; // 0-127 for CC, 128 for pitch bend
    long tick;
    int value; // CC: 0-127, pitch bend: -8192..+8191

    CCEvent(int ccNumber, long tick, int value) {
      this.ccNumber = ccNumber;
      this.tick = tick;
      this.value = value;
    }
  }

  final List<Note> notes = new ArrayList<>();
  final List<CCEvent> ccEvents = new ArrayList<>();
  Sequence sequence;
  Track midiTrack;

  /** Load MIDI from file, falling back to empty sequence on error. */
  void loadMidi(File midiFile) {
    notes.clear();
    ccEvents.clear();
    try {
      if (midiFile.exists()) {
        sequence = MidiSystem.getSequence(midiFile);
        if (sequence.getTracks().length > 0) {
          for (Track t : sequence.getTracks()) {
            if (hasNotes(t)) {
              midiTrack = t;
              break;
            }
          }
          if (midiTrack == null) midiTrack = sequence.getTracks()[0];
        } else {
          sequence = new Sequence(Sequence.PPQ, 96);
          midiTrack = sequence.createTrack();
        }
      } else {
        sequence = new Sequence(Sequence.PPQ, 96);
        midiTrack = sequence.createTrack();
      }
      parseTrack(midiTrack);
    } catch (Exception e) {
      LOG.log(Level.WARNING, "Failed to load MIDI", e);
      try {
        sequence = new Sequence(Sequence.PPQ, 96);
        midiTrack = sequence.createTrack();
      } catch (Exception ex) {
      }
    }
  }

  /** Check if a MIDI track contains any NOTE_ON events. */
  boolean hasNotes(Track t) {
    for (int i = 0; i < t.size(); i++) {
      MidiMessage msg = t.get(i).getMessage();
      if (msg instanceof ShortMessage) {
        ShortMessage sm = (ShortMessage) msg;
        if (sm.getCommand() == ShortMessage.NOTE_ON) return true;
      }
    }
    return false;
  }

  /** Parse NOTE_ON/OFF events and CC/PitchBend events from a track. */
  void parseTrack(Track track) {
    Note[] pendingNotes = new Note[128];

    for (int i = 0; i < track.size(); i++) {
      javax.sound.midi.MidiEvent event = track.get(i);
      MidiMessage msg = event.getMessage();

      if (msg instanceof ShortMessage) {
        ShortMessage sm = (ShortMessage) msg;
        int cmd = sm.getCommand();
        int pitch = sm.getData1();
        int vel = sm.getData2();

        if (cmd == ShortMessage.NOTE_ON && vel > 0) {
          if (pendingNotes[pitch] == null) {
            Note n = new Note(pitch, event.getTick(), 0, vel);
            n.onEvent = event;
            pendingNotes[pitch] = n;
          }
        } else if (cmd == ShortMessage.NOTE_OFF || (cmd == ShortMessage.NOTE_ON && vel == 0)) {
          if (pendingNotes[pitch] != null) {
            Note n = pendingNotes[pitch];
            n.durationTicks = event.getTick() - n.startTick;
            n.offEvent = event;
            notes.add(n);
            pendingNotes[pitch] = null;
          }
        } else if (cmd == ShortMessage.CONTROL_CHANGE) {
          ccEvents.add(new CCEvent(sm.getData1(), event.getTick(), sm.getData2()));
        } else if (cmd == ShortMessage.PITCH_BEND) {
          // MIDI pitch bend: data1=LSB, data2=MSB, center=8192
          int bendValue = ((sm.getData2() & 0x7F) << 7) | (sm.getData1() & 0x7F);
          ccEvents.add(new CCEvent(128, event.getTick(), bendValue - 8192));
        }
      }
    }
  }

  /** Sync all notes and CC events to backend via IPC (immediate in-memory update). */
  void syncToBackend(int trackIdx, int slotIdx, int clipIdx) {
    int resolution = (sequence != null) ? sequence.getResolution() : 480;
    int totalCount = notes.size() + ccEvents.size();
    long[] ticks = new long[totalCount];
    int[] pitches = new int[totalCount];
    long[] durations = new long[totalCount];
    int[] velocities = new int[totalCount];

    for (int i = 0; i < notes.size(); i++) {
      Note n = notes.get(i);
      ticks[i] = n.startTick;
      pitches[i] = n.pitch;
      durations[i] = Math.max(1, n.durationTicks);
      velocities[i] = n.velocity;
    }

    // CC events are sent separately via the proto cc_number/cc_value fields
    // For now, send only notes through the existing updateClipMidi path
    BackendManager.getInstance()
        .updateClipMidi(
            trackIdx,
            slotIdx,
            clipIdx,
            resolution,
            java.util.Arrays.copyOf(ticks, notes.size()),
            java.util.Arrays.copyOf(pitches, notes.size()),
            java.util.Arrays.copyOf(durations, notes.size()),
            java.util.Arrays.copyOf(velocities, notes.size()));
  }

  /** Load notes and CC events from backend IPC data (replaces local data). */
  void loadFromBackendData(ClipMidiData data) {
    notes.clear();
    ccEvents.clear();
    for (int i = 0; i < data.getEventsCount(); i++) {
      hibiki.pb.core.MidiEvent ev = data.getEvents(i);
      if (ev.getCcNumber() > 0
          || (ev.getCcNumber() == 0
              && ev.getCcValue() != 0
              && ev.getPitch() == 0
              && ev.getVelocity() == 0)) {
        // CC or pitch bend event
        ccEvents.add(new CCEvent((int) ev.getCcNumber(), ev.getTick(), ev.getCcValue()));
      } else {
        notes.add(
            new Note(
                (int) ev.getPitch(), ev.getTick(), ev.getDurationTicks(), (int) ev.getVelocity()));
      }
    }

    // Update sequence resolution if provided
    if (data.getResolution() > 0) {
      try {
        sequence = new Sequence(Sequence.PPQ, data.getResolution());
        midiTrack = sequence.createTrack();
      } catch (Exception e) {
        LOG.log(Level.WARNING, "Failed to update sequence resolution", e);
      }
    }
  }
}
