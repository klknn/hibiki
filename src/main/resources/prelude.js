// Hibiki's public scripting SDK. Java/Protobuf implementation details stay private here.
const hibiki = (function () {
  const BackendManager = Packages.hibiki.BackendManager;
  const Request = Packages.hibiki.pb.commands.Request;
  const TransportCmd = Packages.hibiki.pb.commands.TransportCmd;
  const TrackCmd = Packages.hibiki.pb.commands.TrackCmd;
  const PluginCmd = Packages.hibiki.pb.commands.PluginCmd;
  const AutomationCmd = Packages.hibiki.pb.commands.AutomationCmd;
  const MidiCmd = Packages.hibiki.pb.commands.MidiCmd;
  const ProjectCmd = Packages.hibiki.pb.commands.ProjectCmd;
  const EntityRef = Packages.hibiki.pb.core.EntityRef;
  const Clip = Packages.hibiki.pb.core.Clip;
  const MidiEvent = Packages.hibiki.pb.core.MidiEvent;
  const AutomationPoint = Packages.hibiki.pb.core.AutomationPoint;
  const Theme = Packages.hibiki.ui.Theme;
  const backend = BackendManager.getInstance();

  function send(request) { backend.sendRequest(request); }
  function ref(track, plugin, slot, clip, lane) {
    const builder = EntityRef.newBuilder().setTrackIndex(track);
    if (plugin >= 0) builder.setPluginIndex(plugin);
    if (slot >= 0) builder.setSessionSlot(slot);
    if (clip >= 0) builder.setTimelineClip(clip);
    if (lane >= 0) builder.setLaneIndex(lane);
    return builder.build();
  }
  function trackCommand(action, target, configure) {
    const builder = TrackCmd.newBuilder().setAction(action).setTarget(target);
    if (configure) configure(builder);
    send(Request.newBuilder().setTrack(builder).build());
  }
  function writeMidi(target, resolution, notes) {
    const builder = MidiCmd.newBuilder().setAction(MidiCmd.Action.ACTION_UPDATE)
      .setTarget(target).setResolution(resolution);
    for (var i = 0; i < notes.length; i++) {
      const note = notes[i];
      builder.addEvents(MidiEvent.newBuilder().setTick(note.tick).setPitch(note.pitch)
        .setDurationTicks(note.dur).setVelocity(note.vel));
    }
    send(Request.newBuilder().setMidi(builder).build());
  }
  function midiApi(target) {
    return {
      replaceNotes: function (resolution, notes) { writeMidi(target, resolution, notes); },
      get: function () { send(Request.newBuilder().setMidi(MidiCmd.newBuilder()
        .setAction(MidiCmd.Action.ACTION_GET).setTarget(target)).build()); },
    };
  }
  function sessionSlot(track, slot) {
    const target = ref(track, -1, slot, -1, -1);
    return {
      load: function (path, loop) { trackCommand(TrackCmd.Action.ACTION_LOAD_CLIP, target,
        function (b) { b.setClipData(Clip.newBuilder().setPath(path).setIsLoop(!!loop)); }); },
      play: function () { trackCommand(TrackCmd.Action.ACTION_PLAY_SLOT, target); },
      remove: function () { trackCommand(TrackCmd.Action.ACTION_DELETE_CLIP, target); },
      setLoop: function (enabled) { trackCommand(TrackCmd.Action.ACTION_SET_CLIP_LOOP, target,
        function (b) { b.setFlag(!!enabled); }); },
      midi: midiApi(target),
    };
  }
  function timelineClip(track, clip) {
    const target = ref(track, -1, -1, clip, -1);
    return {
      remove: function () { trackCommand(TrackCmd.Action.ACTION_REMOVE_TIMELINE_CLIP, target); },
      midi: midiApi(target),
    };
  }
  function device(track, plugin) {
    return {
      remove: function () { send(Request.newBuilder().setPlugin(PluginCmd.newBuilder()
        .setAction(PluginCmd.Action.ACTION_REMOVE).setTarget(ref(track, plugin, -1, -1, -1))).build()); },
      showGui: function () { send(Request.newBuilder().setPlugin(PluginCmd.newBuilder()
        .setAction(PluginCmd.Action.ACTION_SHOW_GUI).setTarget(ref(track, plugin, -1, -1, -1))).build()); },
      parameter: function (id) { return { set: function (value) { send(Request.newBuilder()
        .setPlugin(PluginCmd.newBuilder().setAction(PluginCmd.Action.ACTION_SET_PARAM)
        .setTarget(ref(track, plugin, -1, -1, -1)).setParamId(id).setParamValue(value)).build()); } }; },
    };
  }
  function track(track) {
    return {
      session: { slot: function (slot) { return sessionSlot(track, slot); } },
      arrangement: {
        addClip: function (path, start, duration) { trackCommand(TrackCmd.Action.ACTION_ADD_TIMELINE_CLIP,
          ref(track, -1, -1, -1, -1), function (b) { b.setClipData(Clip.newBuilder().setPath(path)
            .setDurationBeats(duration)); b.setValue(start); }); },
        clip: function (clip) { return timelineClip(track, clip); },
      },
      devices: {
        load: function (path, index) { send(Request.newBuilder().setPlugin(PluginCmd.newBuilder()
          .setAction(PluginCmd.Action.ACTION_LOAD).setTarget(ref(track, index || 0, -1, -1, -1))
          .setPath(path)).build()); },
        at: function (index) { return device(track, index); },
      },
      mixer: {
        setVolume: function (value) { trackCommand(TrackCmd.Action.ACTION_SET_VOLUME, ref(track, -1, -1, -1, -1), function (b) { b.setValue(value); }); },
        setPan: function (value) { trackCommand(TrackCmd.Action.ACTION_SET_PAN, ref(track, -1, -1, -1, -1), function (b) { b.setValue(value); }); },
        setMuted: function (value) { trackCommand(TrackCmd.Action.ACTION_SET_MUTE, ref(track, -1, -1, -1, -1), function (b) { b.setFlag(!!value); }); },
      },
    };
  }
  return {
    transport: {
      play: function () { send(Request.newBuilder().setTransport(TransportCmd.newBuilder().setAction(TransportCmd.Action.ACTION_PLAY)).build()); },
      stop: function () { send(Request.newBuilder().setTransport(TransportCmd.newBuilder().setAction(TransportCmd.Action.ACTION_STOP)).build()); },
      seek: function (position) { send(Request.newBuilder().setTransport(TransportCmd.newBuilder().setAction(TransportCmd.Action.ACTION_SEEK).setSeekPos(position)).build()); },
    },
    tracks: { at: track },
    project: {
      save: function (path) { send(Request.newBuilder().setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_SAVE).setPath(path)).build()); },
      load: function (path) { send(Request.newBuilder().setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_LOAD).setPath(path)).build()); },
      setBpm: function (bpm) { send(Request.newBuilder().setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_SET_BPM).setBpm(bpm)).build()); },
      undo: function () { send(Request.newBuilder().setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_UNDO)).build()); },
      redo: function () { send(Request.newBuilder().setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_REDO)).build()); },
      bounce: function (path) { send(Request.newBuilder().setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_BOUNCE).setPath(path)).build()); },
    },
    theme: { set: function (name) { const preset = Theme.Preset.valueOf(name.toUpperCase().replace(/-/g, "_")); Theme.getInstance().update(preset, Theme.getInstance().getScaling(), Theme.getInstance().getBaseFontSize()); } },
  };
})();

function print(message) { java.lang.System.out.println(message); }
const console = { log: print, error: function (message) { java.lang.System.err.println(message); } };
