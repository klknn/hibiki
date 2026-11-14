module gui_d.main;

import dlangui;
import dlangui.widgets.widget : Widget;
import gui_d.ipc_client;
import gui_d.theme;
import gui_d.transport_model;
import gui_d.widgets : MixerCanvasWidget;
import pb.commands : TransportCmd;
import pb.notifications : Notification;
import std.file : exists;
import std.format : format;
import std.string : startsWith;

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

/// Entry point for the Hibiki `dlangui` + SDL2 native GUI.
extern (C) int UIAppMain(string[] args) {
    Platform.instance.uiTheme = "theme_dark";

    Window window = Platform.instance.createWindow("Hibiki DAW (dlangui + SDL2)", null, WindowFlag.Resizable, 1120, 680);

    TransportModel model;
    EngineProcessClient client;

    auto root = new VerticalLayout("root");
    root.backgroundColor = Y2kColor.BG_WORKSPACE;
    root.layoutWidth = FILL_PARENT;
    root.layoutHeight = FILL_PARENT;

    // Top Y2K Transport Bar
    auto transportBar = new HorizontalLayout("transport_bar");
    transportBar.backgroundColor = Y2kColor.BG_PANEL;
    transportBar.padding = Rect(8, 6, 8, 6);
    transportBar.layoutWidth = FILL_PARENT;
    transportBar.layoutHeight = WRAP_CONTENT;

    auto btnPlay = new Button("btn_play", "PLAY"d);
    auto btnStop = new Button("btn_stop", "STOP"d);
    auto btnRec  = new Button("btn_rec",  "REC"d);
    auto btnLoop = new Button("btn_loop", "LOOP"d);

    auto lcdTime = new TextWidget("lcd_time", "00:00.000  |  001:1:00"d);
    lcdTime.backgroundColor = Y2kColor.BG_LCD;
    lcdTime.textColor = Y2kColor.FG_LCD_AMBER;
    lcdTime.padding = Rect(10, 4, 10, 4);
    lcdTime.fontSize = 18;

    auto lcdBpm = new TextWidget("lcd_bpm", "140.0 BPM"d);
    lcdBpm.backgroundColor = Y2kColor.BG_LCD;
    lcdBpm.textColor = Y2kColor.FG_LCD_CYAN;
    lcdBpm.padding = Rect(10, 4, 10, 4);
    lcdBpm.fontSize = 18;

    auto btnBpmDown = new Button("btn_bpm_down", "-"d);
    auto btnBpmUp   = new Button("btn_bpm_up",   "+"d);

    transportBar.addChild(btnPlay);
    transportBar.addChild(btnStop);
    transportBar.addChild(btnRec);
    transportBar.addChild(btnLoop);
    transportBar.addChild(lcdTime);
    transportBar.addChild(btnBpmDown);
    transportBar.addChild(lcdBpm);
    transportBar.addChild(btnBpmUp);

    auto mixerCanvas = new MixerCanvasWidget("mixer_canvas", &model);

    auto statusBar = new TextWidget("status_bar", "Initializing Hibiki dlangui..."d);
    statusBar.backgroundColor = Y2kColor.BG_PANEL_DARK;
    statusBar.textColor = Y2kColor.FG_MUTED;
    statusBar.padding = Rect(8, 4, 8, 4);
    statusBar.layoutWidth = FILL_PARENT;

    root.addChild(transportBar);
    root.addChild(mixerCanvas);
    root.addChild(statusBar);

    void refreshUi() {
        import std.utf : toUTF32;
        lcdTime.text = toUTF32(
            formatTimecode(model.playheadPosSec) ~ "  |  " ~
            formatBarBeat(model.playheadPosSec, model.bpm));
        lcdBpm.text = toUTF32(format("%.1f BPM", model.bpm));
        statusBar.text = toUTF32(model.statusMessage);
        mixerCanvas.invalidate();
    }

    client = new EngineProcessClient((Notification notif) {
        window.executeInUiThread(() {
            model.applyNotification(notif);
            refreshUi();
        });
    });

    string enginePath = resolveEnginePath(args);
    if (enginePath.length > 0 && client.start(enginePath)) {
        model.statusMessage = "Connected to engine: " ~ enginePath;
    } else {
        model.statusMessage = "Standalone UI preview (pass --engine=bazel-bin/engine/hbk-play)";
    }
    refreshUi();

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
