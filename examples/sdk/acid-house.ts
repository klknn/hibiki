// @ts
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

const bass = hibiki.liveSet.tracks.at(0);
bass.devices.load("builtin://3xosc", 0);
// Bounce renders the arrangement, so create an in-memory MIDI clip there first.
bass.arrangementClips.createClip("", { positionBeats: 0, lengthBeats: 4 });
bass.arrangementClips.at(0).midi.replaceNotes(ppq, notes);
bass.mixerDevice.volume.setNormalized(0.82);
hibiki.liveSet.tempo.setBpm(128);
hibiki.liveSet.transport.start();
hibiki.liveSet.bounce("acid-house.wav");
