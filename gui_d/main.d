module gui_d.main;

import core.math : fabs;
import core.stdc.math : sinf;
import core.thread : Thread;
import core.time : msecs;
import dlangui;
import dlangui.graphics.colors : COLOR_TRANSPARENT;
import dlangui.widgets.controls : TextWidget;
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
import gui_d.theme : activeDawUiFontFace, applyFlatThemeOverrides, dp, FlCompactButton, FlThemeId,
    registerBundledDawFonts;
import gui_d.timeline_view : beatDurationSec, TimelineCanvasWidget;
import gui_d.toolbar : FlToolbarWidget;
import gui_d.transport_model;
import gui_d.workspace : DesktopLayoutPreset, FlDesktopCanvas, FlWindowFrame;
import pb.commands : TransportCmd;
import pb.core : MidiEvent;
import pb.notifications : Notification;
import std.file : exists;
import std.format : format;
import std.path : baseName;
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
    bool delegate(uint keyCode, uint flags) onGlobalKey;
    void delegate() onAnimationTick;

    this(string id) {
        super(id);
    }

    @property override bool wantsKeyTracking() {
        return true;
    }

    override bool onKeyEvent(KeyEvent event) {
        if (event.action == KeyAction.KeyDown && onGlobalKey !is null) {
            // Don't hijack typing while user is typing in Search EditLine
            if (window !is null && cast(EditLine)window.focusedWidget !is null) {
                return super.onKeyEvent(event);
            }
            if (onGlobalKey(event.keyCode, event.flags)) {
                return true;
            }
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
    registerBundledDawFonts();

    Window window = Platform.instance.createWindow(
        "Hibiki DAW — FL Studio 2026 Workspace",
        null, WindowFlag.Resizable, 1480, 920);

    TransportModel model;
    model.initDefaultTemplate();
    EngineProcessClient client;

    const initPal = model.palette();
    applyFlatThemeOverrides(initPal);

    auto root = new FlMainRootLayout("root");
    root.layoutWidth = FILL_PARENT;
    root.layoutHeight = FILL_PARENT;

    // 1. Top FL Studio Menu Strip + Hint Panel + Theme Quick Toggle
    auto menuRow = new HorizontalLayout("menu_row");
    menuRow.padding = Rect(dp(6), dp(3), dp(6), dp(3));
    menuRow.layoutWidth = FILL_PARENT;
    menuRow.layoutHeight = WRAP_CONTENT;

    auto hintBar = new TextWidget("hint_bar", toUTF32(model.hintText));
    hintBar.padding = Rect(dp(10), dp(4), dp(10), dp(4));
    hintBar.margins = Rect(dp(8), 0, dp(6), 0);
    hintBar.fontSize = dp(11);
    hintBar.fontWeight = FontWeight.Normal;
    hintBar.layoutWidth = FILL_PARENT;

    auto btnTheme = new FlCompactButton("btn_theme", "Theme: FL Default Dark"d, initPal, 11, 22, true, false);
    FlCompactButton[] menuBarButtons;

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
                                         &model, playlistCanvas, Rect(0, 0, dp(730), dp(430)), 360, 200);
    auto winChannelRack = new FlWindowFrame("win_channel_rack", "Channel Rack - Pattern 1",
                                            &model, channelRack, Rect(dp(736), 0, dp(1180), dp(340)), 420, 170);
    auto winPianoRoll = new FlWindowFrame("win_piano_roll", "Piano roll - Acid Bass",
                                          &model, pianoRollCanvas, Rect(0, dp(436), dp(610), dp(760)), 340, 200);
    auto winMixer = new FlWindowFrame("win_mixer", "Mixer - Master",
                                      &model, mixerCanvas, Rect(dp(616), dp(346), dp(1180), dp(760)), 440, 240);
    auto winPlugin = new FlWindowFrame("win_plugin", "Fruity Wrapper - Acid Bass",
                                       &model, pluginWrapper, Rect(dp(320), dp(160), dp(800), dp(480)), 360, 240);
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
    statusBar.padding = Rect(dp(10), dp(3), dp(10), dp(3));
    statusBar.fontSize = dp(11);
    statusBar.fontWeight = FontWeight.Normal;
    statusBar.layoutWidth = FILL_PARENT;

    void applyThemeColors() {
        const pal = model.palette();
        applyFlatThemeOverrides(pal);
        root.backgroundColor = pal.bgDesktop;
        const uint barBg = pal.retroBevel ? pal.bgToolbar : pal.bgWindowHeader;
        menuRow.backgroundColor = barBg;
        hintBar.backgroundColor = pal.bgPanelAlt;
        hintBar.textColor = pal.retroBevel ? pal.fgText : pal.fgMuted;
        statusBar.backgroundColor = barBg;
        statusBar.textColor = pal.retroBevel ? pal.fgText : pal.fgMuted;
        const string fface = activeDawUiFontFace();
        if (fface.length > 0) {
            hintBar.fontFace = fface;
            statusBar.fontFace = fface;
        }
        btnTheme.text = toUTF32("Theme: " ~ pal.name);
        btnTheme.updatePalette(pal);
        foreach (mb; menuBarButtons) {
            mb.updatePalette(pal);
            mb.customFillColor = barBg;
            mb.customBorderColor = COLOR_TRANSPARENT;
        }
        browserPanel.refresh();
        root.invalidate();
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
    auto btnFile = new FlCompactButton("m_file", "FILE"d, initPal, 11, 22, true, false);
    auto btnEdit = new FlCompactButton("m_edit", "EDIT"d, initPal, 11, 22, true, false);
    auto btnAdd  = new FlCompactButton("m_add",  "ADD"d, initPal, 11, 22, true, false);
    auto btnPat  = new FlCompactButton("m_pat",  "PATTERNS"d, initPal, 11, 22, true, false);
    auto btnView = new FlCompactButton("m_view", "VIEW"d, initPal, 11, 22, true, false);
    auto btnOpt  = new FlCompactButton("m_opt",  "OPTIONS"d, initPal, 11, 22, true, false);
    auto btnHelp = new FlCompactButton("m_help", "HELP"d, initPal, 11, 22, true, false);

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
            model.activePickerKind = PlaylistClipKind.PATTERN;
            model.activePickerIndex = idx;
            model.hintText = format("Selected %s", model.patterns[idx].name);
            refreshUi(true);
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
            new Action(604, "Theme: Warm Amber"d),
            new Action(605, "Theme: Royal Violet"d),
            new Action(606, "Theme: Windows 95"d)
        );
        showMenuPopup(s, m, (int id) {
            if (id == 601) model.themeId = FlThemeId.DEFAULT_DARK;
            else if (id == 602) model.themeId = FlThemeId.ULTRAMARINE;
            else if (id == 603) model.themeId = FlThemeId.MIDNIGHT_MINT;
            else if (id == 604) model.themeId = FlThemeId.WARM_AMBER;
            else if (id == 605) model.themeId = FlThemeId.ROYAL_VIOLET;
            else if (id == 606) model.themeId = FlThemeId.WIN95;
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
        enum int NUM_THEMES = cast(int)(FlThemeId.max) + 1;
        model.themeId = cast(FlThemeId)((cast(int)model.themeId + 1) % NUM_THEMES);
        applyThemeColors();
        refreshUi();
        return true;
    };

    foreach (mb; [btnFile, btnEdit, btnAdd, btnPat, btnView, btnOpt, btnHelp]) {
        mb.focusable = false;
        menuBarButtons ~= mb;
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
            model.isRecording = false;
        } else {
            client.sendRequest(makeTransportRequest(TransportCmd.Action.ACTION_PLAY));
            model.isPlaying = true;
        }
        refreshUi();
    };
    toolbar.onStop = () {
        client.sendRequest(makeTransportRequest(TransportCmd.Action.ACTION_STOP));
        client.sendRequest(makeSeekRequest(0.0));
        model.isPlaying = false;
        model.isRecording = false;
        model.playheadPosSec = 0.0;
        foreach (ref trk; model.tracks) {
            trk.peakL = 0.0f;
            trk.peakR = 0.0f;
        }
        refreshUi();
    };
    toolbar.onRecordToggle = () {
        model.isRecording = !model.isRecording;
        if (model.isRecording) {
            model.isPlaying = true;
            client.sendRequest(makeTransportRequest(TransportCmd.Action.ACTION_RECORD));
        } else {
            model.isPlaying = false;
            client.sendRequest(makeTransportRequest(TransportCmd.Action.ACTION_STOP));
        }
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
        model.activePickerKind = PlaylistClipKind.PATTERN;
        model.activePickerIndex = model.selectedPatternIndex;
        model.hintText = "Selected " ~ model.patterns[model.selectedPatternIndex].name;
        refreshUi(true);
    };
    toolbar.onPatternAdd = () {
        const int idx = cast(int)model.patterns.length;
        model.patterns ~= PatternState(idx, format("Pattern %d", idx + 1), model.palette().clipPattern, 16);
        model.selectedPatternIndex = idx;
        model.activePickerKind = PlaylistClipKind.PATTERN;
        model.activePickerIndex = idx;
        model.hintText = format("Created Pattern %d", idx + 1);
        refreshUi(true);
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
        client.sendRequest(makePluginLoadRequest(ins, uri, slotIdx));
        model.hintText = format("Loaded %s onto Mixer Insert %d", label, ins);
        desktop.showWindow("win_mixer");
        refreshUi();
    };
    browserPanel.onAddSampleClip = (string path) {
        const int targetLane = (model.selectedTrack > 0) ? model.selectedTrack : 1;
        client.sendRequest(makeAddTimelineClipRequest(targetLane, path, cast(float)model.playheadPosSec, 8.0f));
        const size_t tArr = model.ensureTrackIndex(targetLane);
        TimelineClipState c;
        c.clipIndex = model.nextClipIndex(model.tracks[tArr]);
        c.kind = PlaylistClipKind.AUDIO;
        c.name = baseName(path);
        c.color = model.palette().clipAudio;
        c.startTimeSec = model.playheadPosSec;
        c.durationSec = 4.0 * beatDurationSec(model.bpm) * 2.0;
        c.waveform = model.samplePreview.waveform.dup;
        model.tracks[tArr].clips ~= c;
        model.hintText = format("Placed Audio Clip '%s' on Track %d", c.name, targetLane);
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
        if (winPlugin.visibility == Visibility.Visible) {
            pluginWrapper.setTargetChannel(chIdx);
            if (chIdx >= 0 && chIdx < cast(int)model.channels.length) {
                winPlugin.windowTitle = "Fruity Wrapper - " ~ model.channels[chIdx].name;
            }
        }
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
        model.selectPianoRollChannel(chIdx);
        desktop.showWindow("win_piano_roll");
        refreshUi();
    };
    channelRack.onMuteChannel = (int chIdx, bool muted) {
        model.hintText = format("Channel %d %s", chIdx + 1, muted ? "Muted" : "Unmuted");
        refreshUi();
    };
    channelRack.onRouteMixer = (int chIdx, int mixerInsert) {
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
    playlistCanvas.onDeleteClip = (int trackIdx, int clipIdx) {
        client.sendRequest(makeRemoveTimelineClipRequest(trackIdx, clipIdx));
        refreshUi();
    };
    playlistCanvas.onPaintClip = (int trackIdx, double startSec) {
        const idx = model.ensureTrackIndex(trackIdx);
        TimelineClipState clip;
        clip.clipIndex = model.nextClipIndex(model.tracks[idx]);
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
            static immutable string[4] AUDIO_NAMES = ["loop140.wav", "bb140.wav", "kick_punch.wav", "snare_crisp.wav"];
            static immutable string[4] AUDIO_PATHS = [
                "testdata/loop140.wav", "testdata/bb140.wav",
                "testdata/kick_punch.wav", "testdata/snare_crisp.wav"
            ];
            const size_t ai = (model.activePickerIndex >= 0 && model.activePickerIndex < 4)
                ? cast(size_t)model.activePickerIndex : 0;
            clip.name = AUDIO_NAMES[ai];
            clip.color = model.palette().clipAudio;
            clip.waveform = model.samplePreview.waveform.dup;
            client.sendRequest(makeAddTimelineClipRequest(trackIdx, AUDIO_PATHS[ai], cast(float)startSec, 8.0f));
        } else {
            static immutable string[2] AUTO_NAMES = ["Acid Cutoff Env", "Master Volume Env"];
            const size_t aui = (model.activePickerIndex >= 0 && model.activePickerIndex < 2)
                ? cast(size_t)model.activePickerIndex : 0;
            clip.name = AUTO_NAMES[aui];
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
        const int targetTrk = (model.activeMidi.channelIndex >= 0 &&
                               model.activeMidi.channelIndex < cast(int)model.channels.length)
            ? model.channels[model.activeMidi.channelIndex].mixerInsert
            : model.activeMidi.trackIndex;
        sendVirtualNote(targetTrk, pitch);
    };
    pianoRollCanvas.onTargetChannelChanged = (int chIdx) {
        refreshUi();
    };

    // Mixer Callbacks
    mixerCanvas.onVolumeChange = (int trackIdx, float vol) {
        client.sendRequest(makeTrackVolumeRequest(trackIdx, vol));
        refreshUi();
    };
    mixerCanvas.onPanChange = (int trackIdx, float pan) {
        client.sendRequest(makeTrackPanRequest(trackIdx, pan));
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
    mixerCanvas.onFxBypass = (int trackIdx, int slotIdx, bool enabled) {
        client.sendRequest(makePluginBypassRequest(trackIdx, slotIdx, enabled));
        refreshUi();
    };
    mixerCanvas.onLoadFxSlot = (int trackIdx, int slotIdx, string uri) {
        client.sendRequest(makePluginLoadRequest(trackIdx, uri, slotIdx));
        refreshUi();
    };
    mixerCanvas.onRemoveFxSlot = (int trackIdx, int slotIdx) {
        client.sendRequest(makePluginRemoveRequest(trackIdx, slotIdx));
        refreshUi();
    };
    mixerCanvas.onPromptLoadFx = (int trackIdx, int slotIdx) {
        static struct FxPreset { string name; string uri; }
        static immutable FxPreset[8] PRESETS = [
            FxPreset("Parametric EQ", "builtin://eq"),
            FxPreset("Compressor", "builtin://compressor"),
            FxPreset("Stereo Reverb", "builtin://reverb"),
            FxPreset("Ping-Pong Delay", "builtin://delay"),
            FxPreset("HOTT Multiband", "builtin://hott"),
            FxPreset("Fruity Limiter", "builtin://limiter"),
            FxPreset("Chorus", "builtin://chorus"),
            FxPreset("Bitcrusher", "builtin://bitcrusher"),
        ];
        FxPreset[] choices;
        foreach (p; PRESETS) choices ~= p;
        foreach (pd; model.scannedPlugins) {
            if (!pd.is_instrument) {
                choices ~= FxPreset("VST3: " ~ (pd.name.length > 0 ? pd.name : pd.path), pd.path);
            }
        }
        auto m = new MenuItem();
        foreach (i, c; choices) {
            m.add(new Action(800 + cast(int)i, toUTF32(c.name)));
        }
        showMenuPopup(mixerCanvas, m, (int id) {
            const int ci = id - 800;
            if (ci >= 0 && ci < cast(int)choices.length) {
                const size_t tArr = model.ensureTrackIndex(trackIdx);
                auto slot = TransportModel.makeDefaultPluginSlot(choices[ci].name, choices[ci].uri, false, slotIdx);
                model.tracks[tArr].plugins ~= slot;
                client.sendRequest(makePluginLoadRequest(trackIdx, choices[ci].uri, slotIdx));
                pluginWrapper.setTargetMixerFx(trackIdx, slotIdx);
                winPlugin.windowTitle = format("Fruity Wrapper - Insert %d Slot %d", trackIdx, slotIdx + 1);
                desktop.showWindow("win_plugin");
                model.hintText = format("Loaded '%s' -> Insert %d Slot %d", choices[ci].name, trackIdx, slotIdx + 1);
                refreshUi();
            }
        });
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
    pluginWrapper.onSetBypass = (int trackIdx, int plugIdx, bool enabled) {
        client.sendRequest(makePluginBypassRequest(trackIdx, plugIdx, enabled));
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
            return true;
        } else if (keyCode == KeyCode.F5) {
            desktop.toggleWindow("win_playlist");
            refreshUi();
            return true;
        } else if (keyCode == KeyCode.F6) {
            desktop.toggleWindow("win_channel_rack");
            refreshUi();
            return true;
        } else if (keyCode == KeyCode.F7) {
            desktop.toggleWindow("win_piano_roll");
            refreshUi();
            return true;
        } else if (keyCode == KeyCode.F8) {
            browserPanel.visibility = (browserPanel.visibility == Visibility.Visible)
                ? Visibility.Gone : Visibility.Visible;
            root.requestLayout();
            refreshUi();
            return true;
        } else if (keyCode == KeyCode.F9) {
            desktop.toggleWindow("win_mixer");
            refreshUi();
            return true;
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
                return true;
            }
        }
        return false;
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
