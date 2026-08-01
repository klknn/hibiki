/** Canonical public TypeScript contract for the Hibiki scripting SDK. */

export {};

declare global {
interface MidiNote {
  readonly tick: number;
  readonly pitch: number;
  readonly dur: number;
  readonly vel: number;
}

/** A MIDI clip's editable note content. */
interface MidiClip {
  replaceNotes(resolution: number, notes: readonly MidiNote[]): void;
  getNotes(): void;
}

/** A clip addressed by a Session slot or the arrangement. */
interface Clip {
  readonly midi: MidiClip;
}

/** A Session View slot on a track. */
interface ClipSlot {
  /** Valid only while the slot contains a compatible clip. */
  readonly clip: Clip;
  loadClip(path: string, loop?: boolean): void;
  fire(): void;
  stop(): void;
  deleteClip(): void;
  setLooping(enabled: boolean): void;
}

/** An arrangement clip. Positions and lengths are expressed in beats. */
interface ArrangementClip extends Clip {
  delete(): void;
}

interface ArrangementClips {
  createClip(
      path: string,
      options: { readonly positionBeats: number; readonly lengthBeats: number }): void;
  at(index: number): ArrangementClip;
}

/** A continuous control represented by a normalized value in the 0..1 range. */
interface NormalizedParameter {
  setNormalized(value: number): void;
}

/** A boolean control. */
interface BooleanParameter {
  set(value: boolean): void;
}

interface Device {
  readonly parameters: { at(index: number): NormalizedParameter };
  delete(): void;
  showGui(): void;
}

interface Devices {
  load(path: string, index?: number): void;
  at(index: number): Device;
}

interface MixerDevice {
  readonly volume: NormalizedParameter;
  readonly pan: NormalizedParameter;
  readonly muted: BooleanParameter;
}

interface Track {
  readonly clipSlots: { at(index: number): ClipSlot };
  readonly arrangementClips: ArrangementClips;
  readonly devices: Devices;
  readonly mixerDevice: MixerDevice;
}

interface Transport {
  start(): void;
  stop(): void;
  seekBeats(positionBeats: number): void;
}

/** Root of the Ableton-style scripting hierarchy. */
interface LiveSet {
  readonly transport: Transport;
  readonly tracks: { at(index: number): Track };
  readonly tempo: { setBpm(bpm: number): void };
  save(path: string): void;
  load(path: string): void;
  undo(): void;
  redo(): void;
  bounce(path: string): void;
}

interface HibikiSdk {
  readonly liveSet: LiveSet;
  readonly theme: { set(name: string): void };
}
}
