module gui_d.main;

import core.math : fabs;
import core.stdc.math : sinf;
import core.thread : Thread;
import core.time : msecs;
import dlangui;
import dlangui.widgets.controls : Button, TextWidget;
import dlangui.widgets.editors : EditLine;
import dlangui.widgets.layouts : HorizontalLayout, VerticalLayout;
import dlangui.widgets.menu : MenuItem, PopupMenu;
import dlangui.widgets.popup : PopupAlign, PopupFlags;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.browser_panel : BrowserPanel;
import gui_d.channel_rack : ChannelRackWidget;
import gui_d.ipc_client;
import gui_d.mixer_view : MixerCanvasWidget;
import gui_d.piano_roll : PianoRollCanvasWidget;
import gui_d.plugin_window : PluginWrapperWidget;
import gui_d.theme : FlThemeId;
import gui_d.timeline_view : beatDurationSec, TimelineCanvasWidget;
import gui_d.toolbar : FlToolbarWidget;
import gui_d.transport_model;
import gui_d.workspace : DesktopLayoutPreset, FlDesktopCanvas, FlWindowFrame;
import pb.commands : TransportCmd;
import pb.core : MidiEvent;
import pb.notifications : Notification;
import std.file : exists;
import std.format : format;
import std.string : startsWith;
import std.utf : toUTF32;

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

/// Root layout hosting global keyboard shortcuts and the 33ms prototype playback animation timer.
private class FlMainRootLayout : VerticalLayout {
    void delegate(uint keyCode, uint flags) onGlobalKey;
    void delegate() onAnimationTick;

    this(string id) {
        super(id);
    }

    override bool onKeyEvent(KeyEvent event) {
        if (event.action == KeyAction.KeyDown && onGlobalKey !is null) {
            // Don't hijack typing while user is typing in Search EditLine
            if (window !is null && cast(EditLine)window.focusedWidget !is null) {
                return super.onKeyEvent(event);
            }
            onGlobalKey(event.keyCode, event.flags);
        }
        return super.onKeyEvent(event);
    }

    override bool onTimer(ulong id) {
        if (onAnimationTick !is null) onAnimationTick();
        return true;
    }
}

/// Entry point for the FL Studio 2026 DAW UI Prototype (`dlangui` + SDL2).
extern (C) int UIAppMain(string[] args) {
    Platform.instance.uiTheme = "theme_dark";

    Window window = Platform.instance.createWindow(
        "Hibiki DAW — FL Studio 2026 Workspace",
        null, WindowFlag.Resizable, 1480, 920);

    TransportModel model;
    model.initDefaultTemplate();
    EngineProcessClient client;

    auto root = new FlMainRootLayout("root");
    root.layoutWidth = FILL_PARENT;
    root.layoutHeight = FILL_PARENT;

    // 1. Top FL Studio Menu Strip + Hint Panel + Theme Quick Toggle
    auto menuRow = new HorizontalLayout("menu_row");
    menuRow.padding = Rect(6, 3, 6, 3);
    menuRow.layoutWidth = FILL_PARENT;
    menuRow.layoutHeight = WRAP_CONTENT;

    auto hintBar = new TextWidget("hint_bar", toUTF32(model.hintText));
    hintBar.padding = Rect(10, 4, 10, 4);
    hintBar.layoutWidth = FILL_PARENT;

    auto btnTheme = new Button("btn_theme", "Theme: FL Default Dark"d);

    // 2. Modular FL Studio 2026 Toolbar
    auto toolbar = new FlToolbarWidget("fl_toolbar", &model);

    // 3. Main Area: Left Docked Browser + Right Floating/Snapping FL Desktop
    auto workspace = new HorizontalLayout("workspace");
    workspace.layoutWidth = FILL_PARENT;
    workspace.layoutHeight = FILL_PARENT;

    auto browserPanel = new BrowserPanel("browser_panel", &model);
    auto desktop = new FlDesktopCanvas("fl_desktop", &model);

    auto playlistCanvas = new TimelineCanvasWidget("playlist_canvas", &model);
    auto channelRack = new ChannelRackWidget("channel_rack", &model);
    auto pianoRollCanvas = new PianoRollCanvasWidget("piano_roll_canvas", &model);
    auto mixerCanvas = new MixerCanvasWidget("mixer_canvas", &model);
    auto pluginWrapper = new PluginWrapperWidget("plugin_wrapper", &model);

    auto winPlaylist = new FlWindowFrame("win_playlist", "Playlist - Arrangement 1",
                                         &model, playlistCanvas, Rect(0, 0, 730, 430), 360, 200);
    auto winChannelRack = new FlWindowFrame("win_channel_rack", "Channel Rack - Pattern 1",
                                            &model, channelRack, Rect(736, 0, 1180, 340), 420, 170);
    auto winPianoRoll = new FlWindowFrame("win_piano_roll", "Piano roll - Acid Bass",
                                          &model, pianoRollCanvas, Rect(0, 436, 610, 760), 340, 200);
    auto winMixer = new FlWindowFrame("win_mixer", "Mixer - Master",
                                      &model, mixerCanvas, Rect(616, 346, 1180, 760), 440, 240);
    auto winPlugin = new FlWindowFrame("win_plugin", "Fruity Wrapper - Acid Bass",
                                       &model, pluginWrapper, Rect(320, 160, 800, 480), 360, 240);
    winPlugin.visibility = Visibility.Gone;

    // Add in initial Z-order (last added = topmost)
    desktop.addWindow(winPianoRoll);
    desktop.addWindow(winPlaylist);
    desktop.addWindow(winMixer);
    desktop.addWindow(winChannelRack);
    desktop.addWindow(winPlugin);

    workspace.addChild(browserPanel);
    workspace.addChild(desktop);

    auto statusBar = new TextWidget("status_bar", toUTF32(model.statusMessage));
    statusBar.padding = Rect(10, 3, 10, 3);
    statusBar.layoutWidth = FILL_PARENT;

    void applyThemeColors() {
        const pal = model.palette();
        root.backgroundColor = pal.bgDesktop;
        menuRow.backgroundColor = pal.bgWindowHeader;
        hintBar.backgroundColor = pal.bgLcd;
        hintBar.textColor = pal.accentOrange;
        statusBar.backgroundColor = pal.bgWindowHeader;
        statusBar.textColor = pal.fgMuted;
        btnTheme.text = toUTF32("Theme: " ~ pal.name);
        browserPanel.refresh();
    }

    void refreshUi(bool refreshBrowser = false) {
        if (model.selectedPatternIndex >= 0 && model.selectedPatternIndex < cast(int)model.patterns.length) {
            winChannelRack.windowTitle = "Channel Rack - " ~ model.patterns[model.selectedPatternIndex].name;
        }
        if (model.activeMidi.channelIndex >= 0 && model.activeMidi.channelIndex < cast(int)model.channels.length) {
            winPianoRoll.windowTitle = "Piano roll - " ~ model.channels[model.activeMidi.channelIndex].name;
        }
        hintBar.text = toUTF32(model.hintText);
        statusBar.text = toUTF32(model.statusMessage);
        if (refreshBrowser) browserPanel.refresh();
        toolbar.invalidate();
        desktop.invalidate();
    }

    // Popup menu helper for top bar and window ▼ menus
    void showMenuPopup(Widget anchor, MenuItem rootItem, void delegate(int actionId) handler) {
        auto pop = new PopupMenu(rootItem);
        pop.menuItemClick = delegate(MenuItem item) {
            handler(item.id);
            return true;
        };
        auto pw = window.showPopup(pop, anchor, PopupAlign.Below);
        pw.flags = PopupFlags.CloseOnClickOutside;
    }

    void showWindowMenuPopup(FlWindowFrame win, int sx, int sy) {
        auto rootItem = new MenuItem();
        rootItem.add(
            new Action(1, "Maximize / Restore"d),
            new Action(2, "Reset Default FL 2026 Layout"d),
            new Action(3, "Tile All Windows"d),
            new Action(4, "Close Window"d)
        );
        auto pop = new PopupMenu(rootItem);
        pop.menuItemClick = delegate(MenuItem item) {
            if (item.id == 1) win.toggleMaximize();
            else if (item.id == 2) desktop.applyLayoutPreset(DesktopLayoutPreset.DEFAULT_FL);
            else if (item.id == 3) desktop.applyLayoutPreset(DesktopLayoutPreset.TILE_ALL);
            else if (item.id == 4) {
                win.visibility = Visibility.Gone;
                desktop.requestLayout();
            }
            refreshUi();
            return true;
        };
        auto pw = window.showPopup(pop, win, PopupAlign.Point | PopupAlign.Right, sx, sy);
        pw.flags = PopupFlags.CloseOnClickOutside;
    }

    foreach (w; [winPlaylist, winChannelRack, winPianoRoll, winMixer, winPlugin]) {
        w.onMenuClick = &showWindowMenuPopup;
        w.onClose = (FlWindowFrame closed) {
            model.hintText = closed.windowTitle ~ " hidden — press toolbar or F5..F9 to reopen";
            refreshUi();
        };
    }

    // Top Main Menu Bar Buttons
    auto btnFile = new Button("m_file", "FILE"d);
    auto btnEdit = new Button("m_edit", "EDIT"d);
    auto btnAdd  = new Button("m_add",  "ADD"d);
    auto btnPat  = new Button("m_pat",  "PATTERNS"d);
    auto btnView = new Button("m_view", "VIEW"d);
    auto btnOpt  = new Button("m_opt",  "OPTIONS"d);
    auto btnHelp = new Button("m_help", "HELP"d);

    btnFile.click = (Widget s) {
        auto m = new MenuItem();
        m.add(
            new Action(101, "New from FL Studio Template"d),
            new Action(102, "Open Project (testdata/acid.hbk)"d),
            new Action(103, "Save Project (build/hibiki_save.hbk)"d)
        );
        showMenuPopup(s, m, (int id) {
            if (id == 101) {
                model = TransportModel.init;
                model.initDefaultTemplate();
                applyThemeColors();
            } else if (id == 102) {
                client.sendRequest(makeProjectLoadRequest("testdata/acid.hbk"));
                model.hintText = "Loading project: testdata/acid.hbk";
            } else if (id == 103) {
                client.sendRequest(makeProjectSaveRequest("build/hibiki_save.hbk"));
                model.hintText = "Saved project to build/hibiki_save.hbk";
            }
            refreshUi(true);
        });
        return true;
    };

    btnEdit.click = (Widget s) {
        auto m = new MenuItem();
        m.add(new Action(201, "Undo"d), new Action(202, "Redo"d));
        showMenuPopup(s, m, (int id) {
            client.sendRequest(makeUndoRedoRequest(id == 202));
            refreshUi();
        });
        return true;
    };

    btnAdd.click = (Widget s) {
        auto m = new MenuItem();
        m.add(
            new Action(301, "3x Osc (Synth)"d),
            new Action(302, "Acid Bass (Synth)"d),
            new Action(303, "E-Piano (Keys)"d),
            new Action(304, "Drum Machine"d),
            new Action(305, "Sampler"d)
        );
        showMenuPopup(s, m, (int id) {
            static immutable string[5] NAMES = ["3x Osc", "Acid Bass", "E-Piano", "Drum Machine", "Sampler"];
            static immutable string[5] URIS = [
                "builtin://3xosc", "builtin://acid_bass", "builtin://epiano",
                "builtin://drum_machine", "builtin://sampler"
            ];
            const int idx = id - 301;
            if (idx >= 0 && idx < 5) {
                const int chIdx = cast(int)model.channels.length;
                ChannelState ch;
                ch.initDefaults();
                ch.index = chIdx;
                ch.name = NAMES[idx];
                ch.uri = URIS[idx];
                ch.category = (idx == 3) ? "Drums" : "Synths";
                ch.mixerInsert = (chIdx % 11) + 1;
                ch.generatorPlugin = TransportModel.makeDefaultPluginSlot(ch.name, ch.uri, true, 0);
                model.channels ~= ch;
                model.selectedChannelIndex = chIdx;
                client.sendRequest(makePluginLoadRequest(ch.mixerInsert, ch.uri));
                model.hintText = format("Added '%s' -> Mixer Insert %d", ch.name, ch.mixerInsert);
                refreshUi();
            }
        });
        return true;
    };

    btnPat.click = (Widget s) {
        auto m = new MenuItem();
        m.add(new Action(401, "Insert New Pattern"d), new Action(402, "Clone Current Pattern"d));
        showMenuPopup(s, m, (int id) {
            const int idx = cast(int)model.patterns.length;
            model.patterns ~= PatternState(idx, format("Pattern %d", idx + 1), model.palette().clipPattern, 16);
            model.selectedPatternIndex = idx;
            model.hintText = format("Selected %s", model.patterns[idx].name);
            refreshUi();
        });
        return true;
    };

    btnView.click = (Widget s) {
        auto m = new MenuItem();
        m.add(
            new Action(501, "Toggle Playlist [F5]"d),
            new Action(502, "Toggle Channel Rack [F6]"d),
            new Action(503, "Toggle Piano Roll [F7]"d),
            new Action(504, "Toggle Browser [F8]"d),
            new Action(505, "Toggle Mixer [F9]"d),
            new Action(510, "Arrange: FL Studio 2026 Default"d),
            new Action(511, "Arrange: Tile 2x2"d),
            new Action(512, "Arrange: Production (Beatmaking)"d),
            new Action(513, "Arrange: Mixing Focus"d),
            new Action(514, "Arrange: Fullscreen Playlist"d)
        );
        showMenuPopup(s, m, (int id) {
            if (id == 501) desktop.toggleWindow("win_playlist");
            else if (id == 502) desktop.toggleWindow("win_channel_rack");
            else if (id == 503) desktop.toggleWindow("win_piano_roll");
            else if (id == 504) {
                browserPanel.visibility = (browserPanel.visibility == Visibility.Visible)
                    ? Visibility.Gone : Visibility.Visible;
                root.requestLayout();
            } else if (id == 505) desktop.toggleWindow("win_mixer");
            else if (id == 510) desktop.applyLayoutPreset(DesktopLayoutPreset.DEFAULT_FL);
            else if (id == 511) desktop.applyLayoutPreset(DesktopLayoutPreset.TILE_ALL);
            else if (id == 512) desktop.applyLayoutPreset(DesktopLayoutPreset.PRODUCTION);
            else if (id == 513) desktop.applyLayoutPreset(DesktopLayoutPreset.MIXING);
            else if (id == 514) desktop.applyLayoutPreset(DesktopLayoutPreset.FOCUS_PLAYLIST);
            refreshUi();
        });
        return true;
    };

    btnOpt.click = (Widget s) {
        auto m = new MenuItem();
        m.add(
            new Action(601, "Theme: FL Default Dark"d),
            new Action(602, "Theme: Ultramarine"d),
            new Action(603, "Theme: Midnight Mint"d),
            new Action(604, "Theme: Warm Amber"d)
        );
        showMenuPopup(s, m, (int id) {
            if (id == 601) model.themeId = FlThemeId.DEFAULT_DARK;
            else if (id == 602) model.themeId = FlThemeId.ULTRAMARINE;
            else if (id == 603) model.themeId = FlThemeId.MIDNIGHT_MINT;
            else if (id == 604) model.themeId = FlThemeId.WARM_AMBER;
            applyThemeColors();
            refreshUi();
        });
        return true;
    };

    btnHelp.click = (Widget s) {
        model.hintText = "Shortcuts: Space=Play/Stop | F5=Playlist F6=Rack F7=PianoRoll F8=Browser F9=Mixer | Hold Alt=Free Window Drag";
        refreshUi();
        return true;
    };

    btnTheme.click = (Widget s) {
        model.themeId = cast(FlThemeId)((cast(int)model.themeId + 1) % 4);
        applyThemeColors();
        refreshUi();
        return true;
    };

    foreach (mb; [btnFile, btnEdit, btnAdd, btnPat, btnView, btnOpt, btnHelp]) {
        menuRow.addChild(mb);
    }
    menuRow.addChild(hintBar);
    menuRow.addChild(btnTheme);

    root.addChild(menuRow);
    root.addChild(toolbar);
    root.addChild(workspace);
    root.addChild(statusBar);

    // Connect Engine IPC client
    client = new EngineProcessClient((Notification notif) {
        const bool structural = notif.plugin_list.hasValue || notif.clear_project.hasValue;
        window.executeInUiThread(() {
            model.applyNotification(notif);
            refreshUi(structural);
        });
    });

    string enginePath = resolveEnginePath(args);
    if (enginePath.length > 0 && client.start(enginePath)) {
        model.statusMessage = "Connected to Hibiki engine: " ~ enginePath;
    } else {
        model.statusMessage = "FL Studio 2026 Prototype Mode (pass --engine=bazel-bin/engine/hbk-play for live audio)";
    }

    // Wire Toolbar Actions
    toolbar.queryPlaylistVisible    = () => winPlaylist.visibility == Visibility.Visible;
    toolbar.queryChannelRackVisible = () => winChannelRack.visibility == Visibility.Visible;
    toolbar.queryPianoRollVisible   = () => winPianoRoll.visibility == Visibility.Visible;
    toolbar.queryBrowserVisible     = () => browserPanel.visibility == Visibility.Visible;
    toolbar.queryMixerVisible       = () => winMixer.visibility == Visibility.Visible;
    toolbar.queryPluginVisible      = () => winPlugin.visibility == Visibility.Visible;

    toolbar.onPatSongToggle = () {
        model.hintText = (model.playbackMode == PlaybackMode.PAT)
            ? "Playback Mode: PAT (loops Channel Rack Pattern)"
            : "Playback Mode: SONG (plays Playlist arrangement)";
        refreshUi();
    };
    toolbar.onPlayToggle = () {
        if (model.isPlaying) {
            client.sendRequest(makeTransportRequest(TransportCmd.Action.ACTION_STOP));
            model.isPlaying = false;
        } else {
            client.sendRequest(makeTransportRequest(TransportCmd.Action.ACTION_PLAY));
            model.isPlaying = true;
        }
        refreshUi();
    };
    toolbar.onStop = () {
        client.sendRequest(makeTransportRequest(TransportCmd.Action.ACTION_STOP));
        model.isPlaying = false;
        model.playheadPosSec = 0.0;
        foreach (ref trk; model.tracks) {
            trk.peakL = 0.0f;
            trk.peakR = 0.0f;
        }
        refreshUi();
    };
    toolbar.onRecordToggle = () {
        model.isRecording = !model.isRecording;
        client.sendRequest(makeTransportRequest(TransportCmd.Action.ACTION_RECORD));
        refreshUi();
    };
    toolbar.onLoopToggle = () {
        model.loopEnabled = !model.loopEnabled;
        client.sendRequest(makeSetLoopRequest(model.loopEnabled, model.loopStartSec, model.loopEndSec));
        refreshUi();
    };
    toolbar.onBpmChanged = (double bpm) {
        client.sendRequest(makeSetBpmRequest(cast(float)bpm));
        refreshUi();
    };
    toolbar.onUndoRedo = (bool redo) {
        client.sendRequest(makeUndoRedoRequest(redo));
        refreshUi();
    };
    toolbar.onPatternStep = (int delta) {
        if (model.patterns.length == 0) return;
        const int n = cast(int)model.patterns.length;
        model.selectedPatternIndex = (model.selectedPatternIndex + delta + n) % n;
        model.hintText = "Selected " ~ model.patterns[model.selectedPatternIndex].name;
        refreshUi();
    };
    toolbar.onPatternAdd = () {
        const int idx = cast(int)model.patterns.length;
        model.patterns ~= PatternState(idx, format("Pattern %d", idx + 1), model.palette().clipPattern, 16);
        model.selectedPatternIndex = idx;
        model.hintText = format("Created Pattern %d", idx + 1);
        refreshUi();
    };
    toolbar.onToggleWindow = (string winKey) {
        if (winKey == "browser") {
            browserPanel.visibility = (browserPanel.visibility == Visibility.Visible)
                ? Visibility.Gone : Visibility.Visible;
            root.requestLayout();
        } else {
            desktop.toggleWindow(winKey);
        }
        refreshUi();
    };

    // Virtual MIDI Audition Helper
    void sendVirtualNote(int trackIdx, uint note) {
        client.sendRequest(makeSendVirtualMidiRequest(trackIdx, note, 108, true));
        auto t = new Thread(() {
            Thread.sleep(180.msecs);
            client.sendRequest(makeSendVirtualMidiRequest(trackIdx, note, 0, false));
        });
        t.isDaemon = true;
        t.start();
    }

    // Browser Callbacks
    browserPanel.onAddGenerator = (string label, string uri, bool isInst) {
        const int chIdx = cast(int)model.channels.length;
        ChannelState ch;
        ch.initDefaults();
        ch.index = chIdx;
        ch.name = label.startsWith("Gen: ") ? label[5 .. $] : label;
        ch.uri = uri;
        ch.category = "Synths";
        ch.color = model.palette().clipPattern;
        ch.mixerInsert = (chIdx % 11) + 1;
        ch.generatorPlugin = TransportModel.makeDefaultPluginSlot(ch.name, uri, true, 0);
        model.channels ~= ch;
        model.selectedChannelIndex = chIdx;
        model.hintText = format("Added '%s' to Channel Rack -> Insert %d", ch.name, ch.mixerInsert);
        client.sendRequest(makePluginLoadRequest(ch.mixerInsert, uri));
        desktop.showWindow("win_channel_rack");
        refreshUi();
    };
    browserPanel.onAddEffect = (string label, string uri) {
        const int ins = model.selectedMixerInsert;
        const size_t tArr = model.ensureTrackIndex(ins);
        const int slotIdx = cast(int)model.tracks[tArr].plugins.length;
        if (slotIdx < 10) {
            const string cleanName = label.startsWith("FX: ") ? label[4 .. $] : label;
            model.tracks[tArr].plugins ~= TransportModel.makeDefaultPluginSlot(cleanName, uri, false, slotIdx);
        }
        client.sendRequest(makePluginLoadRequest(ins, uri));
        model.hintText = format("Loaded %s onto Mixer Insert %d", label, ins);
        desktop.showWindow("win_mixer");
        refreshUi();
    };
    browserPanel.onAddSampleClip = (string path) {
        const int targetLane = (model.selectedTrack > 0) ? model.selectedTrack : 1;
        client.sendRequest(makeAddTimelineClipRequest(targetLane, path, cast(float)model.playheadPosSec, 8.0f));
        const size_t tArr = model.ensureTrackIndex(targetLane);
        TimelineClipState c;
        c.clipIndex = cast(int)model.tracks[tArr].clips.length;
        c.kind = PlaylistClipKind.AUDIO;
        c.name = path;
        c.color = model.palette().clipAudio;
        c.startTimeSec = model.playheadPosSec;
        c.durationSec = 4.0 * beatDurationSec(model.bpm) * 2.0;
        c.waveform = model.samplePreview.waveform.dup;
        model.tracks[tArr].clips ~= c;
        model.hintText = format("Placed Audio Clip '%s' on Track %d", path, targetLane);
        refreshUi();
    };
    browserPanel.onLoadProject = (string path) {
        client.sendRequest(makeProjectLoadRequest(path));
        model.hintText = "Loading project: " ~ path;
        refreshUi();
    };
    browserPanel.onSelectPattern = (int patIdx) {
        if (patIdx >= 0 && patIdx < cast(int)model.patterns.length) {
            model.selectedPatternIndex = patIdx;
            model.activePickerKind = PlaylistClipKind.PATTERN;
            model.activePickerIndex = patIdx;
            refreshUi();
        }
    };
    browserPanel.onScanVst3 = (string searchPath) {
        client.sendRequest(makePluginListRequest(searchPath));
        model.hintText = "Scanning VST3 plugins in: " ~ searchPath;
        refreshUi();
    };
    browserPanel.onPreviewNote = (uint note) {
        const int targetTrk = (model.selectedChannelIndex < cast(int)model.channels.length)
            ? model.channels[model.selectedChannelIndex].mixerInsert
            : 1;
        sendVirtualNote(targetTrk, note);
    };

    // Channel Rack Callbacks
    channelRack.onSelectChannel = (int chIdx) {
        model.activeMidi.channelIndex = chIdx;
        refreshUi();
    };
    channelRack.onOpenPluginWrapper = (int chIdx) {
        pluginWrapper.setTargetChannel(chIdx);
        if (chIdx >= 0 && chIdx < cast(int)model.channels.length) {
            winPlugin.windowTitle = "Fruity Wrapper - " ~ model.channels[chIdx].name;
        }
        desktop.showWindow("win_plugin");
        refreshUi();
    };
    channelRack.onOpenPianoRoll = (int chIdx) {
        model.activeMidi.channelIndex = chIdx;
        desktop.showWindow("win_piano_roll");
        refreshUi();
    };
    channelRack.onStepToggle = (int chIdx, int stepIdx, bool active) {
        refreshUi();
    };
    channelRack.onAddChannelPrompt = () {
        const int chIdx = cast(int)model.channels.length;
        ChannelState ch;
        ch.initDefaults();
        ch.index = chIdx;
        ch.name = format("3x Osc %d", chIdx + 1);
        ch.uri = "builtin://3xosc";
        ch.category = "Synths";
        ch.color = model.palette().clipPattern;
        ch.mixerInsert = (chIdx % 11) + 1;
        ch.generatorPlugin = TransportModel.makeDefaultPluginSlot(ch.name, ch.uri, true, 0);
        model.channels ~= ch;
        model.selectedChannelIndex = chIdx;
        model.hintText = format("Added new channel '%s' -> Insert %d", ch.name, ch.mixerInsert);
        refreshUi();
    };

    // Playlist Callbacks
    playlistCanvas.onSeek = (double seekSec) {
        client.sendRequest(makeSeekRequest(seekSec));
        refreshUi();
    };
    playlistCanvas.onPaintClip = (int trackIdx, double startSec) {
        const idx = model.ensureTrackIndex(trackIdx);
        TimelineClipState clip;
        clip.clipIndex = cast(int)model.tracks[idx].clips.length;
        clip.kind = model.activePickerKind;
        clip.startTimeSec = startSec;
        clip.durationSec = 4.0 * beatDurationSec(model.bpm) * 2.0;

        if (clip.kind == PlaylistClipKind.PATTERN) {
            const pat = (model.selectedPatternIndex < cast(int)model.patterns.length)
                ? model.patterns[model.selectedPatternIndex]
                : PatternState(0, "Pattern 1", model.palette().clipPattern, 16);
            clip.patternIndex = pat.index;
            clip.name = pat.name;
            clip.color = pat.color;
        } else if (clip.kind == PlaylistClipKind.AUDIO) {
            clip.name = "loop140.wav";
            clip.color = model.palette().clipAudio;
            clip.waveform = model.samplePreview.waveform.dup;
        } else {
            clip.name = "Acid Cutoff Env";
            clip.color = model.palette().clipAutomation;
            clip.autoPoints = [
                AutomationPoint(0.0, 0.2f),
                AutomationPoint(0.5, 0.85f),
                AutomationPoint(1.0, 0.3f),
            ];
        }
        model.tracks[idx].clips ~= clip;
        model.hintText = format("Painted '%s' at %.2fs on Track %d (Right-click to delete)",
                                clip.name, startSec, trackIdx);
        refreshUi();
    };

    // Piano Roll Callbacks
    pianoRollCanvas.onNotesChanged = (const(MidiEvent)[] evs) {
        client.sendRequest(makeMidiUpdateRequest(
            model.activeMidi.trackIndex, model.activeMidi.clipIndex,
            cast(uint)model.activeMidi.resolution, evs));
        refreshUi();
    };
    pianoRollCanvas.onPreviewNote = (uint pitch) {
        sendVirtualNote(model.activeMidi.trackIndex, pitch);
    };

    // Mixer Callbacks
    mixerCanvas.onVolumeChange = (int trackIdx, float vol) {
        client.sendRequest(makeTrackVolumeRequest(trackIdx, vol));
        refreshUi();
    };
    mixerCanvas.onMuteToggle = (int trackIdx, bool muted) {
        client.sendRequest(makeTrackMuteRequest(trackIdx, muted));
        refreshUi();
    };
    mixerCanvas.onSoloToggle = (int trackIdx, bool soloed) {
        client.sendRequest(makeTrackSoloRequest(trackIdx, soloed));
        refreshUi();
    };
    mixerCanvas.onLoadFxSlot = (int trackIdx, int slotIdx, string uri) {
        client.sendRequest(makePluginLoadRequest(trackIdx, uri, slotIdx));
        refreshUi();
    };
    mixerCanvas.onOpenFxPlugin = (int trackIdx, int slotIdx) {
        pluginWrapper.setTargetMixerFx(trackIdx, slotIdx);
        winPlugin.windowTitle = format("Fruity Wrapper - Insert %d Slot %d", trackIdx, slotIdx + 1);
        desktop.showWindow("win_plugin");
        refreshUi();
    };

    // Plugin Wrapper Callbacks
    pluginWrapper.onSetParam = (int trackIdx, int plugIdx, uint paramId, float value) {
        client.sendRequest(makePluginSetParamRequest(trackIdx, plugIdx, paramId, value));
        refreshUi();
    };
    pluginWrapper.onShowNativeGui = (int trackIdx, int plugIdx) {
        client.sendRequest(makePluginShowGuiRequest(trackIdx, plugIdx));
        model.hintText = format("Requested native editor GUI for track %d slot %d", trackIdx, plugIdx);
        refreshUi();
    };

    // Global Keyboard Shortcuts (Space, F5..F9, Typing Keyboard to Piano)
    root.onGlobalKey = (uint keyCode, uint flags) {
        if (keyCode == KeyCode.SPACE) {
            toolbar.onPlayToggle();
        } else if (keyCode == KeyCode.F5) {
            desktop.toggleWindow("win_playlist");
            refreshUi();
        } else if (keyCode == KeyCode.F6) {
            desktop.toggleWindow("win_channel_rack");
            refreshUi();
        } else if (keyCode == KeyCode.F7) {
            desktop.toggleWindow("win_piano_roll");
            refreshUi();
        } else if (keyCode == KeyCode.F8) {
            browserPanel.visibility = (browserPanel.visibility == Visibility.Visible)
                ? Visibility.Gone : Visibility.Visible;
            root.requestLayout();
            refreshUi();
        } else if (keyCode == KeyCode.F9) {
            desktop.toggleWindow("win_mixer");
            refreshUi();
        } else if (model.typingKeyboardEnabled) {
            // FL Studio Typing Keyboard to Piano (Z..M -> C4..B4)
            int pitch = -1;
            switch (keyCode) {
                case KeyCode.KEY_Z: pitch = 60; break;
                case KeyCode.KEY_S: pitch = 61; break;
                case KeyCode.KEY_X: pitch = 62; break;
                case KeyCode.KEY_D: pitch = 63; break;
                case KeyCode.KEY_C: pitch = 64; break;
                case KeyCode.KEY_V: pitch = 65; break;
                case KeyCode.KEY_G: pitch = 66; break;
                case KeyCode.KEY_B: pitch = 67; break;
                case KeyCode.KEY_H: pitch = 68; break;
                case KeyCode.KEY_N: pitch = 69; break;
                case KeyCode.KEY_J: pitch = 70; break;
                case KeyCode.KEY_M: pitch = 71; break;
                default: break;
            }
            if (pitch >= 0) {
                const int targetTrk = (model.selectedChannelIndex < cast(int)model.channels.length)
                    ? model.channels[model.selectedChannelIndex].mixerInsert
                    : 1;
                sendVirtualNote(targetTrk, cast(uint)pitch);
                model.hintText = format("Typing Piano Note: MIDI %d", pitch);
                refreshUi();
            }
        }
    };

    // 33ms prototype playback & meter animation tick (only advances locally if engine is not connected)
    root.onAnimationTick = () {
        if (!model.isPlaying) return;
        if (!client.isRunning()) {
            model.playheadPosSec += 0.033;
            const double loopLen = (model.playbackMode == PlaybackMode.PAT)
                ? 4.0 * beatDurationSec(model.bpm)
                : (model.loopEndSec > model.loopStartSec ? model.loopEndSec : 8.0);
            if (model.playheadPosSec >= loopLen) {
                model.playheadPosSec = 0.0;
            }
            // Animate subtle responsive peak meters in prototype mode
            const int step = model.currentStepIndex();
            float masterP = 0.0f;
            foreach (i, ref trk; model.tracks) {
                if (i == 0) continue;
                float trig = 0.08f;
                foreach (const ref ch; model.channels) {
                    if (!ch.muted && ch.mixerInsert == trk.index && step >= 0 && ch.steps[step]) {
                        trig = 0.72f + 0.22f * fabs(sinf(cast(float)(model.playheadPosSec * 8.0 + i)));
                    }
                }
                trk.peakL = trk.peakL * 0.72f + trig * 0.28f;
                trk.peakR = trk.peakR * 0.70f + trig * 0.27f;
                if (trk.peakL > masterP) masterP = trk.peakL;
            }
            if (model.tracks.length > 0) {
                model.tracks[0].peakL = masterP * model.masterVolume;
                model.tracks[0].peakR = masterP * model.masterVolume * 0.96f;
            }
            refreshUi();
        }
    };

    applyThemeColors();
    refreshUi(true);

    window.mainWidget = root;
    window.show();
    root.setTimer(33);
    int res = Platform.instance.enterMessageLoop();
    client.stop();
    return res;
}
