module gui_d.main;

import core.thread : Thread;
import core.time : msecs;
import dlangui;
import dlangui.widgets.controls : Button, TextWidget;
import dlangui.widgets.editors : EditLine;
import dlangui.widgets.layouts : HorizontalLayout, VerticalLayout;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.device_panel : DeviceRackPanel;
import gui_d.ipc_client;
import gui_d.piano_roll : PianoRollCanvasWidget;
import gui_d.theme;
import gui_d.timeline_view : TimelineCanvasWidget;
import gui_d.transport_model;
import gui_d.widgets : MixerCanvasWidget;
import pb.commands : TransportCmd;
import pb.core : MidiEvent;
import pb.notifications : Notification;
import std.file : exists;
import std.format : format;
import std.string : startsWith;
import std.utf : toUTF32, toUTF8;

mixin APP_ENTRY_POINT;

/// Resolves `hbk-play` binary path from CLI `--engine=` or default workspace locations.
private string resolveEnginePath(string[] args) {
    foreach (arg; args) {
        if (arg.startsWith("--engine=")) {
            return arg[9 .. $];
        }
    }
    foreach (candidate; ["bazel-bin/engine/hbk-play", "engine/hbk-play", "./hbk-play"]) {
        if (exists(candidate)) return candidate;
    }
    return "";
}

/// Entry point for the Hibiki `dlangui` + SDL2 native DAW GUI.
extern (C) int UIAppMain(string[] args) {
    Platform.instance.uiTheme = "theme_dark";

    Window window = Platform.instance.createWindow("Hibiki DAW (dlangui + SDL2)", null, WindowFlag.Resizable, 1360, 840);

    TransportModel model;
    model.ensureTrackIndex(0);
    EngineProcessClient client;

    auto root = new VerticalLayout("root");
    root.backgroundColor = Y2kColor.BG_WORKSPACE;
    root.layoutWidth = FILL_PARENT;
    root.layoutHeight = FILL_PARENT;

    // 1. Top Y2K Transport & Project Bar
    auto transportBar = new HorizontalLayout("transport_bar");
    transportBar.backgroundColor = Y2kColor.BG_PANEL;
    transportBar.padding = Rect(6, 5, 6, 5);
    transportBar.layoutWidth = FILL_PARENT;
    transportBar.layoutHeight = WRAP_CONTENT;

    auto btnPlay = new Button("btn_play", "PLAY"d);
    auto btnStop = new Button("btn_stop", "STOP"d);
    auto btnRec  = new Button("btn_rec",  "REC"d);
    auto btnLoop = new Button("btn_loop", "LOOP"d);

    auto lcdTime = new TextWidget("lcd_time", "00:00.000  |  001:1:00"d);
    lcdTime.backgroundColor = Y2kColor.BG_LCD;
    lcdTime.textColor = Y2kColor.FG_LCD_AMBER;
    lcdTime.padding = Rect(8, 4, 8, 4);
    lcdTime.fontSize = 16;

    auto lcdBpm = new TextWidget("lcd_bpm", "140.0 BPM"d);
    lcdBpm.backgroundColor = Y2kColor.BG_LCD;
    lcdBpm.textColor = Y2kColor.FG_LCD_CYAN;
    lcdBpm.padding = Rect(8, 4, 8, 4);
    lcdBpm.fontSize = 16;

    auto btnBpmDown = new Button("btn_bpm_down", "-"d);
    auto btnBpmUp   = new Button("btn_bpm_up",   "+"d);
    auto btnUndo    = new Button("btn_undo", "UNDO"d);
    auto btnRedo    = new Button("btn_redo", "REDO"d);

    auto edProjectPath = new EditLine("ed_project_path", "testdata/acid.hbk"d);
    edProjectPath.minWidth = 170;
    auto btnLoadProj = new Button("btn_load_proj", "LOAD"d);
    auto btnSaveProj = new Button("btn_save_proj", "SAVE"d);

    auto btnViewSplit    = new Button("btn_view_split", "SPLIT"d);
    auto btnViewTimeline = new Button("btn_view_tl",    "TIMELINE"d);
    auto btnViewPiano    = new Button("btn_view_pr",    "PIANO ROLL"d);
    auto btnViewMixer    = new Button("btn_view_mix",   "MIXER"d);

    transportBar.addChild(btnPlay);
    transportBar.addChild(btnStop);
    transportBar.addChild(btnRec);
    transportBar.addChild(btnLoop);
    transportBar.addChild(lcdTime);
    transportBar.addChild(btnBpmDown);
    transportBar.addChild(lcdBpm);
    transportBar.addChild(btnBpmUp);
    transportBar.addChild(btnUndo);
    transportBar.addChild(btnRedo);
    transportBar.addChild(edProjectPath);
    transportBar.addChild(btnLoadProj);
    transportBar.addChild(btnSaveProj);
    transportBar.addChild(btnViewSplit);
    transportBar.addChild(btnViewTimeline);
    transportBar.addChild(btnViewPiano);
    transportBar.addChild(btnViewMixer);

    // 2. Main Split Workspace: Left Device Rack + Right Timeline/PianoRoll/Mixer
    auto workspace = new HorizontalLayout("workspace");
    workspace.layoutWidth = FILL_PARENT;
    workspace.layoutHeight = FILL_PARENT;

    auto deviceRack = new DeviceRackPanel("device_rack", &model);
    auto rightPane = new VerticalLayout("right_pane");
    rightPane.layoutWidth = FILL_PARENT;
    rightPane.layoutHeight = FILL_PARENT;

    auto timelineCanvas = new TimelineCanvasWidget("timeline_canvas", &model);
    auto pianoRollCanvas = new PianoRollCanvasWidget("piano_roll_canvas", &model);
    auto mixerCanvas = new MixerCanvasWidget("mixer_canvas", &model);
    mixerCanvas.maxHeight = 230;

    rightPane.addChild(timelineCanvas);
    rightPane.addChild(pianoRollCanvas);
    rightPane.addChild(mixerCanvas);

    workspace.addChild(deviceRack);
    workspace.addChild(rightPane);

    auto statusBar = new TextWidget("status_bar", "Initializing Hibiki dlangui..."d);
    statusBar.backgroundColor = Y2kColor.BG_PANEL_DARK;
    statusBar.textColor = Y2kColor.FG_MUTED;
    statusBar.padding = Rect(8, 4, 8, 4);
    statusBar.layoutWidth = FILL_PARENT;

    root.addChild(transportBar);
    root.addChild(workspace);
    root.addChild(statusBar);

    void refreshUi(bool rebuildRack = false) {
        lcdTime.text = toUTF32(
            formatTimecode(model.playheadPosSec) ~ "  |  " ~
            formatBarBeat(model.playheadPosSec, model.bpm));
        lcdBpm.text = toUTF32(format("%.1f BPM", model.bpm));
        statusBar.text = toUTF32(model.statusMessage);
        if (rebuildRack) deviceRack.refresh();
        timelineCanvas.invalidate();
        pianoRollCanvas.invalidate();
        mixerCanvas.invalidate();
    }

    client = new EngineProcessClient((Notification notif) {
        const bool structural = notif.param_list.hasValue ||
                                notif.plugin_list.hasValue ||
                                notif.clear_project.hasValue ||
                                notif.track_info.hasValue;
        window.executeInUiThread(() {
            model.applyNotification(notif);
            refreshUi(structural);
        });
    });

    string enginePath = resolveEnginePath(args);
    if (enginePath.length > 0 && client.start(enginePath)) {
        model.statusMessage = "Connected to engine: " ~ enginePath;
    } else {
        model.statusMessage = "Standalone UI preview (pass --engine=bazel-bin/engine/hbk-play)";
    }
    refreshUi(true);

    // Transport & Project handlers
    btnPlay.click = delegate(Widget src) {
        client.sendRequest(makeTransportRequest(TransportCmd.Action.ACTION_PLAY));
        return true;
    };
    btnStop.click = delegate(Widget src) {
        client.sendRequest(makeTransportRequest(TransportCmd.Action.ACTION_STOP));
        return true;
    };
    btnRec.click = delegate(Widget src) {
        client.sendRequest(makeTransportRequest(TransportCmd.Action.ACTION_RECORD));
        return true;
    };
    btnLoop.click = delegate(Widget src) {
        model.loopEnabled = !model.loopEnabled;
        if (model.loopEndSec <= model.loopStartSec) {
            model.loopStartSec = 0.0;
            model.loopEndSec = 8.0;
        }
        client.sendRequest(makeSetLoopRequest(model.loopEnabled, model.loopStartSec, model.loopEndSec));
        refreshUi();
        return true;
    };
    btnBpmDown.click = delegate(Widget src) {
        model.bpm = (model.bpm > 40.0) ? model.bpm - 5.0 : 40.0;
        client.sendRequest(makeSetBpmRequest(cast(float)model.bpm));
        refreshUi();
        return true;
    };
    btnBpmUp.click = delegate(Widget src) {
        model.bpm = (model.bpm < 300.0) ? model.bpm + 5.0 : 300.0;
        client.sendRequest(makeSetBpmRequest(cast(float)model.bpm));
        refreshUi();
        return true;
    };
    btnUndo.click = delegate(Widget src) {
        client.sendRequest(makeUndoRedoRequest(false));
        return true;
    };
    btnRedo.click = delegate(Widget src) {
        client.sendRequest(makeUndoRedoRequest(true));
        return true;
    };
    btnLoadProj.click = delegate(Widget src) {
        client.sendRequest(makeProjectLoadRequest(toUTF8(edProjectPath.text)));
        return true;
    };
    btnSaveProj.click = delegate(Widget src) {
        client.sendRequest(makeProjectSaveRequest(toUTF8(edProjectPath.text)));
        return true;
    };

    // View mode switching
    btnViewSplit.click = delegate(Widget src) {
        timelineCanvas.visibility = Visibility.Visible;
        pianoRollCanvas.visibility = Visibility.Visible;
        mixerCanvas.visibility = Visibility.Visible;
        root.requestLayout();
        return true;
    };
    btnViewTimeline.click = delegate(Widget src) {
        timelineCanvas.visibility = Visibility.Visible;
        pianoRollCanvas.visibility = Visibility.Gone;
        mixerCanvas.visibility = Visibility.Gone;
        root.requestLayout();
        return true;
    };
    btnViewPiano.click = delegate(Widget src) {
        timelineCanvas.visibility = Visibility.Gone;
        pianoRollCanvas.visibility = Visibility.Visible;
        mixerCanvas.visibility = Visibility.Gone;
        root.requestLayout();
        return true;
    };
    btnViewMixer.click = delegate(Widget src) {
        timelineCanvas.visibility = Visibility.Gone;
        pianoRollCanvas.visibility = Visibility.Gone;
        mixerCanvas.visibility = Visibility.Visible;
        root.requestLayout();
        return true;
    };

    // Device Rack callbacks
    deviceRack.onAddTrack = (int trackIdx) {
        client.sendRequest(makeAddTrackRequest(trackIdx));
        refreshUi();
    };
    deviceRack.onAddClip = (int trackIdx, string path, float durBeats) {
        client.sendRequest(makeAddTimelineClipRequest(trackIdx, path, cast(float)model.playheadPosSec, durBeats));
        if (path.length == 0) {
            model.activeMidi.trackIndex = trackIdx;
            model.activeMidi.clipIndex = cast(int)model.tracks[model.ensureTrackIndex(trackIdx)].clips.length;
            model.activeMidi.events.length = 0;
        }
        refreshUi();
    };
    deviceRack.onLoadPlugin = (int trackIdx, string uri) {
        client.sendRequest(makePluginLoadRequest(trackIdx, uri));
    };
    deviceRack.onRemovePlugin = (int trackIdx, int pluginIdx) {
        client.sendRequest(makePluginRemoveRequest(trackIdx, pluginIdx));
    };
    deviceRack.onShowPluginGui = (int trackIdx, int pluginIdx) {
        client.sendRequest(makePluginShowGuiRequest(trackIdx, pluginIdx));
    };
    deviceRack.onSetParam = (int trackIdx, int pluginIdx, uint paramId, float value) {
        client.sendRequest(makePluginSetParamRequest(trackIdx, pluginIdx, paramId, value));
    };
    deviceRack.onScanVst3 = (string searchPath) {
        client.sendRequest(makePluginListRequest(searchPath));
    };
    deviceRack.onVirtualMidi = (int trackIdx, uint note, bool noteOn) {
        client.sendRequest(makeSendVirtualMidiRequest(trackIdx, note, 100, noteOn));
        if (noteOn) {
            auto t = new Thread(() {
                Thread.sleep(180.msecs);
                client.sendRequest(makeSendVirtualMidiRequest(trackIdx, note, 0, false));
            });
            t.isDaemon = true;
            t.start();
        }
    };
    deviceRack.onSelectionChanged = () {
        refreshUi();
    };

    // Timeline, Piano Roll, and Mixer callbacks
    timelineCanvas.onSeek = (double seekSec) {
        client.sendRequest(makeSeekRequest(seekSec));
        refreshUi();
    };
    timelineCanvas.onSelectTrackClip = (int trackIdx, int clipIdx) {
        if (clipIdx >= 0) {
            model.activeMidi.trackIndex = trackIdx;
            model.activeMidi.clipIndex = clipIdx;
            client.sendRequest(makeMidiGetRequest(trackIdx, clipIdx));
        }
        refreshUi(true);
    };
    pianoRollCanvas.onNotesChanged = (const(MidiEvent)[] evs) {
        client.sendRequest(makeMidiUpdateRequest(
            model.activeMidi.trackIndex, model.activeMidi.clipIndex,
            cast(uint)model.activeMidi.resolution, evs));
    };
    pianoRollCanvas.onPreviewNote = (uint pitch) {
        deviceRack.onVirtualMidi(model.activeMidi.trackIndex, pitch, true);
    };
    mixerCanvas.onVolumeChange = (int trackIdx, float vol) {
        client.sendRequest(makeTrackVolumeRequest(trackIdx, vol));
    };
    mixerCanvas.onMuteToggle = (int trackIdx, bool muted) {
        client.sendRequest(makeTrackMuteRequest(trackIdx, muted));
    };
    mixerCanvas.onSoloToggle = (int trackIdx, bool soloed) {
        client.sendRequest(makeTrackSoloRequest(trackIdx, soloed));
    };

    window.mainWidget = root;
    window.show();
    int res = Platform.instance.enterMessageLoop();
    client.stop();
    return res;
}
