// JavaScript Prelude for Hibiki DAW Scripting
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
const SessionView = Packages.hibiki.ui.SessionView;
const TimelineView = Packages.hibiki.ui.TimelineView;

// Global singletons
const bm = BackendManager.getInstance();
const theme = Theme.getInstance();
const session = SessionView.getInstance();
const timeline = TimelineView.getInstance();

function _send(req) {
    bm.sendRequest(req);
}
function _ref(track, plugin, slot, clip, lane) {
    var builder = EntityRef.newBuilder().setTrackIndex(track || 0);
    if (plugin !== undefined && plugin !== null && plugin >= 0) builder.setPluginIndex(plugin);
    if (slot !== undefined && slot !== null && slot >= 0) builder.setSessionSlot(slot);
    if (clip !== undefined && clip !== null && clip >= 0) builder.setTimelineClip(clip);
    if (lane !== undefined && lane !== null && lane >= 0) builder.setLaneIndex(lane);
    return builder.build();
}
// Transport
function play() {
    _send(Request.newBuilder()
        .setTransport(TransportCmd.newBuilder().setAction(TransportCmd.Action.ACTION_PLAY))
        .build());
}

// Alias for convenience
const play_ = play;

function stop() {
    _send(Request.newBuilder()
        .setTransport(TransportCmd.newBuilder().setAction(TransportCmd.Action.ACTION_STOP))
        .build());
}

// Alias for convenience
const stop_ = stop;

function seek(pos) {
    _send(Request.newBuilder()
        .setTransport(TransportCmd.newBuilder().setAction(TransportCmd.Action.ACTION_SEEK).setSeekPos(pos))
        .build());
}

// Track & Clips
function loadClip(track, slot, path, loop) {
    _send(Request.newBuilder()
        .setTrack(TrackCmd.newBuilder()
            .setAction(TrackCmd.Action.ACTION_LOAD_CLIP)
            .setTarget(_ref(track, null, slot))
            .setClipData(Clip.newBuilder().setPath(path).setIsLoop(!!loop)))
        .build());
}

function playClip(track, slot) {
    _send(Request.newBuilder()
        .setTrack(TrackCmd.newBuilder()
            .setAction(TrackCmd.Action.ACTION_PLAY_SLOT)
            .setTarget(_ref(track, null, slot)))
        .build());
}

// Alias for playClip
const triggerClip = playClip;

function stopTrack(track) {
    _send(Request.newBuilder()
        .setTrack(TrackCmd.newBuilder()
            .setAction(TrackCmd.Action.ACTION_STOP)
            .setTarget(_ref(track)))
        .build());
}

function deleteClip(track, slot) {
    _send(Request.newBuilder()
        .setTrack(TrackCmd.newBuilder()
            .setAction(TrackCmd.Action.ACTION_DELETE_CLIP)
            .setTarget(_ref(track, null, slot)))
        .build());
}

function setClipLoop(track, slot, loop) {
    _send(Request.newBuilder()
        .setTrack(TrackCmd.newBuilder()
            .setAction(TrackCmd.Action.ACTION_SET_CLIP_LOOP)
            .setTarget(_ref(track, null, slot))
            .setFlag(!!loop))
        .build());
}

function addTimelineClip(track, path, start, duration) {
    _send(Request.newBuilder()
        .setTrack(TrackCmd.newBuilder()
            .setAction(TrackCmd.Action.ACTION_ADD_TIMELINE_CLIP)
            .setTarget(_ref(track))
            .setClipData(Clip.newBuilder().setPath(path).setDurationBeats(duration))
            .setValue(start))
        .build());
}

function removeTimelineClip(track, clip) {
    _send(Request.newBuilder()
        .setTrack(TrackCmd.newBuilder()
            .setAction(TrackCmd.Action.ACTION_REMOVE_TIMELINE_CLIP)
            .setTarget(_ref(track, null, null, clip)))
        .build());
}

// Plugins
function loadPlugin(track, path, pluginIdx) {
    _send(Request.newBuilder()
        .setPlugin(PluginCmd.newBuilder()
            .setAction(PluginCmd.Action.ACTION_LOAD)
            .setTarget(_ref(track, pluginIdx || 0))
            .setPath(path))
        .build());
}

function removePlugin(track, plugin) {
    _send(Request.newBuilder()
        .setPlugin(PluginCmd.newBuilder()
            .setAction(PluginCmd.Action.ACTION_REMOVE)
            .setTarget(_ref(track, plugin)))
        .build());
}

function showPluginGui(track, plugin) {
    _send(Request.newBuilder()
        .setPlugin(PluginCmd.newBuilder()
            .setAction(PluginCmd.Action.ACTION_SHOW_GUI)
            .setTarget(_ref(track, plugin)))
        .build());
}

function setParam(track, plugin, paramId, value) {
    _send(Request.newBuilder()
        .setPlugin(PluginCmd.newBuilder()
            .setAction(PluginCmd.Action.ACTION_SET_PARAM)
            .setTarget(_ref(track, plugin))
            .setParamId(paramId)
            .setParamValue(value))
        .build());
}

function listPlugins(path) {
    _send(Request.newBuilder()
        .setPlugin(PluginCmd.newBuilder()
            .setAction(PluginCmd.Action.ACTION_LIST)
            .setPath(path))
        .build());
}

// MIDI
function writeMidi(track, slot, clip, resolution, notes) {
    var builder = MidiCmd.newBuilder()
        .setAction(MidiCmd.Action.ACTION_UPDATE)
        .setTarget(_ref(track, null, slot, clip))
        .setResolution(resolution);
    for (var i = 0; i < notes.length; i++) {
        var n = notes[i];
        builder.addEvents(MidiEvent.newBuilder()
            .setTick(n.tick)
            .setPitch(n.pitch)
            .setDurationTicks(n.dur)
            .setVelocity(n.vel));
    }
    _send(Request.newBuilder().setMidi(builder).build());
}

function getMidi(track, slot, clip) {
    _send(Request.newBuilder()
        .setMidi(MidiCmd.newBuilder()
            .setAction(MidiCmd.Action.ACTION_GET)
            .setTarget(_ref(track, null, slot, clip)))
        .build());
}

// Automation
function addAutomation(track, plugin, paramId) {
    _send(Request.newBuilder()
        .setAutomation(AutomationCmd.newBuilder()
            .setAction(AutomationCmd.Action.ACTION_ADD_LANE)
            .setTarget(_ref(track, plugin))
            .setParamId(paramId))
        .build());
}

// Alias for addAutomation
const addAutomationLane = addAutomation;

function removeAutomation(track, lane) {
    _send(Request.newBuilder()
        .setAutomation(AutomationCmd.newBuilder()
            .setAction(AutomationCmd.Action.ACTION_REMOVE_LANE)
            .setTarget(_ref(track, null, null, null, lane)))
        .build());
}

function setAutomation(track, lane, points) {
    var builder = AutomationCmd.newBuilder()
        .setAction(AutomationCmd.Action.ACTION_UPDATE_POINTS)
        .setTarget(_ref(track, null, null, null, lane));
    for (var i = 0; i < points.length; i++) {
        var p = points[i];
        builder.addPoints(AutomationPoint.newBuilder()
            .setTimeBeats(p[0])
            .setValue(p[1])
            .setTension(p[2] || 0));
    }
    _send(Request.newBuilder().setAutomation(builder).build());
}

function getAutomation(track) {
    _send(Request.newBuilder()
        .setAutomation(AutomationCmd.newBuilder()
            .setAction(AutomationCmd.Action.ACTION_GET_LANES)
            .setTarget(_ref(track)))
        .build());
}

// Project & Theme
function save(path) {
    _send(Request.newBuilder()
        .setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_SAVE).setPath(path))
        .build());
}

function load(path) {
    _send(Request.newBuilder()
        .setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_LOAD).setPath(path))
        .build());
}

function setBpm(bpm) {
    _send(Request.newBuilder()
        .setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_SET_BPM).setBpm(bpm))
        .build());
}

function undo() {
    _send(Request.newBuilder()
        .setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_UNDO))
        .build());
}

function redo() {
    _send(Request.newBuilder()
        .setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_REDO))
        .build());
}

function bounce(path) {
    _send(Request.newBuilder()
        .setProject(ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_BOUNCE).setPath(path))
        .build());
}

function setTheme(presetName) {
    var preset = Packages.hibiki.ui.Theme.Preset.ABLETON_DARK;
    switch (presetName) {
        case "ableton-dark": preset = Packages.hibiki.ui.Theme.Preset.ABLETON_DARK; break;
        case "ableton-light": preset = Packages.hibiki.ui.Theme.Preset.ABLETON_LIGHT; break;
        case "solarized-dark": preset = Packages.hibiki.ui.Theme.Preset.SOLARIZED_DARK; break;
        case "solarized-light": preset = Packages.hibiki.ui.Theme.Preset.SOLARIZED_LIGHT; break;
        case "win95": preset = Packages.hibiki.ui.Theme.Preset.WIN95; break;
        case "winxp": preset = Packages.hibiki.ui.Theme.Preset.WINXP; break;
        default: throw new Error("Unknown preset: " + presetName);
    }
    theme.update(preset, theme.getScaling(), theme.getBaseFontSize());
}

// Global print function
function print(msg) {
    java.lang.System.out.println(msg);
}

// Simple console object for compatibility
const console = {
    log: function(msg) {
        java.lang.System.out.println(msg);
    },
    error: function(msg) {
        java.lang.System.err.println(msg);
    }
};

