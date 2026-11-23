package hibiki.android.ui.components;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.AttributeSet;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import hibiki.android.model.ViewMode;
import hibiki.android.ui.theme.ThemeColors;
import java.util.Locale;

/**
 * Global Header and Transport Bar view.
 */
public class HeaderBarView extends LinearLayout {
    private TextView logoText;
    private TextView timeDisplay;
    private Button btnPlayPause;
    private Button btnStop;
    private Button btnRec;
    private Button btnLoop;
    private TextView bpmDisplay;
    private LinearLayout tabsLayout;

    private boolean isPlaying = false;
    private boolean isRecording = false;
    private boolean isLooping = true;
    private double bpm = 128.0;
    private double playheadSec = 0.0;
    private ViewMode currentView = ViewMode.TRACKER;

    private HeaderActionListener listener;

    public interface HeaderActionListener {
        void onTogglePlay();
        void onStop();
        void onToggleRecord();
        void onToggleLoop();
        void onBpmChange(double newBpm);
        void onSelectView(ViewMode mode);
    }

    public HeaderBarView(Context context) {
        super(context);
        init(context);
    }

    public HeaderBarView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(context);
    }

    private void init(Context context) {
        setOrientation(VERTICAL);
        setBackgroundColor(ThemeColors.BG_PANEL_DARK);
        float density = getResources().getDisplayMetrics().density;
        int padH = (int) (8 * density);
        int padV = (int) (6 * density);
        setPadding(padH, padV, padH, padV);

        // Top Row: Brand, Time, Transport, BPM
        LinearLayout topRow = new LinearLayout(context);
        topRow.setOrientation(HORIZONTAL);
        topRow.setGravity(Gravity.CENTER_VERTICAL);
        LayoutParams topParams = new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
        topRow.setLayoutParams(topParams);

        // Brand & Time
        logoText = new TextView(context);
        logoText.setText("HIBIKI");
        logoText.setTextSize(16);
        logoText.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        logoText.setTextColor(ThemeColors.ACCENT_CYAN);
        topRow.addView(logoText);

        timeDisplay = new TextView(context);
        timeDisplay.setText("00:00.00");
        timeDisplay.setTextSize(12);
        timeDisplay.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        timeDisplay.setTextColor(ThemeColors.ACCENT_LIME);
        timeDisplay.setBackgroundColor(ThemeColors.BG_OLED_BLACK);
        timeDisplay.setPadding((int) (6 * density), (int) (3 * density), (int) (6 * density), (int) (3 * density));
        LayoutParams timeParams = new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
        timeParams.leftMargin = (int) (8 * density);
        timeParams.rightMargin = (int) (8 * density);
        timeDisplay.setLayoutParams(timeParams);
        topRow.addView(timeDisplay);

        // Spacer to push transport controls
        View spacer1 = new View(context);
        LayoutParams spacerParams = new LayoutParams(0, 1, 1.0f);
        spacer1.setLayoutParams(spacerParams);
        topRow.addView(spacer1);

        // Transport Buttons
        btnPlayPause = createStyledButton(context, "PLAY", ThemeColors.BG_PANEL_CARD, ThemeColors.TEXT_PRIMARY);
        btnPlayPause.setOnClickListener(v -> {
            if (listener != null) listener.onTogglePlay();
        });
        topRow.addView(btnPlayPause);

        btnStop = createStyledButton(context, "STOP", ThemeColors.BG_PANEL_CARD, ThemeColors.TEXT_SECONDARY);
        btnStop.setOnClickListener(v -> {
            if (listener != null) listener.onStop();
        });
        topRow.addView(btnStop);

        btnRec = createStyledButton(context, "REC", ThemeColors.BG_PANEL_CARD, ThemeColors.TEXT_SECONDARY);
        btnRec.setOnClickListener(v -> {
            if (listener != null) listener.onToggleRecord();
        });
        topRow.addView(btnRec);

        btnLoop = createStyledButton(context, "LOOP", isLooping ? ThemeColors.ACCENT_AMBER : ThemeColors.BG_PANEL_CARD,
                isLooping ? ThemeColors.BG_OLED_BLACK : ThemeColors.TEXT_SECONDARY);
        btnLoop.setOnClickListener(v -> {
            if (listener != null) listener.onToggleLoop();
        });
        topRow.addView(btnLoop);

        // Spacer
        View spacer2 = new View(context);
        spacer2.setLayoutParams(new LayoutParams((int) (6 * density), 1));
        topRow.addView(spacer2);

        // BPM controls
        Button btnBpmMinus = createStyledButton(context, "-", ThemeColors.BG_PANEL_CARD, ThemeColors.TEXT_PRIMARY);
        btnBpmMinus.setOnClickListener(v -> {
            if (listener != null) listener.onBpmChange(Math.max(40.0, bpm - 1.0));
        });
        topRow.addView(btnBpmMinus);

        bpmDisplay = new TextView(context);
        bpmDisplay.setText("128 BPM");
        bpmDisplay.setTextSize(11);
        bpmDisplay.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        bpmDisplay.setTextColor(ThemeColors.ACCENT_AMBER);
        bpmDisplay.setBackgroundColor(ThemeColors.BG_OLED_BLACK);
        bpmDisplay.setPadding((int) (4 * density), (int) (4 * density), (int) (4 * density), (int) (4 * density));
        LayoutParams bpmParams = new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
        bpmParams.leftMargin = (int) (2 * density);
        bpmParams.rightMargin = (int) (2 * density);
        bpmDisplay.setLayoutParams(bpmParams);
        topRow.addView(bpmDisplay);

        Button btnBpmPlus = createStyledButton(context, "+", ThemeColors.BG_PANEL_CARD, ThemeColors.TEXT_PRIMARY);
        btnBpmPlus.setOnClickListener(v -> {
            if (listener != null) listener.onBpmChange(Math.min(300.0, bpm + 1.0));
        });
        topRow.addView(btnBpmPlus);

        addView(topRow);

        // View Mode Navigation Tabs
        tabsLayout = new LinearLayout(context);
        tabsLayout.setOrientation(HORIZONTAL);
        LayoutParams tabsParams = new LayoutParams(LayoutParams.MATCH_PARENT, (int) (32 * density));
        tabsParams.topMargin = (int) (6 * density);
        tabsLayout.setLayoutParams(tabsParams);

        for (ViewMode mode : ViewMode.values()) {
            Button tabBtn = new Button(context);
            tabBtn.setText(mode.getLabel());
            tabBtn.setTextSize(10);
            tabBtn.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
            LayoutParams lp = new LayoutParams(0, LayoutParams.MATCH_PARENT, 1.0f);
            lp.setMargins((int) (2 * density), 0, (int) (2 * density), 0);
            tabBtn.setLayoutParams(lp);
            tabBtn.setPadding(0, 0, 0, 0);
            tabBtn.setTag(mode);
            tabBtn.setOnClickListener(v -> {
                if (listener != null) listener.onSelectView((ViewMode) v.getTag());
            });
            tabsLayout.addView(tabBtn);
        }
        addView(tabsLayout);

        updateUI();
    }

    private Button createStyledButton(Context context, String text, int bgColor, int textColor) {
        float density = getResources().getDisplayMetrics().density;
        Button btn = new Button(context);
        btn.setText(text);
        btn.setTextSize(10);
        btn.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        btn.setTextColor(textColor);
        btn.setBackgroundColor(bgColor);
        btn.setPadding((int) (4 * density), 0, (int) (4 * density), 0);
        btn.setMinimumHeight(0);
        btn.setMinimumWidth(0);
        LayoutParams lp = new LayoutParams(LayoutParams.WRAP_CONTENT, (int) (32 * density));
        lp.leftMargin = (int) (2 * density);
        lp.rightMargin = (int) (2 * density);
        btn.setLayoutParams(lp);
        return btn;
    }

    public void setHeaderActionListener(HeaderActionListener listener) {
        this.listener = listener;
    }

    public TextView getLogoText() { return logoText; }
    public TextView getTimeDisplay() { return timeDisplay; }
    public Button getBtnPlayPause() { return btnPlayPause; }
    public Button getBtnStop() { return btnStop; }
    public Button getBtnRec() { return btnRec; }
    public Button getBtnLoop() { return btnLoop; }
    public TextView getBpmDisplay() { return bpmDisplay; }
    public LinearLayout getTabsLayout() { return tabsLayout; }

    public Button getTabButton(ViewMode mode) {
        if (tabsLayout == null) return null;
        for (int i = 0; i < tabsLayout.getChildCount(); i++) {
            View child = tabsLayout.getChildAt(i);
            if (child instanceof Button && mode == child.getTag()) {
                return (Button) child;
            }
        }
        return null;
    }

    public void updateState(boolean isPlaying, boolean isRecording, boolean isLooping, double bpm, double playheadSec, ViewMode currentView) {
        this.isPlaying = isPlaying;
        this.isRecording = isRecording;
        this.isLooping = isLooping;
        this.bpm = bpm;
        this.playheadSec = playheadSec;
        this.currentView = currentView;
        updateUI();
    }

    private void updateUI() {
        int mins = (int) (playheadSec / 60);
        int secs = (int) (playheadSec % 60);
        int millis = (int) ((playheadSec * 100) % 100);
        timeDisplay.setText(String.format(Locale.US, "%02d:%02d.%02d", mins, secs, millis));
        timeDisplay.setTextColor(isPlaying ? ThemeColors.ACCENT_LIME : ThemeColors.TEXT_SECONDARY);

        btnPlayPause.setText(isPlaying ? "PAUSE" : "PLAY");
        btnPlayPause.setBackgroundColor(isPlaying ? ThemeColors.ACCENT_LIME : ThemeColors.BG_PANEL_CARD);
        btnPlayPause.setTextColor(isPlaying ? ThemeColors.BG_OLED_BLACK : ThemeColors.TEXT_PRIMARY);

        btnRec.setBackgroundColor(isRecording ? ThemeColors.ACCENT_RED : ThemeColors.BG_PANEL_CARD);
        btnRec.setTextColor(isRecording ? ThemeColors.TEXT_PRIMARY : ThemeColors.TEXT_SECONDARY);

        btnLoop.setBackgroundColor(isLooping ? ThemeColors.ACCENT_AMBER : ThemeColors.BG_PANEL_CARD);
        btnLoop.setTextColor(isLooping ? ThemeColors.BG_OLED_BLACK : ThemeColors.TEXT_SECONDARY);

        bpmDisplay.setText(String.format(Locale.US, "%d BPM", (int) bpm));

        // Update tabs styling
        for (int i = 0; i < tabsLayout.getChildCount(); i++) {
            View child = tabsLayout.getChildAt(i);
            if (child instanceof Button) {
                Button tab = (Button) child;
                ViewMode mode = (ViewMode) tab.getTag();
                boolean isSelected = (mode == currentView);
                GradientDrawable shape = new GradientDrawable();
                shape.setCornerRadius(8);
                if (isSelected) {
                    shape.setColor((ThemeColors.ACCENT_CYAN & 0x00FFFFFF) | 0x33000000);
                    shape.setStroke(2, ThemeColors.ACCENT_CYAN);
                    tab.setTextColor(ThemeColors.ACCENT_CYAN);
                } else {
                    shape.setColor(ThemeColors.BG_PANEL_CARD);
                    tab.setTextColor(ThemeColors.TEXT_SECONDARY);
                }
                tab.setBackground(shape);
            }
        }
    }
}
