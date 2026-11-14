module gui_d.main;

import core.thread : Thread;
import core.time : msecs;
import dlangui;
import dlangui.widgets.combobox : ComboBox;
import dlangui.widgets.controls : Button, TextWidget;
import dlangui.widgets.editors : EditLine;
import dlangui.widgets.layouts : HorizontalLayout, VerticalLayout;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.browser_panel : BrowserPanel;
import gui_d.channel_rack : ChannelRackWidget;
import gui_d.ipc_client;
import gui_d.piano_roll : PianoRollCanvasWidget;
import gui_d.theme;
import gui_d.timeline_view : beatDurationSec, TimelineCanvasWidget;
import gui_d.transport_model;
import gui_d.widgets : MixerCanvasWidget;
import pb.commands : TransportCmd;
import pb.core : MidiEvent;
import pb.notifications : Notification;
import std.conv : to;
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

/// Entry point for the FL Studio 2026-inspired Hibiki `dlangui` + SDL2 DAW GUI.
extern (C) int UIAppMain(string[] args) {
    Platform.instance.uiTheme = "theme_dark";

    Window window = Platform.instance.createWindow(
        "Hibiki DAW — FL Studio 2026 Workspace (dlangui + SDL2)",
        null, WindowFlag.Resizable, 1440, 900);

    TransportModel model;
    model.initDefaultTemplate();
    EngineProcessClient client;

    auto root = new VerticalLayout("root");
    root.backgroundColor = Y2kColor.BG_WORKSPACE;
    root.layoutWidth = FILL_PARENT;
    root.layoutHeight = FILL_PARENT;

    // 1. Top Main Menu + Hint Bar
    auto menuRow = new HorizontalLayout("menu_row");
    menuRow.backgroundColor = Y2kColor.BG_PANEL_DARK;
    menuRow.padding = Rect(6, 2, 6, 2);
    menuRow.layoutWidth = FILL_PARENT;

    foreach (m; ["FILE", "EDIT", "ADD", "PATTERNS", "VIEW", "OPTIONS", "TOOLS", "HELP"]) {
        auto mb = new Button("menu_" ~ m, toUTF32(m));
        mb.click = delegate(Widget s) {
            model.hintText = format("Menu '%s' — use toolbar/browser controls to edit project", m);
            return true;
        };
        menuRow.addChild(mb);
    }
    auto hintBar = new TextWidget("hint_bar", toUTF32(model.hintText));
    hintBar.backgroundColor = Y2kColor.BG_LCD;
    hintBar.textColor = Y2kColor.FG_LCD_AMBER;
    hintBar.padding = Rect(8, 3, 8, 3);
    hintBar.layoutWidth = FILL_PARENT;
    menuRow.addChild(hintBar);

    auto cpuMeter = new TextWidget("cpu_meter", "CPU 2% | 96MB"d);
    cpuMeter.backgroundColor = Y2kColor.BG_LCD;
    cpuMeter.textColor = Y2kColor.FG_LCD_CYAN;
    cpuMeter.padding = Rect(8, 3, 8, 3);
    menuRow.addChild(cpuMeter);

    // 2. FL Studio Main Toolbar (Transport, Time LCD, Pattern, Snap, Window Toggles)
    auto transportBar = new HorizontalLayout("transport_bar");
    transportBar.backgroundColor = Y2kColor.BG_PANEL;
    transportBar.padding = Rect(6, 4, 6, 4);
    transportBar.layoutWidth = FILL_PARENT;
    transportBar.layoutHeight = WRAP_CONTENT;

    auto btnPatSong = new Button("btn_pat_song", "PAT"d);
    auto btnPlay = new Button("btn_play", "PLAY"d);
    auto btnStop = new Button("btn_stop", "STOP"d);
    auto btnRec  = new Button("btn_rec",  "REC"d);
    auto btnLoop = new Button("btn_loop", "LOOP"d);

    auto lcdTime = new TextWidget("lcd_time", "00:00.000  |  001:1:01"d);
    lcdTime.backgroundColor = Y2kColor.BG_LCD;
    lcdTime.textColor = Y2kColor.FG_LCD_AMBER;
    lcdTime.padding = Rect(8, 4, 8, 4);
    lcdTime.fontSize = 15;

    auto btnBpmDown = new Button("btn_bpm_down", "-"d);
    auto lcdBpm = new TextWidget("lcd_bpm", "140.0 BPM"d);
    lcdBpm.backgroundColor = Y2kColor.BG_LCD;
    lcdBpm.textColor = Y2kColor.FG_LCD_CYAN;
    lcdBpm.padding = Rect(8, 4, 8, 4);
    lcdBpm.fontSize = 15;
    auto btnBpmUp = new Button("btn_bpm_up", "+"d);

    auto btnPatPrev = new Button("btn_pat_prev", "<"d);
    auto lblPat = new TextWidget("lbl_pat", "Pattern 1"d);
    lblPat.backgroundColor = Y2kColor.BG_LCD;
    lblPat.textColor = Y2kColor.FG_TEXT;
    lblPat.padding = Rect(8, 4, 8, 4);
    auto btnPatNext = new Button("btn_pat_next", ">"d);
    auto btnPatAdd  = new Button("btn_pat_add", "+PAT"d);

    auto cmbSnap = new ComboBox("cmb_snap", ["Line"d, "Cell"d, "1/2 Beat"d, "1/4 Beat"d, "None"d]);
    cmbSnap.selectedItemIndex = 3;

    auto btnUndo = new Button("btn_undo", "UNDO"d);
    auto btnRedo = new Button("btn_redo", "REDO"d);

    // FL Studio 5 Core Window Toggles (F5..F9)
    auto btnWinPL  = new Button("win_pl",  "PL[F5]"d);
    auto btnWinCR  = new Button("win_cr",  "CR[F6]"d);
    auto btnWinPR  = new Button("win_pr",  "PR[F7]"d);
    auto btnWinMIX = new Button("win_mix", "MIX[F9]"d);
    auto btnWinBR  = new Button("win_br",  "BR[F8]"d);

    transportBar.addChild(btnPatSong);
    transportBar.addChild(btnPlay);
    transportBar.addChild(btnStop);
    transportBar.addChild(btnRec);
    transportBar.addChild(btnLoop);
    transportBar.addChild(lcdTime);
    transportBar.addChild(btnBpmDown);
    transportBar.addChild(lcdBpm);
    transportBar.addChild(btnBpmUp);
    transportBar.addChild(btnPatPrev);
    transportBar.addChild(lblPat);
    transportBar.addChild(btnPatNext);
    transportBar.addChild(btnPatAdd);
    transportBar.addChild(cmbSnap);
    transportBar.addChild(btnUndo);
    transportBar.addChild(btnRedo);
    transportBar.addChild(btnWinPL);
    transportBar.addChild(btnWinCR);
    transportBar.addChild(btnWinPR);
    transportBar.addChild(btnWinMIX);
    transportBar.addChild(btnWinBR);

    // 3. Multi-Window FL Studio Workspace
    auto workspace = new HorizontalLayout("workspace");
    workspace.layoutWidth = FILL_PARENT;
    workspace.layoutHeight = FILL_PARENT;

    auto browserPanel = new BrowserPanel("browser_panel", &model);

    auto centerGrid = new VerticalLayout("center_grid");
    centerGrid.layoutWidth = FILL_PARENT;
    centerGrid.layoutHeight = FILL_PARENT;

    auto topSplit = new HorizontalLayout("top_split");
    topSplit.layoutWidth = FILL_PARENT;
    topSplit.layoutHeight = FILL_PARENT;

    auto playlistCanvas = new TimelineCanvasWidget("playlist_canvas", &model);
    auto channelRack = new ChannelRackWidget("channel_rack", &model);

    topSplit.addChild(playlistCanvas);
    topSplit.addChild(channelRack);

    auto bottomSplit = new HorizontalLayout("bottom_split");
    bottomSplit.layoutWidth = FILL_PARENT;
    bottomSplit.layoutHeight = FILL_PARENT;

    auto pianoRollCanvas = new PianoRollCanvasWidget("piano_roll_canvas", &model);
    auto mixerCanvas = new MixerCanvasWidget("mixer_canvas", &model);

    bottomSplit.addChild(pianoRollCanvas);
    bottomSplit.addChild(mixerCanvas);

    centerGrid.addChild(topSplit);
    centerGrid.addChild(bottomSplit);

    workspace.addChild(browserPanel);
    workspace.addChild(centerGrid);

    auto statusBar = new TextWidget("status_bar", toUTF32(model.statusMessage));
    statusBar.backgroundColor = Y2kColor.BG_PANEL_DARK;
    statusBar.textColor = Y2kColor.FG_MUTED;
    statusBar.padding = Rect(8, 3, 8, 3);
    statusBar.layoutWidth = FILL_PARENT;

    root.addChild(menuRow);
    root.addChild(transportBar);
    root.addChild(workspace);
    root.addChild(statusBar);

    void refreshUi(bool refreshBrowser = false) {
        lcdTime.text = toUTF32(
            formatTimecode(model.playheadPosSec) ~ "  |  " ~
            formatBarBeat(model.playheadPosSec, model.bpm));
        lcdBpm.text = toUTF32(format("%.1f BPM", model.bpm));
        btnPatSong.text = (model.playbackMode == PlaybackMode.PAT) ? "PAT"d : "SONG"d;
        if (model.selectedPatternIndex >= 0 && model.selectedPatternIndex < cast(int)model.patterns.length) {
            lblPat.text = toUTF32(model.patterns[model.selectedPatternIndex].name);
        }
        hintBar.text = toUTF32(model.hintText);
        statusBar.text = toUTF32(model.statusMessage);
        if (refreshBrowser) browserPanel.refresh();
        playlistCanvas.invalidate();
        channelRack.invalidate();
        pianoRollCanvas.invalidate();
        mixerCanvas.invalidate();
    }

    void toggleWidget(Widget w, string name) {
        w.visibility = (w.visibility == Visibility.Visible) ? Visibility.Gone : Visibility.Visible;
        model.hintText = format("%s window %s", name, w.visibility == Visibility.Visible ? "opened" : "hidden");
        root.requestLayout();
        refreshUi();
    }

    client = new EngineProcessClient((Notification notif) {
        const bool structural = notif.plugin_list.hasValue || notif.clear_project.hasValue;
        window.executeInUiThread(() {
            model.applyNotification(notif);
            refreshUi(structural);
        });
    });

    string enginePath = resolveEnginePath(args);
    if (enginePath.length > 0 && client.start(enginePath)) {
        model.statusMessage = "Connected to engine: " ~ enginePath;
    } else {
        model.statusMessage = "FL Studio 2026 Frontend Mode (pass --engine=bazel-bin/engine/hbk-play for audio)";
    }
    refreshUi(true);

    // Toolbar actions
    btnPatSong.click = delegate(Widget s) {
        model.playbackMode = (model.playbackMode == PlaybackMode.PAT) ? PlaybackMode.SONG : PlaybackMode.PAT;
        model.hintText = (model.playbackMode == PlaybackMode.PAT)
            ? "Playback Mode: PAT (loops Channel Rack Pattern)"
            : "Playback Mode: SONG (plays Playlist arrangement)";
        refreshUi();
        return true;
    };
    btnPlay.click = delegate(Widget s) {
        client.sendRequest(makeTransportRequest(TransportCmd.Action.ACTION_PLAY));
        model.isPlaying = true;
        refreshUi();
        return true;
    };
    btnStop.click = delegate(Widget s) {
        client.sendRequest(makeTransportRequest(TransportCmd.Action.ACTION_STOP));
        model.isPlaying = false;
        refreshUi();
        return true;
    };
    btnRec.click = delegate(Widget s) {
        client.sendRequest(makeTransportRequest(TransportCmd.Action.ACTION_RECORD));
        return true;
    };
    btnLoop.click = delegate(Widget s) {
        model.loopEnabled = !model.loopEnabled;
        client.sendRequest(makeSetLoopRequest(model.loopEnabled, model.loopStartSec, model.loopEndSec));
        refreshUi();
        return true;
    };
    btnBpmDown.click = delegate(Widget s) {
        model.bpm = (model.bpm > 40.0) ? model.bpm - 5.0 : 40.0;
        client.sendRequest(makeSetBpmRequest(cast(float)model.bpm));
        refreshUi();
        return true;
    };
    btnBpmUp.click = delegate(Widget s) {
        model.bpm = (model.bpm < 300.0) ? model.bpm + 5.0 : 300.0;
        client.sendRequest(makeSetBpmRequest(cast(float)model.bpm));
        refreshUi();
        return true;
    };
    btnPatPrev.click = delegate(Widget s) {
        if (model.selectedPatternIndex > 0) model.selectedPatternIndex--;
        refreshUi();
        return true;
    };
    btnPatNext.click = delegate(Widget s) {
        if (model.selectedPatternIndex + 1 < cast(int)model.patterns.length) model.selectedPatternIndex++;
        refreshUi();
        return true;
    };
    btnPatAdd.click = delegate(Widget s) {
        const int idx = cast(int)model.patterns.length;
        model.patterns ~= PatternState(idx, format("Pattern %d", idx + 1), Y2kColor.CLIP_PATTERN);
        model.selectedPatternIndex = idx;
        model.hintText = format("Created Pattern %d", idx + 1);
        refreshUi();
        return true;
    };
    btnUndo.click = delegate(Widget s) {
        client.sendRequest(makeUndoRedoRequest(false));
        return true;
    };
    btnRedo.click = delegate(Widget s) {
        client.sendRequest(makeUndoRedoRequest(true));
        return true;
    };

    btnWinPL.click  = delegate(Widget s) { toggleWidget(playlistCanvas, "Playlist"); return true; };
    btnWinCR.click  = delegate(Widget s) { toggleWidget(channelRack, "Channel Rack"); return true; };
    btnWinPR.click  = delegate(Widget s) { toggleWidget(pianoRollCanvas, "Piano Roll"); return true; };
    btnWinMIX.click = delegate(Widget s) { toggleWidget(mixerCanvas, "Mixer"); return true; };
    btnWinBR.click  = delegate(Widget s) { toggleWidget(browserPanel, "Browser"); return true; };

    // Browser actions
    void sendVirtualNote(int trackIdx, uint note) {
        client.sendRequest(makeSendVirtualMidiRequest(trackIdx, note, 100, true));
        auto t = new Thread(() {
            Thread.sleep(180.msecs);
            client.sendRequest(makeSendVirtualMidiRequest(trackIdx, note, 0, false));
        });
        t.isDaemon = true;
        t.start();
    }

    browserPanel.onAddGenerator = (string label, string uri, bool isInst) {
        const int chIdx = cast(int)model.channels.length;
        ChannelState ch;
        ch.index = chIdx;
        ch.name = label.startsWith("Gen: ") ? label[5 .. $] : label;
        ch.uri = uri;
        ch.color = Y2kColor.CLIP_PATTERN;
        ch.mixerInsert = (chIdx % 7) + 1;
        model.channels ~= ch;
        model.selectedChannelIndex = chIdx;
        model.hintText = format("Added '%s' to Channel Rack -> Insert %d", ch.name, ch.mixerInsert);
        client.sendRequest(makePluginLoadRequest(ch.mixerInsert, uri));
        refreshUi();
    };
    browserPanel.onAddEffect = (string label, string uri) {
        const int ins = model.selectedMixerInsert;
        client.sendRequest(makePluginLoadRequest(ins, uri));
        model.hintText = format("Loaded %s onto Mixer Insert %d", label, ins);
        refreshUi();
    };
    browserPanel.onAddSampleClip = (string path) {
        client.sendRequest(makeAddTimelineClipRequest(model.selectedTrack, path, cast(float)model.playheadPosSec, 8.0f));
        TimelineClipState c;
        c.clipIndex = cast(int)model.tracks[model.ensureTrackIndex(model.selectedTrack)].clips.length;
        c.kind = PlaylistClipKind.AUDIO;
        c.name = path;
        c.startTimeSec = model.playheadPosSec;
        c.durationSec = 4.0;
        model.tracks[model.ensureTrackIndex(model.selectedTrack)].clips ~= c;
        model.hintText = format("Placed Audio Clip '%s' on Track %d", path, model.selectedTrack + 1);
        refreshUi();
    };
    browserPanel.onLoadProject = (string path) {
        client.sendRequest(makeProjectLoadRequest(path));
        model.hintText = "Loading project: " ~ path;
        refreshUi();
    };
    browserPanel.onScanVst3 = (string searchPath) {
        client.sendRequest(makePluginListRequest(searchPath));
        model.hintText = "Scanning VST3 plugins in: " ~ searchPath;
        refreshUi();
    };
    browserPanel.onPreviewNote = (uint note) {
        const int targetTrk = (model.selectedChannelIndex < cast(int)model.channels.length)
            ? model.channels[model.selectedChannelIndex].mixerInsert
            : model.selectedTrack;
        sendVirtualNote(targetTrk, note);
    };

    // Channel Rack callbacks
    channelRack.onSelectChannel = (int chIdx) {
        model.activeMidi.channelIndex = chIdx;
        refreshUi();
    };
    channelRack.onStepToggle = (int chIdx, int stepIdx, bool active) {
        refreshUi();
    };

    // Playlist callbacks
    playlistCanvas.onSeek = (double seekSec) {
        client.sendRequest(makeSeekRequest(seekSec));
        refreshUi();
    };
    playlistCanvas.onPaintPatternClip = (int trackIdx, double startSec) {
        const idx = model.ensureTrackIndex(trackIdx);
        const pat = (model.selectedPatternIndex < cast(int)model.patterns.length)
            ? model.patterns[model.selectedPatternIndex]
            : PatternState(0, "Pattern 1", Y2kColor.CLIP_PATTERN);
        TimelineClipState clip;
        clip.clipIndex = cast(int)model.tracks[idx].clips.length;
        clip.patternIndex = pat.index;
        clip.kind = PlaylistClipKind.PATTERN;
        clip.name = pat.name;
        clip.startTimeSec = startSec;
        clip.durationSec = 4.0 * beatDurationSec(model.bpm);
        model.tracks[idx].clips ~= clip;
        model.hintText = format("Painted '%s' at %.2fs on Track %d (Right-click to delete)",
                                pat.name, startSec, trackIdx + 1);
        refreshUi();
    };

    // Piano Roll callbacks
    pianoRollCanvas.onNotesChanged = (const(MidiEvent)[] evs) {
        client.sendRequest(makeMidiUpdateRequest(
            model.activeMidi.trackIndex, model.activeMidi.clipIndex,
            cast(uint)model.activeMidi.resolution, evs));
        refreshUi();
    };
    pianoRollCanvas.onPreviewNote = (uint pitch) {
        sendVirtualNote(model.activeMidi.trackIndex, pitch);
    };

    // Mixer callbacks
    mixerCanvas.onVolumeChange = (int trackIdx, float vol) {
        client.sendRequest(makeTrackVolumeRequest(trackIdx, vol));
        model.selectedMixerInsert = trackIdx;
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
