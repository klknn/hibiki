package hibiki.android.ui.views;

import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.AttributeSet;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;
import hibiki.android.model.DrumPadItem;
import hibiki.android.model.ScaleType;
import hibiki.android.model.SynthMacro;
import hibiki.android.ui.components.DrumPadsView;
import hibiki.android.ui.components.KnobView;
import hibiki.android.ui.components.ScaleKeyboardView;
import hibiki.android.ui.theme.ThemeColors;
import java.util.ArrayList;
import java.util.List;

/**
 * Performance Instruments View (Drum Pads & Scaled Keyboard).
 */
public class InstrumentView extends LinearLayout {
    private final List<DrumPadItem> pads = new ArrayList<>();
    private final List<SynthMacro> macros = new ArrayList<>();
    private int rootNoteIdx = 0;
    private ScaleType scaleType = ScaleType.PENTATONIC_MINOR;
    private int octave = 4;
    private int activeTab = 0; // 0: Drum Pads, 1: Scale Keyboard

    private LinearLayout macrosLayout;
    private Button btnPadsTab;
    private Button btnKeysTab;
    private FrameLayout surfaceContainer;
    private DrumPadsView drumPadsView;
    private LinearLayout keyboardLayout;
    private ScaleKeyboardView scaleKeyboardView;
    private LinearLayout scalesBar;
    private TextView octaveText;

    private OnInstrumentActionListener listener;

    public interface OnInstrumentActionListener {
        void onTriggerPad(DrumPadItem pad);
        void onTriggerNote(int midiNote, String noteName);
        void onMacroChange(String macroId, float newValue);
        void onScaleConfigChange(int rootNoteIdx, ScaleType scaleType, int octave);
    }

    public InstrumentView(Context context) {
        super(context);
        init(context);
    }

    public InstrumentView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(context);
    }

    private void init(Context context) {
        setOrientation(VERTICAL);
        setBackgroundColor(ThemeColors.BG_OLED_BLACK);
        float density = getResources().getDisplayMetrics().density;
        int pad = (int) (8 * density);
        setPadding(pad, pad, pad, pad);

        // 1. Macro Knobs Bar
        macrosLayout = new LinearLayout(context);
        macrosLayout.setOrientation(HORIZONTAL);
        macrosLayout.setGravity(Gravity.CENTER_VERTICAL);
        macrosLayout.setBackgroundColor(ThemeColors.BG_PANEL_DARK);
        macrosLayout.setPadding(pad, pad, pad, pad);
        LayoutParams macroParams = new LayoutParams(LayoutParams.MATCH_PARENT, (int) (80 * density));
        macrosLayout.setLayoutParams(macroParams);
        addView(macrosLayout);

        // 2. Instrument Mode Switcher Tabs (PADS vs KEYS)
        LinearLayout tabsLayout = new LinearLayout(context);
        tabsLayout.setOrientation(HORIZONTAL);
        LayoutParams tabsParams = new LayoutParams(LayoutParams.MATCH_PARENT, (int) (36 * density));
        tabsParams.topMargin = (int) (6 * density);
        tabsParams.bottomMargin = (int) (6 * density);
        tabsLayout.setLayoutParams(tabsParams);

        btnPadsTab = new Button(context);
        btnPadsTab.setText("🥁 DRUM PADS");
        btnPadsTab.setTextSize(11);
        btnPadsTab.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        LayoutParams lpTab1 = new LayoutParams(0, LayoutParams.MATCH_PARENT, 1.0f);
        lpTab1.rightMargin = (int) (3 * density);
        btnPadsTab.setLayoutParams(lpTab1);
        btnPadsTab.setOnClickListener(v -> switchTab(0));
        tabsLayout.addView(btnPadsTab);

        btnKeysTab = new Button(context);
        btnKeysTab.setText("🎹 SCALE KEYBOARD");
        btnKeysTab.setTextSize(11);
        btnKeysTab.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        LayoutParams lpTab2 = new LayoutParams(0, LayoutParams.MATCH_PARENT, 1.0f);
        lpTab2.leftMargin = (int) (3 * density);
        btnKeysTab.setLayoutParams(lpTab2);
        btnKeysTab.setOnClickListener(v -> switchTab(1));
        tabsLayout.addView(btnKeysTab);

        addView(tabsLayout);

        // 3. Performance Surface Container
        surfaceContainer = new FrameLayout(context);
        LayoutParams surfParams = new LayoutParams(LayoutParams.MATCH_PARENT, 0, 1.0f);
        surfaceContainer.setLayoutParams(surfParams);

        // Drum Pads
        drumPadsView = new DrumPadsView(context);
        drumPadsView.setLayoutParams(new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        drumPadsView.setOnPadTriggerListener(padItem -> {
            if (listener != null) listener.onTriggerPad(padItem);
        });
        surfaceContainer.addView(drumPadsView);

        // Scale Keyboard Layout
        keyboardLayout = new LinearLayout(context);
        keyboardLayout.setOrientation(VERTICAL);
        keyboardLayout.setLayoutParams(new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));

        // Scale selector & Octave controls bar
        scalesBar = new LinearLayout(context);
        scalesBar.setOrientation(HORIZONTAL);
        scalesBar.setGravity(Gravity.CENTER_VERTICAL);
        LayoutParams scalesBarParams = new LayoutParams(LayoutParams.MATCH_PARENT, (int) (32 * density));
        scalesBar.setLayoutParams(scalesBarParams);

        ScaleType[] commonScales = new ScaleType[] {
            ScaleType.PENTATONIC_MINOR, ScaleType.BLUES, ScaleType.MINOR, ScaleType.MAJOR
        };
        for (ScaleType s : commonScales) {
            Button sBtn = new Button(context);
            sBtn.setText(s.getDisplayName().length() > 8 ? s.getDisplayName().substring(0, 8) : s.getDisplayName());
            sBtn.setTextSize(9);
            sBtn.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
            LayoutParams sLp = new LayoutParams(0, LayoutParams.MATCH_PARENT, 1.0f);
            sLp.setMargins((int) (2 * density), 0, (int) (2 * density), 0);
            sBtn.setLayoutParams(sLp);
            sBtn.setPadding(0, 0, 0, 0);
            sBtn.setTag(s);
            sBtn.setOnClickListener(v -> {
                scaleType = (ScaleType) v.getTag();
                updateScaleView();
                if (listener != null) listener.onScaleConfigChange(rootNoteIdx, scaleType, octave);
            });
            scalesBar.addView(sBtn);
        }

        // Octave controls
        Button octMinus = new Button(context);
        octMinus.setText("-");
        octMinus.setTextSize(12);
        octMinus.setTextColor(ThemeColors.TEXT_PRIMARY);
        octMinus.setBackgroundColor(ThemeColors.BG_PANEL_CARD);
        octMinus.setLayoutParams(new LayoutParams((int) (28 * density), (int) (28 * density)));
        octMinus.setPadding(0, 0, 0, 0);
        octMinus.setOnClickListener(v -> {
            octave = Math.max(1, octave - 1);
            updateScaleView();
            if (listener != null) listener.onScaleConfigChange(rootNoteIdx, scaleType, octave);
        });
        scalesBar.addView(octMinus);

        octaveText = new TextView(context);
        octaveText.setText("OCT 4");
        octaveText.setTextSize(11);
        octaveText.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        octaveText.setTextColor(ThemeColors.ACCENT_PINK);
        octaveText.setPadding((int) (4 * density), 0, (int) (4 * density), 0);
        scalesBar.addView(octaveText);

        Button octPlus = new Button(context);
        octPlus.setText("+");
        octPlus.setTextSize(12);
        octPlus.setTextColor(ThemeColors.TEXT_PRIMARY);
        octPlus.setBackgroundColor(ThemeColors.BG_PANEL_CARD);
        octPlus.setLayoutParams(new LayoutParams((int) (28 * density), (int) (28 * density)));
        octPlus.setPadding(0, 0, 0, 0);
        octPlus.setOnClickListener(v -> {
            octave = Math.min(7, octave + 1);
            updateScaleView();
            if (listener != null) listener.onScaleConfigChange(rootNoteIdx, scaleType, octave);
        });
        scalesBar.addView(octPlus);

        keyboardLayout.addView(scalesBar);

        scaleKeyboardView = new ScaleKeyboardView(context);
        LayoutParams kLp = new LayoutParams(LayoutParams.MATCH_PARENT, 0, 1.0f);
        kLp.topMargin = (int) (8 * density);
        scaleKeyboardView.setLayoutParams(kLp);
        scaleKeyboardView.setOnNoteTriggerListener((midiNote, noteName) -> {
            if (listener != null) listener.onTriggerNote(midiNote, noteName);
        });
        keyboardLayout.addView(scaleKeyboardView);

        surfaceContainer.addView(keyboardLayout);
        addView(surfaceContainer);

        switchTab(0);
    }

    public void setInstrumentsData(List<DrumPadItem> newPads, List<SynthMacro> newMacros,
                                   int rootNote, ScaleType sType, int oct) {
        this.pads.clear();
        if (newPads != null) this.pads.addAll(newPads);
        drumPadsView.setPads(this.pads);

        this.macros.clear();
        if (newMacros != null) this.macros.addAll(newMacros);
        rebuildMacrosUI();

        this.rootNoteIdx = rootNote;
        this.scaleType = sType;
        this.octave = oct;
        updateScaleView();
    }

    public void setOnInstrumentActionListener(OnInstrumentActionListener listener) {
        this.listener = listener;
    }

    private void switchTab(int tab) {
        this.activeTab = tab;
        float density = getResources().getDisplayMetrics().density;
        if (tab == 0) {
            btnPadsTab.setBackgroundColor((ThemeColors.ACCENT_CYAN & 0x00FFFFFF) | 0x33000000);
            btnPadsTab.setTextColor(ThemeColors.ACCENT_CYAN);
            btnKeysTab.setBackgroundColor(ThemeColors.BG_PANEL_DARK);
            btnKeysTab.setTextColor(ThemeColors.TEXT_SECONDARY);
            drumPadsView.setVisibility(VISIBLE);
            keyboardLayout.setVisibility(GONE);
        } else {
            btnPadsTab.setBackgroundColor(ThemeColors.BG_PANEL_DARK);
            btnPadsTab.setTextColor(ThemeColors.TEXT_SECONDARY);
            btnKeysTab.setBackgroundColor((ThemeColors.ACCENT_PINK & 0x00FFFFFF) | 0x33000000);
            btnKeysTab.setTextColor(ThemeColors.ACCENT_PINK);
            drumPadsView.setVisibility(GONE);
            keyboardLayout.setVisibility(VISIBLE);
        }
    }

    private void rebuildMacrosUI() {
        macrosLayout.removeAllViews();
        Context context = getContext();
        for (SynthMacro m : macros) {
            KnobView knob = new KnobView(context);
            knob.setParams(m.getName(), m.getValue(), ThemeColors.ACCENT_CYAN);
            LayoutParams lp = new LayoutParams(0, LayoutParams.MATCH_PARENT, 1.0f);
            knob.setLayoutParams(lp);
            knob.setOnValueChangeListener(newVal -> {
                if (listener != null) listener.onMacroChange(m.getId(), newVal);
            });
            macrosLayout.addView(knob);
        }
    }

    private void updateScaleView() {
        octaveText.setText("OCT " + octave);
        scaleKeyboardView.setScaleConfig(rootNoteIdx, scaleType, octave, ThemeColors.ACCENT_PINK);

        for (int i = 0; i < scalesBar.getChildCount(); i++) {
            View v = scalesBar.getChildAt(i);
            if (v instanceof Button && v.getTag() instanceof ScaleType) {
                Button b = (Button) v;
                boolean isSel = (b.getTag() == scaleType);
                b.setBackgroundColor(isSel ? ((ThemeColors.ACCENT_PINK & 0x00FFFFFF) | 0x44000000) : ThemeColors.BG_PANEL_CARD);
                b.setTextColor(isSel ? ThemeColors.ACCENT_PINK : ThemeColors.TEXT_SECONDARY);
            }
        }
    }
}
