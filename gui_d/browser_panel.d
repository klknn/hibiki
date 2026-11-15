module gui_d.browser_panel;

import dlangui;
import dlangui.graphics.colors : COLOR_TRANSPARENT;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.graphics.fonts : FontWeight;
import dlangui.widgets.controls : Button, TextWidget;
import dlangui.widgets.editors : EditLine;
import dlangui.widgets.layouts : HorizontalLayout, VerticalLayout;
import dlangui.widgets.scroll : ScrollWidget;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.theme : dp, drawCard, drawVCenteredText, FlCompactButton, FlPalette,
    getDawFont, getFlPalette, safeWidgetFont, truncateLabel;
import gui_d.transport_model : BrowserCategory, PatternState, TransportModel;
import std.algorithm : canFind;
import std.conv : to;
import std.format : format;
import std.path : baseName;
import std.string : startsWith, toLower;
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
    if (rc.width < dp(24) || rc.height < dp(20)) return;

    const int padX = dp(5);
    const int hdrH = dp(16);
    const int wfTop = rc.top + hdrH + dp(2);
    const int wfBot = rc.bottom - dp(4);
    const int midY = (wfTop + wfBot) / 2;
    const int halfH = (wfBot - wfTop) / 2;

    buf.fillRect(Rect(rc.left + dp(4), midY, rc.right - dp(4), midY + dp(1)), pal.borderSubtle);

    if (model && model.samplePreview.waveform.length > 0 && halfH >= dp(3)) {
        const w = rc.width - padX * 2;
        const wf = model.samplePreview.waveform;
        foreach (px; 0 .. w) {
            const size_t bucket = (cast(size_t)px * wf.length) / cast(size_t)w;
            float amp = wf[bucket];
            if (amp < 0.0f) amp = -amp;
            if (amp > 1.0f) amp = 1.0f;
            int barH = cast(int)(amp * halfH);
            if (barH < 1) barH = 1;
            buf.fillRect(Rect(rc.left + padX + px, midY - barH, rc.left + padX + px + 1, midY + barH),
                         pal.accentCyan);
        }
    }

    if (model) {
        auto labelFnt = getDawFont(10, fnt, FontWeight.Bold);
        drawVCenteredText(buf, labelFnt, rc.left + dp(6),
                          Rect(rc.left, rc.top + dp(1), rc.right, rc.top + hdrH),
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
        layoutHeight = dp(54);
        minHeight = dp(54);
    }

    override void measure(int parentWidth, int parentHeight) {
        const int h = dp(54);
        minHeight = h;
        layoutHeight = h;
        measuredContent(parentWidth, parentHeight, parentWidth, h);
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        drawSampleWaveformPreview(buf, _pos, model_, safeWidgetFont(this));
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
    private FlCompactButton btnScan_;
    private FlCompactButton[5] keyBtns_;
    private bool[string] collapsedSections_;
    private Button[5] tabBtns_;

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
        layoutWidth = dp(248);
        minWidth = dp(220);
        maxWidth = dp(290);
        layoutHeight = FILL_PARENT;
        const pal = model_ ? model_.palette() : getFlPalette();
        backgroundColor = pal.bgWindowHeader;
        padding = Rect(dp(5), dp(5), dp(5), dp(5));

        auto hdrRow = new HorizontalLayout("br_hdr_row");
        hdrRow.layoutWidth = FILL_PARENT;
        hdrRow.margins = Rect(0, 0, 0, dp(3));
        auto hdr = new TextWidget("br_hdr", "BROWSER"d);
        hdr.textColor = pal.accentOrange;
        hdr.fontSize = dp(11);
        hdr.padding = Rect(dp(4), dp(2), dp(4), dp(2));
        hdr.layoutWidth = FILL_PARENT;
        btnScan_ = new FlCompactButton("br_scan", "SCAN VST3"d, pal, 10, 20, true, true);
        btnScan_.click = (Widget s) {
            if (onScanVst3 !is null) {
                onScanVst3("testdata");
                onScanVst3("/Library/Audio/Plug-Ins/VST3");
            }
            return true;
        };
        hdrRow.addChild(hdr);
        hdrRow.addChild(btnScan_);
        addChild(hdrRow);

        // FL Studio 2026 Browser Tabs: ALL | PROJ | PLUG | SMP | REC
        auto tabs = new HorizontalLayout("br_tabs");
        tabs.layoutWidth = FILL_PARENT;
        auto btnAll = new FlCompactButton("br_all", "ALL"d, pal, 10, 22, true, true);
        auto btnProj = new FlCompactButton("br_proj", "PROJ"d, pal, 10, 22, true, true);
        auto btnPlug = new FlCompactButton("br_plug", "PLUG"d, pal, 10, 22, true, true);
        auto btnSmp = new FlCompactButton("br_smp", "SMP"d, pal, 10, 22, true, true);
        auto btnRec = new FlCompactButton("br_rec", "REC"d, pal, 10, 22, true, true);
        tabBtns_ = [btnAll, btnProj, btnPlug, btnSmp, btnRec];
        foreach (b; tabBtns_) {
            b.layoutWidth = FILL_PARENT;
            b.focusable = false;
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
        edSearch_.fontSize = dp(11);
        edSearch_.minHeight = dp(22);
        edSearch_.margins = Rect(0, dp(3), 0, dp(3));
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
        previewWidget_.margins = Rect(0, dp(4), 0, dp(3));
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
            auto bk = new FlCompactButton(null, toUTF32(LABELS[i]), pal, 10, 22, true, false);
            bk.layoutWidth = FILL_PARENT;
            bk.focusable = false;
            bk.click = (Widget s) {
                if (onPreviewNote !is null) onPreviewNote(n);
                return true;
            };
            keyBtns_[i] = bk;
            keyRow.addChild(bk);
        }
        addChild(keyRow);

        refresh();
    }

    override void measure(int parentWidth, int parentHeight) {
        layoutWidth = dp(248);
        minWidth = dp(220);
        maxWidth = dp(290);
        padding = Rect(dp(5), dp(5), dp(5), dp(5));
        edSearch_.fontSize = dp(11);
        edSearch_.minHeight = dp(22);
        super.measure(parentWidth, parentHeight);
    }

    void refresh() {
        const pal = model_ ? model_.palette() : getFlPalette();
        backgroundColor = pal.bgWindowHeader;
        if (btnScan_ !is null) {
            btnScan_.updatePalette(pal);
            btnScan_.customFillColor = pal.bgPanelAlt;
            btnScan_.customTextColor = pal.accentCyan;
        }
        foreach (bk; keyBtns_) {
            if (bk !is null) bk.updatePalette(pal);
        }

        static immutable BrowserCategory[5] CATS = [
            BrowserCategory.ALL,
            BrowserCategory.PROJECT,
            BrowserCategory.PLUGINS,
            BrowserCategory.SAMPLES,
            BrowserCategory.RECENT,
        ];
        foreach (i; 0 .. 5) {
            if (tabBtns_[i] is null) continue;
            const bool active = activeCat_ == CATS[i];
            if (auto fb = cast(FlCompactButton)tabBtns_[i]) {
                fb.updatePalette(pal);
                fb.customFillColor = active ? pal.accentOrange : pal.bgControl;
                fb.customTextColor = active ? pal.bgLcd : pal.fgText;
            } else {
                tabBtns_[i].backgroundColor = active ? pal.accentOrange : pal.bgControl;
                tabBtns_[i].textColor = active ? pal.bgLcd : pal.fgText;
            }
        }

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

        const bool hasDynamicPatterns = model_ !is null && model_.patterns.length > 0;
        if (hasDynamicPatterns) {
            foreach (pat; model_.patterns) {
                collect(BrowserCategory.PROJECT, "Current Project",
                        "Pat: " ~ pat.name, format("pattern://%d", pat.index), false);
            }
        }

        foreach (e; DEFAULT_BROWSER_ENTRIES) {
            if (hasDynamicPatterns && e.uri.startsWith("pattern://")) continue;
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
            auto secBtn = new FlCompactButton(null, toUTF32(secPrefix ~ sec), pal, 10, 20, false, true);
            secBtn.layoutWidth = FILL_PARENT;
            secBtn.focusable = false;
            secBtn.customFillColor = pal.bgPanelAlt;
            secBtn.customTextColor = pal.accentCyan;
            const string secKey = sec;
            secBtn.click = (Widget s) {
                collapsedSections_[secKey] = !collapsed;
                refresh();
                return true;
            };
            listBody_.addChild(secBtn);

            if (collapsed) continue;
            foreach (item; grouped[sec]) {
                auto btn = new FlCompactButton(null, toUTF32("  " ~ item.label), pal, 11, 20, false, false);
                btn.layoutWidth = FILL_PARENT;
                btn.focusable = false;
                btn.customFillColor = pal.bgPanel;
                btn.customBorderColor = COLOR_TRANSPARENT;
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
                            model_.samplePreview.name = baseName(entry.uri);
                            model_.samplePreview.path = entry.uri;
                        }
                        if (onAddSampleClip !is null) onAddSampleClip(entry.uri);
                        if (onPreviewNote !is null) onPreviewNote(60);
                    } else if (entry.category == BrowserCategory.RECENT) {
                        if (onLoadProject !is null) onLoadProject(entry.uri);
                    } else if (entry.category == BrowserCategory.PROJECT) {
                        if (entry.uri.length > 10 && entry.uri[0 .. 10] == "pattern://") {
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
    import dlangui.graphics.fonts : Font, FontFamily, FontManager, FontRef, Glyph;
    import dlangui.widgets.styles : createDefaultTheme, currentTheme;

    static class HeadlessTestFont : Font {
        Glyph _glyph;
        this() {
            _glyph.widthPixels = 7;
            _glyph.widthScaled = 7 << 6;
            _glyph.blackBoxX = 6;
            _glyph.blackBoxY = 10;
            _glyph.originY = 10;
            _glyph.glyph = new ubyte[](_glyph.blackBoxX * _glyph.blackBoxY);
            _glyph.glyph[] = 0xC0;
        }
        override @property int size() { return 12; }
        override @property int height() { return 14; }
        override @property int weight() { return 400; }
        override @property int baseline() { return 11; }
        override @property bool italic() { return false; }
        override @property string face() { return "headless"; }
        override @property FontFamily family() { return FontFamily.SansSerif; }
        override @property bool isNull() { return false; }
        override Glyph* getCharGlyph(dchar ch, bool withImage = true) { return &_glyph; }
        override void checkpoint() {}
        override void cleanup() {}
        override void clearGlyphCache() {}
    }

    static class HeadlessTestFontManager : FontManager {
        FontRef _font;
        this() { _font = FontRef(new HeadlessTestFont()); }
        override ref FontRef getFont(int size, int weight, bool italic, FontFamily family, string face) {
            return _font;
        }
        override void checkpoint() {}
        override void cleanup() {}
    }

    if (currentTheme is null) {
        currentTheme = createDefaultTheme();
    }
    import dlangui.graphics.resources : drawableCache, DrawableCache, embedStandardDlangUIResources,
        imageCache, ImageCache;
    const bool hadDrawableCache = drawableCache !is null;
    if (!hadDrawableCache) {
        embedStandardDlangUIResources();
        if (imageCache is null) imageCache = new ImageCache();
        drawableCache = new DrawableCache();
    }
    const bool hadFontMgr = FontManager.instance !is null;
    if (!hadFontMgr) {
        FontManager.instance = new HeadlessTestFontManager();
    }
    BrowserPanel bp;
    import gui_d.theme : destroyWidgetTree;
    scope (exit) {
        destroyWidgetTree(bp);
        bp = null;
        currentTheme = null;
        if (!hadDrawableCache) {
            drawableCache = null;
            imageCache = null;
        }
        if (!hadFontMgr) {
            FontManager.instance = null;
        }
    }

    TransportModel model;
    model.initDefaultTemplate();
    auto buf = new ColorDrawBuf(480, 320);
    drawSampleWaveformPreview(buf, Rect(0, 0, 220, 60), &model);
    const pal = model.palette();
    // Centerline and preview text should render safely via FontManager without crashing
    assert(buf.scanLine(5)[5] == pal.bgLcd || buf.scanLine(37)[20] == pal.accentCyan ||
           buf.scanLine(2)[10] == pal.borderSubtle);

    model.patterns ~= PatternState(3, "Pattern 4 (Break)", 0x00385770, 16);
    bp = new BrowserPanel("br_test", &model);
    foreach (b; bp.tabBtns_) {
        assert(!b.focusable);
    }
    bool foundDynPattern = false;
    foreach (i; 0 .. bp.listBody_.childCount) {
        auto w = bp.listBody_.child(i);
        assert(!w.focusable);
        if (canFind(toUTF8(w.text), "Pattern 4 (Break)")) {
            foundDynPattern = true;
        }
    }
    assert(foundDynPattern);

    // Reproduce 1x (96 DPI) and 2x (192 DPI) measure/layout/draw cycle
    import dlangui.core.types : overrideScreenDPI;
    scope (exit) overrideScreenDPI = 0;
    overrideScreenDPI = 96;
    bp.measure(248, 300);
    const int w1x = bp.measuredWidth;
    bp.layout(Rect(0, 0, 248, 300));
    bp.onDraw(buf);

    overrideScreenDPI = 192;
    bp.measure(496, 600);
    assert(bp.measuredWidth > w1x, "BrowserPanel width must scale cleanly at 192 DPI");
    bp.layout(Rect(0, 0, 480, 320));
    bp.onDraw(buf);
}
