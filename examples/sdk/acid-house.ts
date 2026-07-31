/** Create a one-bar 303-style bassline and render the project. */
const ppq = 480;
const step = ppq / 4;
const pattern = [36, 36, 43, 36, 48, 43, 36, 31, 36, 43, 48, 43, 36, 31, 36, 43];
const notes: MidiNote[] = pattern.map((pitch, index) => ({
  tick: index * step,
  pitch,
  dur: index % 3 === 0 ? step * 2 - 12 : step - 12,
  vel: index % 4 === 0 ? 118 : 92,
}));

const bass = hibiki.tracks.at(0);
bass.devices.load("builtin://3xosc", 0);
bass.session.slot(0).midi.replaceNotes(ppq, notes);
bass.mixer.setVolume(0.82);
hibiki.project.setBpm(128);
hibiki.transport.play();
hibiki.project.bounce("acid-house.wav");
