/** Generate a C-major arpeggio in a session MIDI clip. */
const ppq = 480;
const sixteenth = ppq / 4;
const chord = [60, 64, 67, 71];
const notes: MidiNote[] = [];

for (let step = 0; step < 32; step++) {
  notes.push({
    tick: step * sixteenth,
    pitch: chord[step % chord.length],
    dur: sixteenth - 8,
    vel: step % 4 === 0 ? 108 : 78,
  });
}

const clip = hibiki.tracks.at(0).session.slot(0);
clip.midi.replaceNotes(ppq, notes);
clip.play();
