module gui_d.browser_panel;

import dlangui;
import dlangui.widgets.controls : Button, TextWidget;
import dlangui.widgets.editors : EditLine;
import dlangui.widgets.layouts : HorizontalLayout, VerticalLayout;
import dlangui.widgets.scroll : ScrollWidget;
import gui_d.theme : Y2kColor;
import gui_d.transport_model : ChannelState, TransportModel;
import std.algorithm : canFind;
import std.format : format;
import std.string : toLower;
import std.utf : toUTF32, toUTF8;

enum BrowserCategory {
    ALL,
    PLUGINS,
    SAMPLES,
    PROJECTS,
}

struct BrowserEntry {
    BrowserCategory category;
    string label;
    string uri;
    bool isInstrument = false;
}

immutable BrowserEntry[] DEFAULT_BROWSER_ENTRIES = [
    // Generators
    BrowserEntry(BrowserCategory.PLUGINS,  "Gen: 3x Osc",        "builtin://3xosc",        true),
    BrowserEntry(BrowserCategory.PLUGINS,  "Gen: Acid Bass",     "builtin://acid_bass",    true),
    BrowserEntry(BrowserCategory.PLUGINS,  "Gen: E-Piano",       "builtin://epiano",       true),
    BrowserEntry(BrowserCategory.PLUGINS,  "Gen: Organ",         "builtin://organ",        true),
    BrowserEntry(BrowserCategory.PLUGINS,  "Gen: Drum Machine",  "builtin://drum_machine", true),
    BrowserEntry(BrowserCategory.PLUGINS,  "Gen: DR-8 Kick",     "builtin://dr8_kick",     true),
    BrowserEntry(BrowserCategory.PLUGINS,  "Gen: DR-8 Snare",    "builtin://dr8_snare",    true),
    BrowserEntry(BrowserCategory.PLUGINS,  "Gen: DR-8 Hat",      "builtin://dr8_hat",      true),
    BrowserEntry(BrowserCategory.PLUGINS,  "Gen: Sampler",       "builtin://sampler",      true),
    BrowserEntry(BrowserCategory.PLUGINS,  "VST3: Dexed",        "testdata/Dexed.vst3",    true),
    // Effects
    BrowserEntry(BrowserCategory.PLUGINS,  "FX: EQ",             "builtin://eq",           false),
    BrowserEntry(BrowserCategory.PLUGINS,  "FX: Compressor",     "builtin://compressor",   false),
    BrowserEntry(BrowserCategory.PLUGINS,  "FX: Delay",          "builtin://delay",        false),
    BrowserEntry(BrowserCategory.PLUGINS,  "FX: Reverb",         "builtin://reverb",       false),
    BrowserEntry(BrowserCategory.PLUGINS,  "FX: Chorus",         "builtin://chorus",       false),
    BrowserEntry(BrowserCategory.PLUGINS,  "FX: Bitcrusher",     "builtin://bitcrusher",   false),
    BrowserEntry(BrowserCategory.PLUGINS,  "FX: Limiter",        "builtin://limiter",      false),
    BrowserEntry(BrowserCategory.PLUGINS,  "FX: HOTT",           "builtin://hott",         false),
    BrowserEntry(BrowserCategory.PLUGINS,  "FX: Phaser",         "builtin://phaser",       false),
    // Samples
    BrowserEntry(BrowserCategory.SAMPLES,  "Wav: loop140.wav",   "testdata/loop140.wav",   false),
    BrowserEntry(BrowserCategory.SAMPLES,  "Wav: bb140.wav",     "testdata/bb140.wav",     false),
    // Projects
    BrowserEntry(BrowserCategory.PROJECTS, "Proj: acid.hbk",     "testdata/acid.hbk",      false),
    BrowserEntry(BrowserCategory.PROJECTS, "Proj: timeline_midi.hbk", "testdata/timeline_midi.hbk", false),
    BrowserEntry(BrowserCategory.PROJECTS, "Proj: empty.hbk",    "testdata/empty.hbk",     false),
];

/// FL Studio-style left Browser panel (All / Plugins / Samples / Projects + Search).
class BrowserPanel : VerticalLayout {
    private TransportModel* model_;
    private BrowserCategory activeCat_ = BrowserCategory.ALL;
    private EditLine edSearch_;
    private VerticalLayout listBody_;

    void delegate(string label, string uri, bool isInstrument) onAddGenerator;
    void delegate(string label, string uri) onAddEffect;
    void delegate(string path) onAddSampleClip;
    void delegate(string path) onLoadProject;
    void delegate(string searchPath) onScanVst3;
    void delegate(uint note) onPreviewNote;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = 240;
        minWidth = 220;
        layoutHeight = FILL_PARENT;
        backgroundColor = Y2kColor.BG_PANEL_DARK;
        padding = Rect(4, 4, 4, 4);

        auto hdr = new TextWidget("br_hdr", "BROWSER"d);
        hdr.textColor = Y2kColor.FG_LCD_AMBER;
        hdr.padding = Rect(4, 2, 4, 4);
        addChild(hdr);

        // Category tabs
        auto tabs = new HorizontalLayout("br_tabs");
        tabs.layoutWidth = FILL_PARENT;
        auto btnAll = new Button("br_all", "ALL"d);
        auto btnPlg = new Button("br_plg", "PLG"d);
        auto btnSmp = new Button("br_smp", "SMP"d);
        auto btnPrj = new Button("br_prj", "PRJ"d);
        tabs.addChild(btnAll);
        tabs.addChild(btnPlg);
        tabs.addChild(btnSmp);
        tabs.addChild(btnPrj);
        addChild(tabs);

        btnAll.click = (Widget s) { activeCat_ = BrowserCategory.ALL; refresh(); return true; };
        btnPlg.click = (Widget s) { activeCat_ = BrowserCategory.PLUGINS; refresh(); return true; };
        btnSmp.click = (Widget s) { activeCat_ = BrowserCategory.SAMPLES; refresh(); return true; };
        btnPrj.click = (Widget s) { activeCat_ = BrowserCategory.PROJECTS; refresh(); return true; };

        edSearch_ = new EditLine("br_search", ""d);
        edSearch_.layoutWidth = FILL_PARENT;
        edSearch_.contentChange = (EditableContent content) {
            refresh();
        };
        addChild(edSearch_);

        auto btnScan = new Button("br_scan", "Scan VST3 Folders"d);
        btnScan.layoutWidth = FILL_PARENT;
        btnScan.click = (Widget s) {
            if (onScanVst3 !is null) {
                onScanVst3("testdata");
                onScanVst3("/Library/Audio/Plug-Ins/VST3");
            }
            return true;
        };
        addChild(btnScan);

        auto scroll = new ScrollWidget("br_scroll");
        scroll.layoutWidth = FILL_PARENT;
        scroll.layoutHeight = FILL_PARENT;
        listBody_ = new VerticalLayout("br_list");
        listBody_.layoutWidth = FILL_PARENT;
        listBody_.layoutHeight = WRAP_CONTENT;
        scroll.contentWidget = listBody_;
        addChild(scroll);

        // Mini Audition row at bottom of Browser
        auto keyRow = new HorizontalLayout("br_keys");
        keyRow.layoutWidth = FILL_PARENT;
        static immutable uint[5] NOTES = [60, 62, 64, 67, 72];
        static immutable string[5] LABELS = ["C4", "D4", "E4", "G4", "C5"];
        foreach (i; 0 .. 5) {
            const uint n = NOTES[i];
            auto bk = new Button(null, toUTF32(LABELS[i]));
            bk.click = (Widget s) {
                if (onPreviewNote !is null) onPreviewNote(n);
                return true;
            };
            keyRow.addChild(bk);
        }
        addChild(keyRow);

        refresh();
    }

    void refresh() {
        listBody_.removeAllChildren();
        const string q = toLower(toUTF8(edSearch_.text));

        void addEntryButton(BrowserCategory cat, string label, string uri, bool isInst) {
            if (activeCat_ != BrowserCategory.ALL && activeCat_ != cat) return;
            if (q.length > 0 && !canFind(toLower(label), q) && !canFind(toLower(uri), q)) return;
            auto btn = new Button(null, toUTF32(label));
            btn.layoutWidth = FILL_PARENT;
            btn.click = (Widget s) {
                if (cat == BrowserCategory.PLUGINS) {
                    if (isInst) {
                        if (onAddGenerator !is null) onAddGenerator(label, uri, true);
                    } else {
                        if (onAddEffect !is null) onAddEffect(label, uri);
                    }
                } else if (cat == BrowserCategory.SAMPLES) {
                    if (onAddSampleClip !is null) onAddSampleClip(uri);
                } else if (cat == BrowserCategory.PROJECTS) {
                    if (onLoadProject !is null) onLoadProject(uri);
                }
                return true;
            };
            listBody_.addChild(btn);
        }

        foreach (e; DEFAULT_BROWSER_ENTRIES) {
            addEntryButton(e.category, e.label, e.uri, e.isInstrument);
        }
        if (model_) {
            foreach (pd; model_.scannedPlugins) {
                const string lbl = (pd.is_instrument ? "VST3: " : "VST3 FX: ") ~
                                   (pd.name.length > 0 ? pd.name : pd.path);
                addEntryButton(BrowserCategory.PLUGINS, lbl, pd.path, pd.is_instrument);
            }
        }
    }
}
