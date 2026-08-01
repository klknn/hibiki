/** Rhino-compatible runtime implementation of Hibiki's public scripting SDK. */

declare const Packages: any;
declare const java: any;

interface MidiApi {
  replaceNotes(resolution: number, notes: readonly MidiNote[]): void;
  get(): void;
}

const hibiki: HibikiSdk = (function (): HibikiSdk {
  const BackendManager = Packages.hibiki.BackendManager;
  const Request = Packages.hibiki.pb.commands.Request;
  const TransportCmd = Packages.hibiki.pb.commands.TransportCmd;
  const TrackCmd = Packages.hibiki.pb.commands.TrackCmd;
  const PluginCmd = Packages.hibiki.pb.commands.PluginCmd;
  const MidiCmd = Packages.hibiki.pb.commands.MidiCmd;
  const ProjectCmd = Packages.hibiki.pb.commands.ProjectCmd;
  const EntityRef = Packages.hibiki.pb.core.EntityRef;
  const Clip = Packages.hibiki.pb.core.Clip;
  const MidiEvent = Packages.hibiki.pb.core.MidiEvent;
  const Theme = Packages.hibiki.ui.Theme;
  const backend = BackendManager.getInstance();

  function send(request: any): void {
    backend.sendRequest(request);
  }
  function ref(track: number, plugin: number, slot: number, clip: number): any {
    const builder = EntityRef.newBuilder().setTrackIndex(track);
    if (plugin >= 0) builder.setPluginIndex(plugin);
    if (slot >= 0) builder.setSessionSlot(slot);
    if (clip >= 0) builder.setTimelineClip(clip);
    return builder.build();
  }
  function trackCommand(action: any, target: any, configure?: (builder: any) => void): void {
    const builder = TrackCmd.newBuilder().setAction(action).setTarget(target);
    if (configure) configure(builder);
    send(Request.newBuilder().setTrack(builder).build());
  }
  function writeMidi(target: any, resolution: number, notes: readonly MidiNote[]): void {
    const builder = MidiCmd.newBuilder().setAction(MidiCmd.Action.ACTION_UPDATE)
      .setTarget(target).setResolution(resolution);
    for (let i = 0; i < notes.length; i++) {
      const note = notes[i];
      builder.addEvents(MidiEvent.newBuilder().setTick(note.tick).setPitch(note.pitch)
        .setDurationTicks(note.dur).setVelocity(note.vel));
    }
    send(Request.newBuilder().setMidi(builder).build());
  }
  function midiApi(target: any): MidiApi {
    return {
      replaceNotes: function (resolution: number, notes: readonly MidiNote[]): void { writeMidi(target, resolution, notes); },
      get: function (): void { send(Request.newBuilder().setMidi(MidiCmd.newBuilder()
        .setAction(MidiCmd.Action.ACTION_GET).setTarget(target)).build()); },
    };
  }
  function sessionSlot(track: number, slot: number): any {
    const target = ref(track, -1, slot, -1);
    return {
      load: function (path: string, loop?: boolean): void { trackCommand(TrackCmd.Action.ACTION_LOAD_CLIP, target,
        function (builder: any): void { builder.setClipData(Clip.newBuilder().setPath(path).setIsLoop(!!loop)); }); },
      play: function (): void { trackCommand(TrackCmd.Action.ACTION_PLAY_SLOT, target); },
      remove: function (): void { trackCommand(TrackCmd.Action.ACTION_DELETE_CLIP, target); },
      setLoop: function (enabled: boolean): void { trackCommand(TrackCmd.Action.ACTION_SET_CLIP_LOOP, target,
        function (builder: any): void { builder.setFlag(enabled); }); },
      midi: midiApi(target),
    };
  }
  function timelineClip(track: number, clip: number): any {
    const target = ref(track, -1, -1, clip);
    return { remove: function (): void { trackCommand(TrackCmd.Action.ACTION_REMOVE_TIMELINE_CLIP, target); }, midi: midiApi(target) };
  }
  function device(track: number, plugin: number): any {
    const target = ref(track, plugin, -1, -1);
    return {
      remove: function (): void { send(Request.newBuilder().setPlugin(PluginCmd.newBuilder()
        .setAction(PluginCmd.Action.ACTION_REMOVE).setTarget(target)).build()); },
      showGui: function (): void { send(Request.newBuilder().setPlugin(PluginCmd.newBuilder()
        .setAction(PluginCmd.Action.ACTION_SHOW_GUI).setTarget(target)).build()); },
      parameter: function (id: number): any { return { set: function (value: number): void { send(Request.newBuilder()
        .setPlugin(PluginCmd.newBuilder().setAction(PluginCmd.Action.ACTION_SET_PARAM)
        .setTarget(target).setParamId(id).setParamValue(value)).build()); } }; },
    };
  }
  function track(index: number): any {
    return {
      session: { slot: function (slot: number): any { return sessionSlot(index, slot); } },
      arrangement: {
        addClip: function (path: string, start: number, duration: number): void { trackCommand(TrackCmd.Action.ACTION_ADD_TIMELINE_CLIP,
          ref(index, -1, -1, -1), function (builder: any): void { builder.setClipData(Clip.newBuilder().setPath(path)
            .setDurationBeats(duration)); builder.setValue(start); }); },
        clip: function (clip: number): any { return timelineClip(index, clip); },
      },
      devices: {
        load: function (path: string, plugin?: number): void { send(Request.newBuilder().setPlugin(PluginCmd.newBuilder()
          .setAction(PluginCmd.Action.ACTION_LOAD).setTarget(ref(index, plugin || 0, -1, -1)).setPath(path)).build()); },
        at: function (plugin: number): any { return device(index, plugin); },
      },
      mixer: {
        setVolume: function (value: number): void { trackCommand(TrackCmd.Action.ACTION_SET_VOLUME, ref(index, -1, -1, -1), function (builder: any): void { builder.setValue(value); }); },
        setPan: function (value: number): void { trackCommand(TrackCmd.Action.ACTION_SET_PAN, ref(index, -1, -1, -1), function (builder: any): void { builder.setValue(value); }); },
        setMuted: function (value: boolean): void { trackCommand(TrackCmd.Action.ACTION_SET_MUTE, ref(index, -1, -1, -1), function (builder: any): void { builder.setFlag(value); }); },
      },
    };
  }
  return {
    transport: {
      play: function (): void { send(Request.newBuilder().setTransport(TransportCmd.newBuilder().setAction(TransportCmd.Action.ACTION_PLAY)).build()); },
      stop: function (): void { send(Request.newBuilder().setTransport(TransportCmd.newBuilder().setAction(TransportCmd.Action.ACTION_STOP)).build()); },
      seek: function (position: number): void { send(Request.newBuilder().setTransport(TransportCmd.newBuilder().setAction(TransportCmd.Action.ACTION_SEEK).setSeekPos(position)).build()); },
    },
    tracks: { at: track },
    project: {
      save: function (path: string): void { send(Request.newBuilder().setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_SAVE).setPath(path)).build()); },
      load: function (path: string): void { send(Request.newBuilder().setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_LOAD).setPath(path)).build()); },
      setBpm: function (bpm: number): void { send(Request.newBuilder().setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_SET_BPM).setBpm(bpm)).build()); },
      undo: function (): void { send(Request.newBuilder().setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_UNDO)).build()); },
      redo: function (): void { send(Request.newBuilder().setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_REDO)).build()); },
      bounce: function (path: string): void { send(Request.newBuilder().setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_BOUNCE).setPath(path)).build()); },
    },
    theme: { set: function (name: string): void { const preset = Theme.Preset.valueOf(name.toUpperCase().replace(/-/g, "_")); Theme.getInstance().update(preset, Theme.getInstance().getScaling(), Theme.getInstance().getBaseFontSize()); } },
  };
})();

function print(message: unknown): void { java.lang.System.out.println(message); }
const root: any = this;
root.console = { log: print, error: function (message: unknown): void { java.lang.System.err.println(message); } };
