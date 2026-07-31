/** Typed public surface for Hibiki's scripting SDK. */

interface MidiNote {
  tick: number;
  pitch: number;
  dur: number;
  vel: number;
}

interface MidiClip {
  replaceNotes(resolution: number, notes: MidiNote[]): void;
  get(): void;
}

interface SessionSlot {
  load(path: string, loop: boolean): void;
  play(): void;
  remove(): void;
  setLoop(enabled: boolean): void;
  midi: MidiClip;
}

interface ArrangementClip { remove(): void; midi: MidiClip; }
interface DeviceParameter { set(value: number): void; }
interface Device {
  remove(): void;
  showGui(): void;
  parameter(id: number): DeviceParameter;
}

interface HibikiTrack {
  session: { slot(index: number): SessionSlot };
  arrangement: {
    addClip(path: string, start: number, duration: number): void;
    clip(index: number): ArrangementClip;
  };
  devices: { load(path: string, index?: number): void; at(index: number): Device };
  mixer: { setVolume(value: number): void; setPan(value: number): void; setMuted(value: boolean): void };
}

declare const hibiki: {
  transport: { play(): void; stop(): void; seek(position: number): void };
  tracks: { at(index: number): HibikiTrack };
  project: {
    save(path: string): void; load(path: string): void; setBpm(bpm: number): void;
    undo(): void; redo(): void; bounce(path: string): void;
  };
  theme: { set(name: string): void };
};

declare function print(message: unknown): void;
