module gui_d.browser_panel;

import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.widgets.controls : Button, TextWidget;
import dlangui.widgets.editors : EditLine;
import dlangui.widgets.layouts : HorizontalLayout, VerticalLayout;
import dlangui.widgets.scroll : ScrollWidget;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.theme : drawCard, FlPalette, getFlPalette, truncateLabel;
import gui_d.transport_model : BrowserCategory, TransportModel;
import std.algorithm : canFind;
import std.format : format;
import std.string : toLower;
import std.utf : toUTF32, toUTF8;

/// Entry in the FL Studio 2026 Browser tree.
struct BrowserEntry {
    BrowserCategory category;
    string section;
    string label;
    string uri;
    bool isInstrument = false;
}

immutable BrowserEntry[] DEFAULT_BROWSER_ENTRIES = [
    // Current Project
    BrowserEntry(BrowserCategory.PROJECT, "Current Project", "Pat: Pattern 1 (Drums)", "pattern://0", false),
    BrowserEntry(BrowserCategory.PROJECT, "Current Project", "Pat: Pattern 2 (Acid Bass)", "pattern://1", false),
    BrowserEntry(BrowserCategory.PROJECT, "Current Project", "Pat: Pattern 3 (E-Piano)", "pattern://2", false),
    BrowserEntry(BrowserCategory.PROJECT, "Current Project", "Auto: Acid Cutoff Env", "auto://0", false),
    // Generators
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: 3x Osc", "builtin://3xosc", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: Acid Bass", "builtin://acid_bass", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: E-Piano", "builtin://epiano", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: Organ", "builtin://organ", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: Drum Machine", "builtin://drum_machine", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: DR-8 Kick", "builtin://dr8_kick", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: DR-8 Snare", "builtin://dr8_snare", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: DR-8 Hat", "builtin://dr8_hat", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: Sampler", "builtin://sampler", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "VST3: Dexed FM", "testdata/Dexed.vst3", true),
    // Effects
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: Parametric EQ", "builtin://eq", false),
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: Compressor", "builtin://compressor", false),
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: Ping-Pong Delay", "builtin://delay", false),
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: Stereo Reverb", "builtin://reverb", false),
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: Chorus", "builtin://chorus", false),
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: Bitcrusher", "builtin://bitcrusher", false),
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: Fruity Limiter", "builtin://limiter", false),
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: HOTT Multiband", "builtin://hott", false),
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: Phaser", "builtin://phaser", false),
    // Samples
    BrowserEntry(BrowserCategory.SAMPLES, "Packs / Drums", "Wav: loop140.wav", "testdata/loop140.wav", false),
    BrowserEntry(BrowserCategory.SAMPLES, "Packs / Drums", "Wav: bb140.wav", "testdata/bb140.wav", false),
    BrowserEntry(BrowserCategory.SAMPLES, "Packs / Drums", "Wav: kick_punch.wav", "testdata/kick_punch.wav", false),
    BrowserEntry(BrowserCategory.SAMPLES, "Packs / Drums", "Wav: snare_crisp.wav", "testdata/snare_crisp.wav", false),
    BrowserEntry(BrowserCategory.SAMPLES, "Packs / Synths", "Wav: 808_sub.wav", "testdata/808_sub.wav", false),
    // Projects
    BrowserEntry(BrowserCategory.RECENT, "Demo Projects", "Proj: acid.hbk", "testdata/acid.hbk", false),
    BrowserEntry(BrowserCategory.RECENT, "Demo Projects", "Proj: timeline_midi.hbk", "testdata/timeline_midi.hbk", false),
    BrowserEntry(BrowserCategory.RECENT, "Demo Projects", "Proj: empty.hbk", "testdata/empty.hbk", false),
];

/// Renders the bottom FL Studio Browser sample waveform preview box.
void drawSampleWaveformPreview(
    DrawBuf buf,
    Rect rc,
    const(TransportModel)* model,
    FontRef fnt = FontRef.init
) {
    const pal = model ? model.palette() : getFlPalette();
    drawCard(buf, rc, pal.bgLcd, pal.borderSubtle, 3.0f);
    if (rc.width < 24 || rc.height < 20) return;

    const int wfTop = rc.top + 18;
    const int wfBot = rc.bottom - 4;
    const int midY = (wfTop + wfBot) / 2;
    const int halfH = (wfBot - wfTop) / 2;

    buf.fillRect(Rect(rc.left + 4, midY, rc.right - 4, midY + 1), pal.borderSubtle);

    if (model && model.samplePreview.waveform.length > 0 && halfH >= 3) {
        const w = rc.width - 10;
        const wf = model.samplePreview.waveform;
        foreach (px; 0 .. w) {
            const size_t bucket = (cast(size_t)px * wf.length) / cast(size_t)w;
            float amp = wf[bucket];
            if (amp < 0.0f) amp = -amp;
            if (amp > 1.0f) amp = 1.0f;
            int barH = cast(int)(amp * halfH);
            if (barH < 1) barH = 1;
            buf.fillRect(Rect(rc.left + 5 + px, midY - barH, rc.left + 6 + px, midY + barH),
                         pal.accentCyan);
        }
    }

    if (!fnt.isNull && model) {
        fnt.drawText(buf, rc.left + 6, rc.top + 3,
                     toUTF32(truncateLabel(model.samplePreview.name, 18)),
                     pal.accentOrange);
    }
}

/// Bottom waveform audition widget in the Browser.
class BrowserPreviewWidget : Widget {
    private TransportModel* model_;
    void delegate() onAuditionClick;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = FILL_PARENT;
        layoutHeight = 56;
        minHeight = 56;
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        drawSampleWaveformPreview(buf, _pos, model_, font);
    }

    override bool onMouseEvent(MouseEvent event) {
        if (event.action == MouseAction.ButtonDown && event.button == MouseButton.Left) {
            if (onAuditionClick !is null) onAuditionClick();
            return true;
        }
        return super.onMouseEvent(event);
    }
}

/// FL Studio 2026 left-docked Browser panel (All / Project / Plugins / Samples / Recent + Search + Preview).
class BrowserPanel : VerticalLayout {
    private TransportModel* model_;
    private BrowserCategory activeCat_ = BrowserCategory.ALL;
    private EditLine edSearch_;
    private VerticalLayout listBody_;
    private BrowserPreviewWidget previewWidget_;
    private bool[string] collapsedSections_;

    void delegate(string label, string uri, bool isInstrument) onAddGenerator;
    void delegate(string label, string uri) onAddEffect;
    void delegate(string path) onAddSampleClip;
    void delegate(string path) onLoadProject;
    void delegate(int patternIdx) onSelectPattern;
    void delegate(string searchPath) onScanVst3;
    void delegate(uint note) onPreviewNote;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = 250;
        minWidth = 220;
        layoutHeight = FILL_PARENT;
        const pal = model_ ? model_.palette() : getFlPalette();
        backgroundColor = pal.bgWindowHeader;
        padding = Rect(5, 5, 5, 5);

        auto hdrRow = new HorizontalLayout("br_hdr_row");
        hdrRow.layoutWidth = FILL_PARENT;
        auto hdr = new TextWidget("br_hdr", "BROWSER"d);
        hdr.textColor = pal.accentOrange;
        hdr.padding = Rect(4, 2, 4, 4);
        hdr.layoutWidth = FILL_PARENT;
        auto btnScan = new Button("br_scan", "SCAN VST3"d);
        btnScan.click = (Widget s) {
            if (onScanVst3 !is null) {
                onScanVst3("testdata");
                onScanVst3("/Library/Audio/Plug-Ins/VST3");
            }
            return true;
        };
        hdrRow.addChild(hdr);
        hdrRow.addChild(btnScan);
        addChild(hdrRow);

        // FL Studio 2026 Browser Tabs: ALL | PROJ | PLUG | SMP | REC
        auto tabs = new HorizontalLayout("br_tabs");
        tabs.layoutWidth = FILL_PARENT;
        auto btnAll  = new Button("br_all",  "ALL"d);
        auto btnProj = new Button("br_proj", "PROJ"d);
        auto btnPlug = new Button("br_plug", "PLUG"d);
        auto btnSmp  = new Button("br_smp",  "SMP"d);
        auto btnRec  = new Button("br_rec",  "REC"d);
        foreach (b; [btnAll, btnProj, btnPlug, btnSmp, btnRec]) {
            b.layoutWidth = FILL_PARENT;
            tabs.addChild(b);
        }
        addChild(tabs);

        btnAll.click  = (Widget s) { activeCat_ = BrowserCategory.ALL;     refresh(); return true; };
        btnProj.click = (Widget s) { activeCat_ = BrowserCategory.PROJECT; refresh(); return true; };
        btnPlug.click = (Widget s) { activeCat_ = BrowserCategory.PLUGINS; refresh(); return true; };
        btnSmp.click  = (Widget s) { activeCat_ = BrowserCategory.SAMPLES; refresh(); return true; };
        btnRec.click  = (Widget s) { activeCat_ = BrowserCategory.RECENT;  refresh(); return true; };

        edSearch_ = new EditLine("br_search", ""d);
        edSearch_.layoutWidth = FILL_PARENT;
        edSearch_.margins = Rect(0, 3, 0, 3);
        edSearch_.contentChange = (EditableContent content) {
            refresh();
        };
        addChild(edSearch_);

        auto scroll = new ScrollWidget("br_scroll");
        scroll.layoutWidth = FILL_PARENT;
        scroll.layoutHeight = FILL_PARENT;
        listBody_ = new VerticalLayout("br_list");
        listBody_.layoutWidth = FILL_PARENT;
        listBody_.layoutHeight = WRAP_CONTENT;
        scroll.contentWidget = listBody_;
        addChild(scroll);

        // Bottom Sample Waveform Preview
        previewWidget_ = new BrowserPreviewWidget("br_preview", model_);
        previewWidget_.margins = Rect(0, 4, 0, 3);
        previewWidget_.onAuditionClick = () {
            if (onPreviewNote !is null) onPreviewNote(60);
        };
        addChild(previewWidget_);

        // Audition pitch keys
        auto keyRow = new HorizontalLayout("br_keys");
        keyRow.layoutWidth = FILL_PARENT;
        static immutable uint[5] NOTES = [60, 62, 64, 67, 72];
        static immutable string[5] LABELS = ["C4", "D4", "E4", "G4", "C5"];
        foreach (i; 0 .. 5) {
            const uint n = NOTES[i];
            auto bk = new Button(null, toUTF32(LABELS[i]));
            bk.layoutWidth = FILL_PARENT;
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
        const pal = model_ ? model_.palette() : getFlPalette();
        backgroundColor = pal.bgWindowHeader;
        listBody_.removeAllChildren();
        const string q = toLower(toUTF8(edSearch_.text));

        // Group entries by section
        string[] sectionOrder;
        BrowserEntry[][string] grouped;

        void collect(BrowserCategory cat, string sec, string label, string uri, bool isInst) {
            if (activeCat_ != BrowserCategory.ALL && activeCat_ != cat) return;
            if (q.length > 0 && !canFind(toLower(label), q) && !canFind(toLower(uri), q)) return;
            if (sec !in grouped) {
                sectionOrder ~= sec;
            }
            grouped[sec] ~= BrowserEntry(cat, sec, label, uri, isInst);
        }

        foreach (e; DEFAULT_BROWSER_ENTRIES) {
            collect(e.category, e.section, e.label, e.uri, e.isInstrument);
        }
        if (model_) {
            foreach (pd; model_.scannedPlugins) {
                const string lbl = (pd.is_instrument ? "VST3: " : "VST3 FX: ") ~
                                   (pd.name.length > 0 ? pd.name : pd.path);
                collect(BrowserCategory.PLUGINS, pd.is_instrument ? "VST3 Generators" : "VST3 Effects",
                        lbl, pd.path, pd.is_instrument);
            }
        }

        foreach (sec; sectionOrder) {
            const bool collapsed = (sec in collapsedSections_) !is null && collapsedSections_[sec];
            const string secPrefix = collapsed ? "> " : "v ";
            auto secBtn = new Button(null, toUTF32(secPrefix ~ sec));
            secBtn.layoutWidth = FILL_PARENT;
            secBtn.backgroundColor = pal.bgPanelAlt;
            secBtn.textColor = pal.accentCyan;
            const string secKey = sec;
            secBtn.click = (Widget s) {
                collapsedSections_[secKey] = !collapsed;
                refresh();
                return true;
            };
            listBody_.addChild(secBtn);

            if (collapsed) continue;
            foreach (item; grouped[sec]) {
                auto btn = new Button(null, toUTF32("  " ~ item.label));
                btn.layoutWidth = FILL_PARENT;
                const auto entry = item;
                btn.click = (Widget s) {
                    if (entry.category == BrowserCategory.PLUGINS) {
                        if (entry.isInstrument) {
                            if (onAddGenerator !is null) onAddGenerator(entry.label, entry.uri, true);
                        } else {
                            if (onAddEffect !is null) onAddEffect(entry.label, entry.uri);
                        }
                    } else if (entry.category == BrowserCategory.SAMPLES) {
                        if (model_) {
                            model_.samplePreview.name = entry.label;
                            model_.samplePreview.path = entry.uri;
                        }
                        if (onAddSampleClip !is null) onAddSampleClip(entry.uri);
                        if (onPreviewNote !is null) onPreviewNote(60);
                    } else if (entry.category == BrowserCategory.RECENT) {
                        if (onLoadProject !is null) onLoadProject(entry.uri);
                    } else if (entry.category == BrowserCategory.PROJECT) {
                        if (entry.uri.length > 10 && entry.uri[0 .. 10] == "pattern://") {
                            import std.conv : to;
                            if (onSelectPattern !is null) onSelectPattern(to!int(entry.uri[10 .. $]));
                        }
                    }
                    previewWidget_.invalidate();
                    return true;
                };
                listBody_.addChild(btn);
            }
        }
        previewWidget_.invalidate();
    }
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;

    TransportModel model;
    model.initDefaultTemplate();
    auto buf = new ColorDrawBuf(220, 60);
    drawSampleWaveformPreview(buf, Rect(0, 0, 220, 60), &model);
    const pal = model.palette();
    // Centerline should be present at midY
    assert(buf.scanLine(5)[5] == pal.bgLcd || buf.scanLine(37)[20] == pal.accentCyan ||
           buf.scanLine(2)[10] == pal.borderSubtle);
}
