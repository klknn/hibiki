/** Canonical public TypeScript contract for the Hibiki scripting SDK. */

export {};

declare global {
interface MidiNote {
  readonly tick: number;
  readonly pitch: number;
  readonly dur: number;
  readonly vel: number;
}

interface MidiClip {
  replaceNotes(resolution: number, notes: readonly MidiNote[]): void;
  get(): void;
}

interface SessionSlot {
  readonly midi: MidiClip;
  load(path: string, loop?: boolean): void;
  play(): void;
  remove(): void;
  setLoop(enabled: boolean): void;
}

interface ArrangementClip {
  readonly midi: MidiClip;
  remove(): void;
}

interface DeviceParameter {
  set(value: number): void;
}

interface Device {
  remove(): void;
  showGui(): void;
  parameter(id: number): DeviceParameter;
}

interface DeviceChain {
  load(path: string, index?: number): void;
  at(index: number): Device;
}

interface Mixer {
  setVolume(value: number): void;
  setPan(value: number): void;
  setMuted(value: boolean): void;
}

interface Track {
  readonly session: { slot(index: number): SessionSlot };
  readonly arrangement: {
    addClip(path: string, start: number, duration: number): void;
    clip(index: number): ArrangementClip;
  };
  readonly devices: DeviceChain;
  readonly mixer: Mixer;
}

interface HibikiSdk {
  readonly transport: {
    play(): void;
    stop(): void;
    seek(position: number): void;
  };
  readonly tracks: { at(index: number): Track };
  readonly project: {
    save(path: string): void;
    load(path: string): void;
    setBpm(bpm: number): void;
    undo(): void;
    redo(): void;
    bounce(path: string): void;
  };
  readonly theme: { set(name: string): void };
}
}
