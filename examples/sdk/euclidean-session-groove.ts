// @ts
/**
 * Create a 5-in-16 Euclidean rhythm in a Session View MIDI clip and launch it.
 *
 * This demonstrates the Live Set → Track → ClipSlot → Clip hierarchy.
 */
const ppq = 480;
const steps = 16;
const pulses = 5;
const sixteenth = ppq / 4;
const notes: MidiNote[] = [];

for (let step = 0; step < steps; step++) {
  // Evenly distribute five accents across one 16th-note bar.
  if ((step * pulses) % steps < pulses) {
    notes.push({
      tick: step * sixteenth,
      pitch: step % 8 === 0 ? 48 : 36,
      dur: sixteenth - 12,
      vel: step % 4 === 0 ? 112 : 84,
    });
  }
}

const liveSet = hibiki.liveSet;
const track = liveSet.tracks.at(0);
track.devices.load("builtin://3xosc", 0);
track.mixerDevice.volume.setNormalized(0.72);

const clipSlot = track.clipSlots.at(0);
clipSlot.loadClip("testdata/test.mid", true);
clipSlot.clip.midi.replaceNotes(ppq, notes);
clipSlot.fire();
