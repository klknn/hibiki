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
import gui_d.theme : activeDawUiFontFace, dp, drawCard, drawDetailedWaveform, drawThemeCard, drawVCenteredText,
    FlCompactButton, FlPalette, getDawFont, getFlPalette, safeWidgetFont, ScopedRetroAntialias, truncateLabel;
import gui_d.transport_model : BrowserCategory, generateSampleWaveform, PatternState, PlaylistClipKind,
    TransportModel;
import std.algorithm : canFind, sort;
import std.conv : to;
import std.file : dirEntries, exists, isDir, read, SpanMode;
import std.format : format;
import std.path : baseName, buildPath, extension;
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
    // Built-in Generators
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: 3x Osc", "builtin://3xosc", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: Acid Bass", "builtin://acid_bass", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: E-Piano", "builtin://epiano", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: Organ", "builtin://organ", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: Drum Machine", "builtin://drum_machine", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: DR-8 Kick", "builtin://dr8_kick", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: DR-8 Snare", "builtin://dr8_snare", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: DR-8 Hat", "builtin://dr8_hat", true),
    BrowserEntry(BrowserCategory.PLUGINS, "Generators", "Gen: Sampler", "builtin://sampler", true),
    // Built-in Effects
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: Parametric EQ", "builtin://eq", false),
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: Compressor", "builtin://compressor", false),
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: Ping-Pong Delay", "builtin://delay", false),
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: Stereo Reverb", "builtin://reverb", false),
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: Chorus", "builtin://chorus", false),
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: Bitcrusher", "builtin://bitcrusher", false),
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: Fruity Limiter", "builtin://limiter", false),
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: HOTT Multiband", "builtin://hott", false),
    BrowserEntry(BrowserCategory.PLUGINS, "Effects", "FX: Phaser", "builtin://phaser", false),
];

private uint readLE32(const(ubyte)[] b, size_t off) pure nothrow @safe {
    return cast(uint)b[off] | (cast(uint)b[off + 1] << 8) |
           (cast(uint)b[off + 2] << 16) | (cast(uint)b[off + 3] << 24);
}

private ushort readLE16(const(ubyte)[] b, size_t off) pure nothrow @safe {
    return cast(ushort)(b[off] | (b[off + 1] << 8));
}

/// Decodes real PCM/float `.wav` amplitude peaks (`[-1..1]` bipolar alternating envelope) for Browser/Clip preview.
float[] loadWavPreviewPeaks(string path, size_t numBins = 256, int* outSampleRate = null, int* outBits = null) @safe {
    if (numBins == 0 || path.length == 0) return null;
    try {
        if (!exists(path) || isDir(path)) return null;
        auto raw = cast(const(ubyte)[])read(path);
        if (raw.length < 44) return null;
        if (raw[0 .. 4] != cast(const(ubyte)[])"RIFF" || raw[8 .. 12] != cast(const(ubyte)[])"WAVE") {
            return null;
        }
        ushort audioFmt = 1;
        ushort channels = 1;
        uint sampleRate = 44100;
        ushort bitsPerSample = 16;
        size_t dataStart = 0;
        size_t dataLen = 0;

        size_t pos = 12;
        while (pos + 8 <= raw.length) {
            const cId = raw[pos .. pos + 4];
            const uint cSize = readLE32(raw, pos + 4);
            pos += 8;
            if (pos + cSize > raw.length) break;
            if (cId == cast(const(ubyte)[])"fmt " && cSize >= 16) {
                audioFmt = readLE16(raw, pos);
                channels = readLE16(raw, pos + 2);
                sampleRate = readLE32(raw, pos + 4);
                bitsPerSample = readLE16(raw, pos + 14);
            } else if (cId == cast(const(ubyte)[])"data") {
                dataStart = pos;
                dataLen = cSize;
                break;
            }
            pos += cSize + (cSize & 1u);
        }
        if (dataStart == 0 || dataLen == 0 || channels == 0 ||
            (bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32)) {
            return null;
        }
        if (outSampleRate !is null) *outSampleRate = cast(int)sampleRate;
        if (outBits !is null) *outBits = cast(int)bitsPerSample;

        const size_t bytesPerSample = bitsPerSample / 8;
        const size_t frameStride = bytesPerSample * channels;
        if (frameStride == 0) return null;
        const size_t totalFrames = dataLen / frameStride;
        if (totalFrames == 0) return null;

        auto peaks = new float[numBins];
        foreach (bi; 0 .. numBins) {
            const size_t f0 = bi * totalFrames / numBins;
            size_t f1 = (bi + 1) * totalFrames / numBins;
            if (f1 <= f0) f1 = f0 + 1;
            if (f1 > totalFrames) f1 = totalFrames;
            const size_t step = (f1 - f0 > 128) ? ((f1 - f0) / 128) : 1;
            float bestPos = 0.0f;
            float bestNeg = 0.0f;
            for (size_t f = f0; f < f1; f += step) {
                const size_t off = dataStart + f * frameStride;
                if (off + bytesPerSample > raw.length) break;
                float s = 0.0f;
                if (bitsPerSample == 16) {
                    short v = cast(short)readLE16(raw, off);
                    s = cast(float)v / 32768.0f;
                } else if (bitsPerSample == 24) {
                    int v = cast(int)raw[off] | (cast(int)raw[off + 1] << 8) | (cast(int)raw[off + 2] << 16);
                    if ((v & 0x800000) != 0) v |= 0xFF000000;
                    s = cast(float)v / 8388608.0f;
                } else if (bitsPerSample == 32 && audioFmt == 3) {
                    uint bits = readLE32(raw, off);
                    s = *(() @trusted => cast(float*)&bits)();
                } else if (bitsPerSample == 32) {
                    int v = cast(int)readLE32(raw, off);
                    s = cast(float)v / 2147483648.0f;
                }
                if (s > bestPos) bestPos = s;
                if (-s > bestNeg) bestNeg = -s;
            }
            peaks[bi] = (bi % 2 == 0) ? bestPos : -bestNeg;
        }
        return peaks;
    } catch (Exception) {
        return null;
    }
}

/// Scans a filesystem directory (recursing 1 level into non-bundle subdirs) for `.wav`, `.hbk`, and `.vst3`/`.clap`.
BrowserEntry[] scanBrowserDirectory(string dirPath) @trusted {
    BrowserEntry[] outEntries;
    if (dirPath.length == 0) return outEntries;
    try {
        if (!exists(dirPath) || !isDir(dirPath)) return outEntries;
        const string dirLbl = baseName(dirPath);
        void visitDir(string cur, string subSection, int depth) {
            foreach (entry; dirEntries(cur, SpanMode.shallow)) {
                const string full = entry.name;
                const string bn = baseName(full);
                if (bn.length == 0 || bn[0] == '.') continue;
                const string ext = toLower(extension(bn));
                if (ext == ".vst3" || ext == ".clap") {
                    outEntries ~= BrowserEntry(
                        BrowserCategory.PLUGINS, "Local Bundles (" ~ dirLbl ~ ")",
                        "VST3: " ~ bn, full, true);
                    continue; // Do not recurse inside plugin bundles
                }
                if (entry.isDir) {
                    if (depth < 1) visitDir(full, dirLbl ~ " / " ~ bn, depth + 1);
                    continue;
                }
                if (ext == ".wav" || ext == ".aif" || ext == ".aiff") {
                    outEntries ~= BrowserEntry(
                        BrowserCategory.SAMPLES, subSection,
                        "Wav: " ~ bn, full, false);
                } else if (ext == ".hbk") {
                    outEntries ~= BrowserEntry(
                        BrowserCategory.RECENT, "Projects (" ~ dirLbl ~ ")",
                        "Proj: " ~ bn, full, false);
                }
            }
        }
        visitDir(dirPath, "Folder: " ~ dirLbl, 0);
        outEntries.sort!((a, b) => a.label < b.label);
    } catch (Exception) {
    }
    return outEntries;
}

/// Renders the bottom FL Studio Browser sample waveform preview box.
void drawSampleWaveformPreview(
    DrawBuf buf,
    Rect rc,
    const(TransportModel)* model,
    FontRef fnt = FontRef.init
) {
    const pal = model ? model.palette() : getFlPalette();
    auto aaGuard = ScopedRetroAntialias(pal.retroBevel);
    drawThemeCard(buf, rc, pal.bgLcd, pal.borderSubtle, pal, 3.0f, true);
    if (rc.width < dp(24) || rc.height < dp(20)) return;

    const int padX = dp(5);
    const int hdrH = dp(16);
    const int wfTop = rc.top + hdrH + dp(2);
    const int wfBot = rc.bottom - dp(4);
    const int midY = (wfTop + wfBot) / 2;
    const int halfH = (wfBot - wfTop) / 2;

    if (model && model.samplePreview.waveform.length > 0 && halfH >= dp(3)) {
        drawDetailedWaveform(buf, Rect(rc.left + padX, wfTop, rc.right - padX, wfBot),
                             model.samplePreview.waveform, pal.accentCyan,
                             (pal.accentCyan & 0x00FFFFFF) | 0x8C000000, pal.borderSubtle);
    } else {
        buf.fillRect(Rect(rc.left + dp(4), midY, rc.right - dp(4), midY + dp(1)), pal.borderSubtle);
    }

    if (model) {
        auto labelFnt = getDawFont(10, fnt, FontWeight.Normal);
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
    private TextWidget hdr_;
    private FlCompactButton btnScan_;
    private FlCompactButton btnDir_;
    private FlCompactButton[5] keyBtns_;
    private bool[string] collapsedSections_;
    private Button[5] tabBtns_;

    void delegate(string label, string uri, bool isInstrument) onAddGenerator;
    void delegate(string label, string uri) onAddEffect;
    void delegate(string path) onAddSampleClip;
    void delegate(string path) onAuditionSample;
    void delegate() onAddSampleDir;
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
        backgroundColor = pal.retroBevel ? pal.bgPanel : pal.bgWindowHeader;
        padding = Rect(dp(5), dp(5), dp(5), dp(5));

        auto hdrRow = new HorizontalLayout("br_hdr_row");
        hdrRow.layoutWidth = FILL_PARENT;
        hdrRow.margins = Rect(0, 0, 0, dp(3));
        hdr_ = new TextWidget("br_hdr", "BROWSER"d);
        hdr_.textColor = pal.accentOrange;
        hdr_.fontSize = dp(11);
        hdr_.fontWeight = FontWeight.Normal;
        const string hdrFace = activeDawUiFontFace();
        if (hdrFace.length > 0) hdr_.fontFace = hdrFace;
        hdr_.padding = Rect(dp(4), dp(2), dp(4), dp(2));
        hdr_.layoutWidth = FILL_PARENT;
        btnDir_ = new FlCompactButton("br_add_dir", "+DIR"d, pal, 10, 20, true, false);
        btnDir_.focusable = false;
        btnDir_.margins = Rect(0, 0, dp(2), 0);
        btnDir_.click = (Widget s) {
            if (onAddSampleDir !is null) onAddSampleDir();
            return true;
        };
        btnScan_ = new FlCompactButton("br_scan", "SCAN"d, pal, 10, 20, true, false);
        btnScan_.focusable = false;
        btnScan_.click = (Widget s) {
            if (onScanVst3 !is null) {
                onScanVst3("testdata");
                onScanVst3("/Library/Audio/Plug-Ins/VST3");
            }
            refresh();
            return true;
        };
        hdrRow.addChild(hdr_);
        hdrRow.addChild(btnDir_);
        hdrRow.addChild(btnScan_);
        addChild(hdrRow);

        // FL Studio 2026 Browser Tabs: ALL | PROJ | PLUG | SMP | REC
        auto tabs = new HorizontalLayout("br_tabs");
        tabs.layoutWidth = FILL_PARENT;
        auto btnAll = new FlCompactButton("br_all", "ALL"d, pal, 10, 22, true, false);
        auto btnProj = new FlCompactButton("br_proj", "PROJ"d, pal, 10, 22, true, false);
        auto btnPlug = new FlCompactButton("br_plug", "PLUG"d, pal, 10, 22, true, false);
        auto btnSmp = new FlCompactButton("br_smp", "SMP"d, pal, 10, 22, true, false);
        auto btnRec = new FlCompactButton("br_rec", "REC"d, pal, 10, 22, true, false);
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
        edSearch_.backgroundImageId = null;
        edSearch_.backgroundColor = pal.bgLcd;
        edSearch_.textColor = pal.fgText;
        edSearch_.padding = Rect(dp(6), dp(3), dp(6), dp(3));
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
            if (model_ && model_.samplePreview.path.length > 0 && onAuditionSample !is null) {
                onAuditionSample(model_.samplePreview.path);
            } else if (onPreviewNote !is null) {
                onPreviewNote(60);
            }
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
        backgroundColor = pal.retroBevel ? pal.bgPanel : pal.bgWindowHeader;
        if (hdr_ !is null) {
            hdr_.textColor = pal.accentOrange;
        }
        if (btnDir_ !is null) {
            btnDir_.updatePalette(pal);
            btnDir_.customFillColor = pal.bgPanelAlt;
            btnDir_.customTextColor = pal.fgText;
        }
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

        if (edSearch_ !is null) {
            edSearch_.backgroundImageId = null;
            edSearch_.backgroundColor = pal.bgLcd;
            edSearch_.textColor = pal.fgText;
            const string fface = activeDawUiFontFace();
            if (fface.length > 0) edSearch_.fontFace = fface;
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
            bool[string] seenUris;
            foreach (dir; model_.sampleScanDirs) {
                foreach (se; scanBrowserDirectory(dir)) {
                    if ((se.uri in seenUris) !is null) continue;
                    seenUris[se.uri] = true;
                    collect(se.category, se.section, se.label, se.uri, se.isInstrument);
                }
            }
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
            auto secBtn = new FlCompactButton(null, toUTF32(secPrefix ~ sec), pal, 11, 20, false, false);
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
                            model_.activePickerKind = PlaylistClipKind.AUDIO;
                            int sr = 44100;
                            int bits = 16;
                            auto peaks = loadWavPreviewPeaks(entry.uri, 512, &sr, &bits);
                            if (peaks.length > 0) {
                                model_.samplePreview.waveform = peaks;
                                model_.samplePreview.info = format("%d Hz | %d-bit", sr, bits);
                            } else {
                                uint seed = 17;
                                foreach (ch; entry.uri) seed = seed * 31u + cast(uint)ch;
                                model_.samplePreview.waveform =
                                    generateSampleWaveform(512, seed, canFind(toLower(entry.uri), "loop"));
                            }
                            model_.hintText = format("Previewing '%s' (double-click to add to Playlist)",
                                                     baseName(entry.uri));
                        }
                        if (onAuditionSample !is null) onAuditionSample(entry.uri);
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
                if (entry.category == BrowserCategory.SAMPLES) {
                    btn.mouseEvent = delegate(Widget src, MouseEvent ev) {
                        if (ev.action == MouseAction.ButtonDown &&
                            ((ev.button == MouseButton.Left && ev.doubleClick) ||
                             ev.button == MouseButton.Right)) {
                            if (onAddSampleClip !is null) onAddSampleClip(entry.uri);
                            return true;
                        }
                        return false;
                    };
                }
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

    // Phase 5: No fake non-existent WAV paths in DEFAULT_BROWSER_ENTRIES & real directory scan + WAV RIFF decoder
    foreach (de; DEFAULT_BROWSER_ENTRIES) {
        assert(!canFind(de.uri, "kick_punch.wav") && !canFind(de.uri, "snare_crisp.wav"),
               "DEFAULT_BROWSER_ENTRIES must not contain hardcoded non-existent WAV files");
    }

    import std.file : mkdirRecurse, rmdirRecurse, tempDir, write;
    const string scanDir = buildPath(tempDir(), "hibiki_browser_scan_test");
    if (exists(scanDir)) rmdirRecurse(scanDir);
    mkdirRecurse(scanDir);
    scope (exit) { if (exists(scanDir)) rmdirRecurse(scanDir); }
    mkdirRecurse(buildPath(scanDir, "Synth.vst3", "Contents"));
    write(buildPath(scanDir, "Synth.vst3", "Contents", "dummy.wav"), cast(const(ubyte)[])[1, 2, 3]);
    write(buildPath(scanDir, "demo.hbk"), cast(const(ubyte)[])"Hbk");

    // Write valid 16-bit mono PCM WAV (8 samples of +/-16384 = 0.5 amplitude)
    ubyte[44 + 16] wavBytes = 0;
    wavBytes[0 .. 4] = cast(const(ubyte)[])"RIFF";
    wavBytes[4 .. 8] = [52, 0, 0, 0];
    wavBytes[8 .. 12] = cast(const(ubyte)[])"WAVE";
    wavBytes[12 .. 16] = cast(const(ubyte)[])"fmt ";
    wavBytes[16] = 16;
    wavBytes[20] = 1; // PCM
    wavBytes[22] = 1; // mono
    wavBytes[24 .. 28] = [0x44, 0xAC, 0, 0]; // 44100 Hz
    wavBytes[34] = 16; // 16-bit
    wavBytes[36 .. 40] = cast(const(ubyte)[])"data";
    wavBytes[40] = 16; // 16 bytes
    foreach (si; 0 .. 8) {
        short s = (si % 2 == 0) ? 16384 : -16384;
        wavBytes[44 + si * 2] = cast(ubyte)(s & 0xFF);
        wavBytes[44 + si * 2 + 1] = cast(ubyte)((s >> 8) & 0xFF);
    }
    const string wavPath = buildPath(scanDir, "tone.wav");
    write(wavPath, wavBytes);

    auto scanned = scanBrowserDirectory(scanDir);
    assert(scanned.length == 3, "Expected .wav, .hbk, and .vst3 (without recursing inside .vst3/Contents)");
    int decSr = 0;
    int decBits = 0;
    auto peaks = loadWavPreviewPeaks(wavPath, 4, &decSr, &decBits);
    assert(peaks.length == 4 && decSr == 44100 && decBits == 16);
    assert(peaks[0] > 0.49f && peaks[1] < -0.49f);

    // Regression: single-click sample auditions without adding clip; double-click adds clip
    int auditionCount = 0;
    int addedClipCount = 0;
    bp.onAuditionSample = (string p) { ++auditionCount; };
    bp.onAddSampleClip = (string p) { ++addedClipCount; };
    model.sampleScanDirs = [scanDir];
    bp.activeCat_ = BrowserCategory.SAMPLES;
    bp.refresh();
    Widget sampleBtn;
    foreach (li; 0 .. bp.listBody_.childCount) {
        auto w = bp.listBody_.child(li);
        if (w.click.assigned && canFind(toUTF8(w.text), "tone.wav")) {
            sampleBtn = w;
            break;
        }
    }
    assert(sampleBtn !is null, "Expected SAMPLES listBody_ to contain scanned tone.wav");
    sampleBtn.click(sampleBtn);
    assert(auditionCount == 1 && addedClipCount == 0,
           "Expected single-click on sample to only audition, not add clip");
    auto dbl = new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, 5, 5);
    dbl.lbutton.down(5, 5, MouseFlag.LButton);
    dbl.lbutton.up(5, 5, 0);
    dbl.lbutton.down(5, 5, MouseFlag.LButton);
    sampleBtn.onMouseEvent(dbl);
    assert(addedClipCount == 1,
           "Expected double-click on sample to add clip to Playlist");
}
