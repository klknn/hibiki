module gui_d.device_panel;

import dlangui;
import dlangui.widgets.combobox : ComboBox;
import dlangui.widgets.controls : Button, TextWidget;
import dlangui.widgets.editors : EditLine;
import dlangui.widgets.layouts : HorizontalLayout, VerticalLayout;
import dlangui.widgets.scrollbar : ScrollBar, SliderWidget;
import dlangui.widgets.scroll : ScrollWidget;
import gui_d.theme : Y2kColor;
import gui_d.transport_model : PluginSlotState, TransportModel;
import std.conv : to;
import std.format : format;
import std.utf : toUTF32, toUTF8;

/// Built-in instrument and effect entries matching `engine/instruments/registry.cpp` & `engine/effects/registry.cpp`.
struct BuiltinDeviceEntry {
    string label;
    string uri;
}

immutable BuiltinDeviceEntry[] BUILTIN_DEVICES = [
    BuiltinDeviceEntry("Inst: 3xOsc",        "builtin://3xosc"),
    BuiltinDeviceEntry("Inst: Acid Bass",    "builtin://acid_bass"),
    BuiltinDeviceEntry("Inst: E-Piano",      "builtin://epiano"),
    BuiltinDeviceEntry("Inst: Organ",        "builtin://organ"),
    BuiltinDeviceEntry("Inst: Drum Machine", "builtin://drum_machine"),
    BuiltinDeviceEntry("Inst: DR-8 Kick",    "builtin://dr8_kick"),
    BuiltinDeviceEntry("Inst: DR-8 Snare",   "builtin://dr8_snare"),
    BuiltinDeviceEntry("Inst: DR-8 Hat",     "builtin://dr8_hat"),
    BuiltinDeviceEntry("Inst: Sampler",      "builtin://sampler"),
    BuiltinDeviceEntry("FX: EQ",             "builtin://eq"),
    BuiltinDeviceEntry("FX: Compressor",     "builtin://compressor"),
    BuiltinDeviceEntry("FX: Delay",          "builtin://delay"),
    BuiltinDeviceEntry("FX: Reverb",         "builtin://reverb"),
    BuiltinDeviceEntry("FX: Chorus",         "builtin://chorus"),
    BuiltinDeviceEntry("FX: Bitcrusher",     "builtin://bitcrusher"),
    BuiltinDeviceEntry("FX: Limiter",        "builtin://limiter"),
    BuiltinDeviceEntry("FX: HOTT",           "builtin://hott"),
    BuiltinDeviceEntry("FX: Phaser",         "builtin://phaser"),
];

/// Left-hand DAW Browser & Device Rack panel for loading clips, built-ins, VST3s, and tweaking params.
class DeviceRackPanel : ScrollWidget {
    private TransportModel* model_;
    private TextWidget lblSelectedTrack_;
    private ComboBox cmbBuiltin_;
    private EditLine edVstPath_;
    private EditLine edAudioPath_;
    private VerticalLayout chainContainer_;
    private VerticalLayout scanContainer_;
    private VerticalLayout paramContainer_;

    void delegate(int trackIdx) onAddTrack;
    void delegate(int trackIdx, string path, float durBeats) onAddClip;
    void delegate(int trackIdx, string uri) onLoadPlugin;
    void delegate(int trackIdx, int pluginIdx) onRemovePlugin;
    void delegate(int trackIdx, int pluginIdx) onShowPluginGui;
    void delegate(int trackIdx, int pluginIdx, uint paramId, float value) onSetParam;
    void delegate(string searchPath) onScanVst3;
    void delegate(int trackIdx, uint note, bool noteOn) onVirtualMidi;
    void delegate() onSelectionChanged;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = 310;
        minWidth = 290;
        layoutHeight = FILL_PARENT;
        backgroundColor = Y2kColor.BG_PANEL;

        auto body = new VerticalLayout("device_body");
        body.layoutWidth = FILL_PARENT;
        body.layoutHeight = WRAP_CONTENT;
        body.padding = Rect(6, 6, 6, 6);

        // 1. Track selector
        auto trkRow = new HorizontalLayout("trk_row");
        trkRow.layoutWidth = FILL_PARENT;
        lblSelectedTrack_ = new TextWidget("lbl_sel_trk", "Track 1"d);
        lblSelectedTrack_.textColor = Y2kColor.FG_LCD_AMBER;
        lblSelectedTrack_.padding = Rect(4, 2, 4, 2);
        auto btnPrevTrk = new Button("btn_prev_trk", "<"d);
        auto btnNextTrk = new Button("btn_next_trk", ">"d);
        auto btnNewTrk  = new Button("btn_new_trk", "+ TRK"d);
        trkRow.addChild(btnPrevTrk);
        trkRow.addChild(lblSelectedTrack_);
        trkRow.addChild(btnNextTrk);
        trkRow.addChild(btnNewTrk);
        body.addChild(trkRow);

        btnPrevTrk.click = delegate(Widget src) {
            if (model_ && model_.selectedTrack > 0) {
                model_.selectedTrack--;
                refresh();
                if (onSelectionChanged !is null) onSelectionChanged();
            }
            return true;
        };
        btnNextTrk.click = delegate(Widget src) {
            if (model_) {
                model_.selectedTrack++;
                model_.ensureTrackIndex(model_.selectedTrack);
                refresh();
                if (onSelectionChanged !is null) onSelectionChanged();
            }
            return true;
        };
        btnNewTrk.click = delegate(Widget src) {
            if (!model_) return true;
            int nextIdx = cast(int)model_.tracks.length;
            model_.ensureTrackIndex(nextIdx);
            model_.selectedTrack = nextIdx;
            if (onAddTrack !is null) onAddTrack(nextIdx);
            refresh();
            return true;
        };

        // 2. Clip Creation (Audio WAV or MIDI Clip)
        body.addChild(makeSectionHeader("TIMELINE CLIPS"));
        edAudioPath_ = new EditLine("ed_audio_path", "testdata/loop140.wav"d);
        edAudioPath_.layoutWidth = FILL_PARENT;
        body.addChild(edAudioPath_);

        auto clipBtnRow = new HorizontalLayout("clip_btn_row");
        clipBtnRow.layoutWidth = FILL_PARENT;
        auto btnAddAudio = new Button("btn_add_audio", "+ AUDIO CLIP"d);
        auto btnAddMidi  = new Button("btn_add_midi",  "+ MIDI CLIP"d);
        clipBtnRow.addChild(btnAddAudio);
        clipBtnRow.addChild(btnAddMidi);
        body.addChild(clipBtnRow);

        btnAddAudio.click = delegate(Widget src) {
            if (model_ && onAddClip !is null) {
                onAddClip(model_.selectedTrack, toUTF8(edAudioPath_.text), 8.0f);
            }
            return true;
        };
        btnAddMidi.click = delegate(Widget src) {
            if (model_ && onAddClip !is null) {
                onAddClip(model_.selectedTrack, "", 8.0f);
            }
            return true;
        };

        // 3. Built-in Devices
        body.addChild(makeSectionHeader("BUILTIN DEVICES"));
        dstring[] items;
        foreach (d; BUILTIN_DEVICES) items ~= toUTF32(d.label);
        cmbBuiltin_ = new ComboBox("cmb_builtin", items);
        cmbBuiltin_.selectedItemIndex = 0;
        cmbBuiltin_.layoutWidth = FILL_PARENT;
        body.addChild(cmbBuiltin_);

        auto btnLoadBuiltin = new Button("btn_load_builtin", "LOAD BUILTIN ON TRACK"d);
        btnLoadBuiltin.layoutWidth = FILL_PARENT;
        body.addChild(btnLoadBuiltin);
        btnLoadBuiltin.click = delegate(Widget src) {
            if (!model_ || onLoadPlugin is null) return true;
            int idx = cmbBuiltin_.selectedItemIndex;
            if (idx >= 0 && idx < cast(int)BUILTIN_DEVICES.length) {
                onLoadPlugin(model_.selectedTrack, BUILTIN_DEVICES[idx].uri);
            }
            return true;
        };

        // 4. VST3 Plugin Loader & Scanner
        body.addChild(makeSectionHeader("VST3 PLUGINS"));
        edVstPath_ = new EditLine("ed_vst_path", "testdata/Dexed.vst3"d);
        edVstPath_.layoutWidth = FILL_PARENT;
        body.addChild(edVstPath_);

        auto vstBtnRow = new HorizontalLayout("vst_btn_row");
        vstBtnRow.layoutWidth = FILL_PARENT;
        auto btnLoadVst = new Button("btn_load_vst", "LOAD VST3"d);
        auto btnScanVst = new Button("btn_scan_vst", "SCAN VST3"d);
        vstBtnRow.addChild(btnLoadVst);
        vstBtnRow.addChild(btnScanVst);
        body.addChild(vstBtnRow);

        btnLoadVst.click = delegate(Widget src) {
            if (model_ && onLoadPlugin !is null) {
                onLoadPlugin(model_.selectedTrack, toUTF8(edVstPath_.text));
            }
            return true;
        };
        btnScanVst.click = delegate(Widget src) {
            if (onScanVst3 !is null) {
                onScanVst3("testdata");
                onScanVst3("/Library/Audio/Plug-Ins/VST3");
            }
            return true;
        };

        scanContainer_ = new VerticalLayout("scan_container");
        scanContainer_.layoutWidth = FILL_PARENT;
        body.addChild(scanContainer_);

        // 5. Virtual MIDI Audition Keys
        body.addChild(makeSectionHeader("AUDITION KEYS (MIDI)"));
        auto keyRow = new HorizontalLayout("audition_keys");
        keyRow.layoutWidth = FILL_PARENT;
        static immutable uint[8] NOTES = [60, 62, 64, 65, 67, 69, 71, 72];
        static immutable string[8] LABELS = ["C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5"];
        foreach (i; 0 .. 8) {
            const uint note = NOTES[i];
            auto btnKey = new Button("key_" ~ LABELS[i], toUTF32(LABELS[i]));
            btnKey.click = delegate(Widget src) {
                if (model_ && onVirtualMidi !is null) {
                    onVirtualMidi(model_.selectedTrack, note, true);
                }
                return true;
            };
            keyRow.addChild(btnKey);
        }
        body.addChild(keyRow);

        // 6. Track Plugin Chain & Parameters
        body.addChild(makeSectionHeader("LOADED DEVICES & PARAMS"));
        chainContainer_ = new VerticalLayout("chain_container");
        chainContainer_.layoutWidth = FILL_PARENT;
        body.addChild(chainContainer_);

        paramContainer_ = new VerticalLayout("param_container");
        paramContainer_.layoutWidth = FILL_PARENT;
        body.addChild(paramContainer_);

        contentWidget = body;
    }

    private TextWidget makeSectionHeader(string title) {
        auto w = new TextWidget(null, toUTF32(title));
        w.textColor = Y2kColor.FG_LCD_CYAN;
        w.backgroundColor = Y2kColor.BG_PANEL_DARK;
        w.padding = Rect(4, 3, 4, 3);
        w.margins = Rect(0, 6, 0, 2);
        w.layoutWidth = FILL_PARENT;
        return w;
    }

    /// Rebuilds the plugin chain, scanned VST3 list, and parameter sliders from `model_`.
    void refresh() {
        if (!model_) return;
        lblSelectedTrack_.text = toUTF32(format("Track %d", model_.selectedTrack + 1));

        // Scanned VST3 list (cap at 8 buttons for tidiness)
        scanContainer_.removeAllChildren();
        const size_t scanLimit = model_.scannedPlugins.length > 8 ? 8 : model_.scannedPlugins.length;
        foreach (i; 0 .. scanLimit) {
            const pd = model_.scannedPlugins[i];
            auto btn = new Button(null, toUTF32("+ " ~ (pd.name.length > 0 ? pd.name : pd.path)));
            btn.layoutWidth = FILL_PARENT;
            const string plugPath = pd.path;
            btn.click = delegate(Widget src) {
                edVstPath_.text = toUTF32(plugPath);
                if (onLoadPlugin !is null) onLoadPlugin(model_.selectedTrack, plugPath);
                return true;
            };
            scanContainer_.addChild(btn);
        }

        // Loaded plugins on selectedTrack
        chainContainer_.removeAllChildren();
        paramContainer_.removeAllChildren();
        PluginSlotState[] plugins;
        foreach (const ref t; model_.tracks) {
            if (t.index == model_.selectedTrack) {
                plugins = cast(PluginSlotState[])t.plugins;
                break;
            }
        }
        if (plugins.length == 0) {
            auto emptyLbl = new TextWidget(null, "(No devices loaded on track)"d);
            emptyLbl.textColor = Y2kColor.FG_MUTED;
            chainContainer_.addChild(emptyLbl);
            return;
        }

        PluginSlotState* activePlug = null;
        foreach (ref p; plugins) {
            if (p.pluginIndex == model_.selectedPluginIndex) activePlug = &p;
            auto row = new HorizontalLayout();
            row.layoutWidth = FILL_PARENT;
            const int pIdx = p.pluginIndex;
            auto btnSel = new Button(null, toUTF32(format("[%d] %s", pIdx, p.name)));
            btnSel.layoutWidth = FILL_PARENT;
            btnSel.click = delegate(Widget src) {
                model_.selectedPluginIndex = pIdx;
                refresh();
                return true;
            };
            auto btnGui = new Button(null, "GUI"d);
            btnGui.click = delegate(Widget src) {
                if (onShowPluginGui !is null) onShowPluginGui(model_.selectedTrack, pIdx);
                return true;
            };
            auto btnDel = new Button(null, "X"d);
            btnDel.click = delegate(Widget src) {
                if (onRemovePlugin !is null) onRemovePlugin(model_.selectedTrack, pIdx);
                return true;
            };
            row.addChild(btnSel);
            row.addChild(btnGui);
            row.addChild(btnDel);
            chainContainer_.addChild(row);
        }

        if (activePlug is null) activePlug = &plugins[0];
        const size_t pCount = activePlug.params.length > 10 ? 10 : activePlug.params.length;
        foreach (i; 0 .. pCount) {
            const pm = activePlug.params[i];
            auto pRow = new HorizontalLayout();
            pRow.layoutWidth = FILL_PARENT;
            auto pLbl = new TextWidget(null, toUTF32(pm.name.length > 14 ? pm.name[0 .. 14] : pm.name));
            pLbl.minWidth = 105;
            pLbl.textColor = Y2kColor.FG_TEXT;
            auto slider = new SliderWidget(null);
            slider.minValue = 0;
            slider.maxValue = 1000;
            slider.position = cast(int)(pm.currentValue * 1000.0f);
            slider.layoutWidth = FILL_PARENT;
            const int plugIdx = activePlug.pluginIndex;
            const uint paramId = pm.id;
            slider.scrollEvent = delegate(AbstractSlider src, ScrollEvent ev) {
                const float norm = cast(float)src.position / 1000.0f;
                if (onSetParam !is null) onSetParam(model_.selectedTrack, plugIdx, paramId, norm);
                return true;
            };
            pRow.addChild(pLbl);
            pRow.addChild(slider);
            paramContainer_.addChild(pRow);
        }
    }
}
