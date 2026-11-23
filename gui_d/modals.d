module gui_d.modals;

import dlangui;
import dlangui.dialogs.dialog : Dialog, DialogFlag;
import dlangui.widgets.controls : TextWidget;
import dlangui.widgets.editors : EditLine;
import dlangui.widgets.layouts : HorizontalLayout, VerticalLayout;
import dlangui.widgets.widget : Widget;
import gui_d.theme : activeDawUiFontFace, dp, FlCompactButton, FlPalette, getFlPalette;
import gui_d.transport_model : TransportModel;
import std.conv : to;
import std.format : format;
import std.string : strip;
import std.utf : toUTF32, toUTF8;

/// FL Studio 2026 themed modal prompt dialog for File Open, Save As, Bounce WAV, Collect Files, or Add Folder.
class FlTextPromptDialog : Dialog {
    private TransportModel* model_;
    private string promptLabel_;
    private string initialValue_;
    private string[] quickChoices_;
    private EditLine edInput_;

    void delegate(string value) onConfirm;

    this(
        Window parentWin,
        TransportModel* model,
        string title,
        string promptLabel,
        string initialValue,
        string[] quickChoices = null,
        void delegate(string value) confirmCb = null
    ) {
        super(UIString.fromRaw(toUTF32(title)), parentWin, DialogFlag.Modal | DialogFlag.Popup, dp(420), dp(220));
        model_ = model;
        promptLabel_ = promptLabel;
        initialValue_ = initialValue;
        quickChoices_ = quickChoices;
        onConfirm = confirmCb;
        buildContents();
    }

    string currentText() const {
        return edInput_ ? strip(toUTF8(edInput_.text)) : initialValue_;
    }

    void confirm() {
        const string val = currentText();
        if (onConfirm !is null && val.length > 0) {
            onConfirm(val);
        }
        dismiss();
    }

    void dismiss() {
        if (_popup !is null && _parentWindow !is null) {
            close(null);
        } else if (window !is null) {
            close(null);
        }
    }

    private void buildContents() {
        const pal = model_ ? model_.palette() : getFlPalette();
        backgroundColor = pal.bgPanel;
        padding = Rect(dp(12), dp(10), dp(12), dp(10));
        minWidth = dp(400);

        const string face = activeDawUiFontFace();

        auto lbl = new TextWidget("prm_lbl", toUTF32(promptLabel_));
        lbl.textColor = pal.fgText;
        lbl.fontSize = dp(11);
        if (face.length > 0) lbl.fontFace = face;
        lbl.margins = Rect(0, 0, 0, dp(6));
        addChild(lbl);

        edInput_ = new EditLine("prm_edit", toUTF32(initialValue_));
        edInput_.layoutWidth = FILL_PARENT;
        edInput_.backgroundImageId = null;
        edInput_.backgroundColor = pal.bgLcd;
        edInput_.textColor = pal.accentCyan;
        edInput_.fontSize = dp(12);
        edInput_.padding = Rect(dp(8), dp(6), dp(8), dp(6));
        edInput_.margins = Rect(0, 0, 0, dp(8));
        addChild(edInput_);

        if (quickChoices_.length > 0) {
            auto qLbl = new TextWidget("prm_qlbl", "Quick Picks:"d);
            qLbl.textColor = pal.fgMuted;
            qLbl.fontSize = dp(10);
            if (face.length > 0) qLbl.fontFace = face;
            qLbl.margins = Rect(0, 0, 0, dp(4));
            addChild(qLbl);

            auto qList = new VerticalLayout("prm_qlist");
            qList.layoutWidth = FILL_PARENT;
            qList.margins = Rect(0, 0, 0, dp(8));
            FlCompactButton makeChoiceBtn(string cVal) {
                auto qb = new FlCompactButton(null, toUTF32(cVal), pal, 10, 22, false, false);
                qb.layoutWidth = FILL_PARENT;
                qb.customFillColor = pal.bgPanelAlt;
                qb.customTextColor = pal.fgText;
                qb.margins = Rect(0, 0, 0, dp(2));
                qb.click = (Widget s) {
                    if (edInput_ !is null) edInput_.text = toUTF32(cVal);
                    return true;
                };
                return qb;
            }
            foreach (choice; quickChoices_) {
                qList.addChild(makeChoiceBtn(choice));
            }
            addChild(qList);
        }

        auto btnRow = new HorizontalLayout("prm_btns");
        btnRow.layoutWidth = FILL_PARENT;
        auto spacer = new Widget();
        spacer.layoutWidth = FILL_PARENT;
        auto btnCancel = new FlCompactButton("prm_cancel", "Cancel"d, pal, 11, 24, true, false);
        btnCancel.margins = Rect(0, 0, dp(6), 0);
        btnCancel.click = (Widget s) {
            dismiss();
            return true;
        };
        auto btnOk = new FlCompactButton("prm_ok", "OK"d, pal, 11, 24, true, false);
        btnOk.customFillColor = pal.accentOrange;
        btnOk.customTextColor = pal.bgLcd;
        btnOk.click = (Widget s) {
            confirm();
            return true;
        };
        btnRow.addChild(spacer);
        btnRow.addChild(btnCancel);
        btnRow.addChild(btnOk);
        addChild(btnRow);
    }
}

/// Interactive FL Studio 2026 Audio, Plugin Host, Recording & Scan Settings Modal.
class FlSettingsDialog : Dialog {
    private TransportModel* model_;
    int selectedLatencyMs = 10;
    bool selectedDoublePrecision = false;
    bool selectedSandbox = false;
    bool selectedOverdub = true;
    bool selectedCountdown = false;

    private FlCompactButton[4] latBtns_;
    private FlCompactButton[2] precBtns_;
    private FlCompactButton[2] hostBtns_;
    private FlCompactButton btnOverdub_;
    private FlCompactButton btnCountdown_;
    private TextWidget txtDevices_;
    private EditLine edScanPath_;

    void delegate(int latencyMs, bool useDouble, bool sandbox, bool overdub, bool countdown) onApplySettings;
    void delegate() onRefreshDevices;
    void delegate(string path) onScanVst3Path;
    void delegate(string dirPath) onAddSampleDir;

    this(Window parentWin, TransportModel* model) {
        super(UIString.fromRaw("Audio & Plugin Settings"d), parentWin, DialogFlag.Modal | DialogFlag.Popup, dp(480), dp(360));
        model_ = model;
        if (model_) {
            selectedLatencyMs = model_.bufferLatencyMs > 0 ? model_.bufferLatencyMs : 10;
            selectedDoublePrecision = model_.useDoublePrecision;
            selectedSandbox = model_.sandboxPlugins;
            selectedOverdub = model_.overdubEnabled;
            selectedCountdown = model_.countdownEnabled;
        }
        buildContents();
        syncToggleStyles();
    }

    void dismiss() {
        if ((_popup !is null && _parentWindow !is null) || window !is null) {
            close(null);
        }
    }

    void refreshDeviceSummary() {
        syncToggleStyles();
        invalidate();
    }

    private void syncToggleStyles() {
        const pal = model_ ? model_.palette() : getFlPalette();
        static immutable int[4] LATS = [5, 10, 20, 40];
        foreach (i, b; latBtns_) {
            if (b is null) continue;
            const bool active = selectedLatencyMs == LATS[i];
            b.updatePalette(pal);
            b.customFillColor = active ? pal.accentOrange : pal.bgControl;
            b.customTextColor = active ? pal.bgLcd : pal.fgText;
        }
        foreach (i, b; precBtns_) {
            if (b is null) continue;
            const bool active = (i == 1) ? selectedDoublePrecision : !selectedDoublePrecision;
            b.updatePalette(pal);
            b.customFillColor = active ? pal.accentCyan : pal.bgControl;
            b.customTextColor = active ? pal.bgLcd : pal.fgText;
        }
        foreach (i, b; hostBtns_) {
            if (b is null) continue;
            const bool active = (i == 1) ? selectedSandbox : !selectedSandbox;
            b.updatePalette(pal);
            b.customFillColor = active ? pal.accentOrange : pal.bgControl;
            b.customTextColor = active ? pal.bgLcd : pal.fgText;
        }
        if (btnOverdub_ !is null) {
            btnOverdub_.updatePalette(pal);
            btnOverdub_.customFillColor = selectedOverdub ? pal.accentGreen : pal.bgControl;
            btnOverdub_.customTextColor = selectedOverdub ? pal.bgLcd : pal.fgText;
        }
        if (btnCountdown_ !is null) {
            btnCountdown_.updatePalette(pal);
            btnCountdown_.customFillColor = selectedCountdown ? pal.accentOrange : pal.bgControl;
            btnCountdown_.customTextColor = selectedCountdown ? pal.bgLcd : pal.fgText;
        }
        if (txtDevices_ !is null && model_) {
            const size_t aN = model_.audioInputs.length;
            const size_t mN = model_.midiInputs.length;
            string firstA = aN > 0 ? model_.audioInputs[0].name : "Default Input";
            string firstM = mN > 0 ? model_.midiInputs[0].name : "Virtual QWERTY";
            txtDevices_.text = toUTF32(format("Audio In (%d): %s  |  MIDI In (%d): %s", aN, firstA, mN, firstM));
        }
    }

    private void buildContents() {
        const pal = model_ ? model_.palette() : getFlPalette();
        backgroundColor = pal.bgPanel;
        padding = Rect(dp(14), dp(12), dp(14), dp(12));
        minWidth = dp(460);
        const string face = activeDawUiFontFace();

        TextWidget mkLabel(string s) {
            auto t = new TextWidget(null, toUTF32(s));
            t.textColor = pal.fgText;
            t.fontSize = dp(11);
            if (face.length > 0) t.fontFace = face;
            t.margins = Rect(0, dp(4), 0, dp(3));
            return t;
        }

        // 1. Buffer Latency
        addChild(mkLabel("Audio Buffer Latency:"));
        auto latRow = new HorizontalLayout("set_lat_row");
        latRow.layoutWidth = FILL_PARENT;
        static immutable int[4] LATS = [5, 10, 20, 40];
        FlCompactButton makeLatBtn(int v) {
            auto b = new FlCompactButton(null, toUTF32(format("%d ms", v)), pal, 10, 22, true, false);
            b.layoutWidth = FILL_PARENT;
            b.margins = Rect(0, 0, dp(3), 0);
            b.click = (Widget s) {
                selectedLatencyMs = v;
                syncToggleStyles();
                return true;
            };
            return b;
        }
        foreach (i, ms; LATS) {
            auto b = makeLatBtn(ms);
            latBtns_[i] = b;
            latRow.addChild(b);
        }
        addChild(latRow);

        // 2. Processing Precision
        addChild(mkLabel("DSP Processing Precision:"));
        auto precRow = new HorizontalLayout("set_prec_row");
        precRow.layoutWidth = FILL_PARENT;
        static immutable string[2] PREC_L = ["32-bit Float", "64-bit Double"];
        FlCompactButton makePrecBtn(string lbl, bool dbl) {
            auto b = new FlCompactButton(null, toUTF32(lbl), pal, 10, 22, true, false);
            b.layoutWidth = FILL_PARENT;
            b.margins = Rect(0, 0, dp(3), 0);
            b.click = (Widget s) {
                selectedDoublePrecision = dbl;
                syncToggleStyles();
                return true;
            };
            return b;
        }
        foreach (i; 0 .. 2) {
            auto b = makePrecBtn(PREC_L[i], i == 1);
            precBtns_[i] = b;
            precRow.addChild(b);
        }
        addChild(precRow);

        // 3. VST3 Plugin Host Mode
        addChild(mkLabel("VST3 Plugin Hosting Mode:"));
        auto hostRow = new HorizontalLayout("set_host_row");
        hostRow.layoutWidth = FILL_PARENT;
        static immutable string[2] HOST_L = ["In-Process", "Local Sandbox"];
        FlCompactButton makeHostBtn(string lbl, bool sb) {
            auto b = new FlCompactButton(null, toUTF32(lbl), pal, 10, 22, true, false);
            b.layoutWidth = FILL_PARENT;
            b.margins = Rect(0, 0, dp(3), 0);
            b.click = (Widget s) {
                selectedSandbox = sb;
                syncToggleStyles();
                return true;
            };
            return b;
        }
        foreach (i; 0 .. 2) {
            auto b = makeHostBtn(HOST_L[i], i == 1);
            hostBtns_[i] = b;
            hostRow.addChild(b);
        }
        addChild(hostRow);

        // 4. Recording Behavior
        addChild(mkLabel("Recording Flow:"));
        auto recRow = new HorizontalLayout("set_rec_row");
        recRow.layoutWidth = FILL_PARENT;
        btnOverdub_ = new FlCompactButton("set_overdub", "Overdub MIDI"d, pal, 10, 22, true, false);
        btnOverdub_.layoutWidth = FILL_PARENT;
        btnOverdub_.margins = Rect(0, 0, dp(4), 0);
        btnOverdub_.click = (Widget s) {
            selectedOverdub = !selectedOverdub;
            syncToggleStyles();
            return true;
        };
        btnCountdown_ = new FlCompactButton("set_countdown", "3-2-1 Countdown"d, pal, 10, 22, true, false);
        btnCountdown_.layoutWidth = FILL_PARENT;
        btnCountdown_.click = (Widget s) {
            selectedCountdown = !selectedCountdown;
            syncToggleStyles();
            return true;
        };
        recRow.addChild(btnOverdub_);
        recRow.addChild(btnCountdown_);
        addChild(recRow);

        // 5. Audio/MIDI Devices + Scan Path
        addChild(mkLabel("Audio / MIDI Inputs & Sample/VST3 Folder:"));
        auto devRow = new HorizontalLayout("set_dev_row");
        devRow.layoutWidth = FILL_PARENT;
        txtDevices_ = new TextWidget("set_dev_txt", "Querying inputs..."d);
        txtDevices_.layoutWidth = FILL_PARENT;
        txtDevices_.textColor = pal.accentCyan;
        txtDevices_.fontSize = dp(10);
        if (face.length > 0) txtDevices_.fontFace = face;
        auto btnRef = new FlCompactButton("set_ref_dev", "Refresh"d, pal, 10, 22, true, false);
        btnRef.click = (Widget s) {
            if (onRefreshDevices !is null) onRefreshDevices();
            syncToggleStyles();
            return true;
        };
        devRow.addChild(txtDevices_);
        devRow.addChild(btnRef);
        addChild(devRow);

        auto scanRow = new HorizontalLayout("set_scan_row");
        scanRow.layoutWidth = FILL_PARENT;
        scanRow.margins = Rect(0, dp(4), 0, dp(8));
        edScanPath_ = new EditLine("set_scan_ed", toUTF32("testdata"));
        edScanPath_.layoutWidth = FILL_PARENT;
        edScanPath_.backgroundImageId = null;
        edScanPath_.backgroundColor = pal.bgLcd;
        edScanPath_.textColor = pal.fgText;
        edScanPath_.fontSize = dp(11);
        edScanPath_.padding = Rect(dp(6), dp(4), dp(6), dp(4));
        auto btnAddDir = new FlCompactButton("set_add_dir", "+Samples"d, pal, 10, 22, true, false);
        btnAddDir.margins = Rect(dp(4), 0, dp(3), 0);
        btnAddDir.click = (Widget s) {
            const p = strip(toUTF8(edScanPath_.text));
            if (p.length > 0 && onAddSampleDir !is null) onAddSampleDir(p);
            return true;
        };
        auto btnScanVst = new FlCompactButton("set_scan_vst", "Scan VST3"d, pal, 10, 22, true, false);
        btnScanVst.click = (Widget s) {
            const p = strip(toUTF8(edScanPath_.text));
            if (p.length > 0 && onScanVst3Path !is null) onScanVst3Path(p);
            return true;
        };
        scanRow.addChild(edScanPath_);
        scanRow.addChild(btnAddDir);
        scanRow.addChild(btnScanVst);
        addChild(scanRow);

        // 6. Apply / Close
        auto foot = new HorizontalLayout("set_foot");
        foot.layoutWidth = FILL_PARENT;
        auto sp = new Widget();
        sp.layoutWidth = FILL_PARENT;
        auto btnClose = new FlCompactButton("set_cancel", "Cancel"d, pal, 11, 24, true, false);
        btnClose.margins = Rect(0, 0, dp(6), 0);
        btnClose.click = (Widget s) { dismiss(); return true; };
        auto btnApply = new FlCompactButton("set_apply", "Apply Settings"d, pal, 11, 24, true, false);
        btnApply.customFillColor = pal.accentOrange;
        btnApply.customTextColor = pal.bgLcd;
        btnApply.click = (Widget s) {
            if (onApplySettings !is null) {
                onApplySettings(selectedLatencyMs, selectedDoublePrecision, selectedSandbox,
                                selectedOverdub, selectedCountdown);
            }
            dismiss();
            return true;
        };
        foot.addChild(sp);
        foot.addChild(btnClose);
        foot.addChild(btnApply);
        addChild(foot);
    }
}

unittest {
    import dlangui.graphics.fonts : Font, FontFamily, FontManager, FontRef, Glyph;
    import dlangui.graphics.resources : drawableCache, DrawableCache, embedStandardDlangUIResources,
        imageCache, ImageCache;
    import dlangui.widgets.styles : createDefaultTheme, currentTheme;
    import gui_d.theme : destroyWidgetTree;

    static class HeadlessModalFont : Font {
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

    static class HeadlessModalFontMgr : FontManager {
        FontRef _font;
        this() { _font = FontRef(new HeadlessModalFont()); }
        override ref FontRef getFont(int size, int weight, bool italic, FontFamily family, string face) {
            return _font;
        }
        override void checkpoint() {}
        override void cleanup() {}
    }

    const bool hadTheme = currentTheme !is null;
    if (!hadTheme) currentTheme = createDefaultTheme();
    const bool hadDrawable = drawableCache !is null;
    if (!hadDrawable) {
        embedStandardDlangUIResources();
        if (imageCache is null) imageCache = new ImageCache();
        drawableCache = new DrawableCache();
    }
    const bool hadFontMgr = FontManager.instance !is null;
    if (!hadFontMgr) FontManager.instance = new HeadlessModalFontMgr();
    scope (exit) {
        if (!hadTheme) currentTheme = null;
        if (!hadDrawable) { drawableCache = null; imageCache = null; }
        if (!hadFontMgr) FontManager.instance = null;
    }

    TransportModel model;
    model.initDefaultTemplate();

    string confirmedVal;
    auto prm = new FlTextPromptDialog(
        null, &model, "Save Project", "Path:", "build/demo.hbk",
        ["testdata/acid.hbk", "testdata/empty.hbk"],
        (string v) { confirmedVal = v; });
    scope (exit) destroyWidgetTree(prm);
    assert(prm.currentText() == "build/demo.hbk");
    prm.confirm();
    assert(confirmedVal == "build/demo.hbk");

    int appliedLat = 0;
    bool appliedDbl = false;
    bool appliedSb = false;
    auto setDlg = new FlSettingsDialog(null, &model);
    scope (exit) destroyWidgetTree(setDlg);
    setDlg.onApplySettings = (int l, bool d, bool s, bool ov, bool cd) {
        appliedLat = l;
        appliedDbl = d;
        appliedSb = s;
    };
    setDlg.latBtns_[2].click(setDlg.latBtns_[2]); // 20 ms
    setDlg.precBtns_[1].click(setDlg.precBtns_[1]); // 64-bit Double
    setDlg.hostBtns_[1].click(setDlg.hostBtns_[1]); // Sandbox
    setDlg.measure(500, 380);
    setDlg.layout(Rect(0, 0, 500, 380));
    auto applyBtn = setDlg.childById!FlCompactButton("set_apply");
    assert(applyBtn !is null);
    applyBtn.click(applyBtn);
    assert(appliedLat == 20 && appliedDbl && appliedSb);
}
