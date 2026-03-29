package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.notifications.ClipMidiData;
import hibiki.pb.core.MidiEvent;

import javax.sound.midi.*;
import javax.swing.*;
import java.io.File;
import java.util.ArrayList;
import java.util.List;

/**
 * Pure data model for MIDI note storage, parsing, and IPC synchronization.
 * Separates MIDI data logic from PianoRoll UI rendering.
 */
class MidiDataModel {

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

    final List<Note> notes = new ArrayList<>();
    Sequence sequence;
    Track midiTrack;

    /** Load MIDI from file, falling back to empty sequence on error. */
    void loadMidi(File midiFile) {
        notes.clear();
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
                    if (midiTrack == null)
                        midiTrack = sequence.getTracks()[0];
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
            e.printStackTrace();
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
                if (sm.getCommand() == ShortMessage.NOTE_ON)
                    return true;
            }
        }
        return false;
    }

    /** Parse NOTE_ON/OFF events from a track into the notes list. */
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
                }
            }
        }
    }

    /** Sync all notes to backend via IPC (immediate in-memory update). */
    void syncToBackend(int trackIdx, int slotIdx, int clipIdx) {
        int resolution = (sequence != null) ? sequence.getResolution() : 480;
        long[] ticks = new long[notes.size()];
        int[] pitches = new int[notes.size()];
        long[] durations = new long[notes.size()];
        int[] velocities = new int[notes.size()];

        for (int i = 0; i < notes.size(); i++) {
            Note n = notes.get(i);
            ticks[i] = n.startTick;
            pitches[i] = n.pitch;
            durations[i] = Math.max(1, n.durationTicks);
            velocities[i] = n.velocity;
        }

        BackendManager.getInstance().updateClipMidi(trackIdx, slotIdx, clipIdx, resolution, ticks, pitches, durations,
                velocities);
    }

    /** Load notes from backend IPC data (replaces local notes). */
    void loadFromBackendData(ClipMidiData data) {
        notes.clear();
        // Each MidiEvent is a complete note (tick, pitch, durationTicks, velocity)
        for (int i = 0; i < data.getEventsCount(); i++) {
            hibiki.pb.core.MidiEvent ev = data.getEvents(i);
            notes.add(new Note(ev.getPitch(), ev.getTick(), ev.getDurationTicks(), ev.getVelocity()));
        }

        // Update sequence resolution if provided
        if (data.getResolution() > 0) {
            try {
                sequence = new Sequence(Sequence.PPQ, data.getResolution());
                midiTrack = sequence.createTrack();
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }
}
