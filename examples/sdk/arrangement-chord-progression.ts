// @ts
/**
 * Write a four-bar minor chord progression to the arrangement and render it.
 *
 * This demonstrates named beat units and arrangement clip access.
 */
const ppq = 480;
const beatsPerBar = 4;
const chordProgression = [
  [57, 60, 64], // Am
  [53, 57, 60], // F
  [48, 52, 55], // C
  [55, 59, 62], // G
];
const notes: MidiNote[] = [];

for (let bar = 0; bar < chordProgression.length; bar++) {
  for (const pitch of chordProgression[bar]) {
    notes.push({
      tick: bar * beatsPerBar * ppq,
      pitch,
      dur: beatsPerBar * ppq - 24,
      vel: 78,
    });
  }
}

const liveSet = hibiki.liveSet;
const track = liveSet.tracks.at(0);
track.devices.load("builtin://3xosc", 0);
track.mixerDevice.volume.setNormalized(0.65);
track.mixerDevice.pan.setNormalized(0.5);

track.arrangementClips.createClip("", {
  positionBeats: 0,
  lengthBeats: chordProgression.length * beatsPerBar,
});
track.arrangementClips.at(0).midi.replaceNotes(ppq, notes);
liveSet.tempo.setBpm(108);
liveSet.transport.start();
liveSet.bounce("chord-progression.wav");
