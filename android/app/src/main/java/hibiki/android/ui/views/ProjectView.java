package hibiki.android.ui.views;

import android.content.Context;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import hibiki.android.ui.theme.ThemeColors;
import java.util.Locale;

/**
 * Project Settings and Diagnostics View.
 */
public class ProjectView extends ScrollView {
    private String projectName = "New Beat 01";
    private double bpm = 128.0;
    private int bufferLatencyMs = 50;

    private TextView titleText;
    private TextView infoText;
    private OnProjectActionListener listener;

    public interface OnProjectActionListener {
        void onLoadDemoSong(String songName);
        void onResetEngine();
    }

    public ProjectView(Context context) {
        super(context);
        init(context);
    }

    public ProjectView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(context);
    }

    private void init(Context context) {
        setBackgroundColor(ThemeColors.BG_OLED_BLACK);
        float density = getResources().getDisplayMetrics().density;
        int pad = (int) (16 * density);
        setPadding(pad, pad, pad, pad);

        LinearLayout content = new LinearLayout(context);
        content.setOrientation(LinearLayout.VERTICAL);
        LayoutParams params = new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
        content.setLayoutParams(params);

        // 1. Project Info Card
        LinearLayout card1 = createCardLayout(context);
        TextView h1 = createCardHeader(context, "PROJECT SETTINGS", ThemeColors.ACCENT_CYAN);
        card1.addView(h1);

        titleText = new TextView(context);
        titleText.setText("Title: " + projectName);
        titleText.setTextSize(14);
        titleText.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        titleText.setTextColor(ThemeColors.TEXT_PRIMARY);
        card1.addView(titleText);

        infoText = new TextView(context);
        infoText.setText(String.format(Locale.US, "Tempo: %d BPM  |  Audio Buffer: %dms (AAudio Low Latency)", (int) bpm, bufferLatencyMs));
        infoText.setTextSize(11);
        infoText.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.NORMAL));
        infoText.setTextColor(ThemeColors.TEXT_SECONDARY);
        card1.addView(infoText);
        content.addView(card1);

        // 2. Demo Songs Card
        LinearLayout card2 = createCardLayout(context);
        TextView h2 = createCardHeader(context, "LOAD TEMPLATES & DEMO SONGS", ThemeColors.ACCENT_LIME);
        card2.addView(h2);

        LinearLayout demosRow = new LinearLayout(context);
        demosRow.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams rowLp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, (int) (44 * density));
        demosRow.setLayoutParams(rowLp);

        Button btnDemo1 = new Button(context);
        btnDemo1.setText("⚡ Electro Groove");
        btnDemo1.setTextSize(11);
        btnDemo1.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        btnDemo1.setTextColor(ThemeColors.ACCENT_CYAN);
        btnDemo1.setBackgroundColor(ThemeColors.BG_PANEL_CARD);
        LinearLayout.LayoutParams d1Params = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1.0f);
        d1Params.rightMargin = (int) (4 * density);
        btnDemo1.setLayoutParams(d1Params);
        btnDemo1.setOnClickListener(v -> {
            if (listener != null) listener.onLoadDemoSong("Electro Groove");
        });
        demosRow.addView(btnDemo1);

        Button btnDemo2 = new Button(context);
        btnDemo2.setText("🧪 Acid Bassline");
        btnDemo2.setTextSize(11);
        btnDemo2.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        btnDemo2.setTextColor(ThemeColors.ACCENT_AMBER);
        btnDemo2.setBackgroundColor(ThemeColors.BG_PANEL_CARD);
        LinearLayout.LayoutParams d2Params = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1.0f);
        d2Params.leftMargin = (int) (4 * density);
        btnDemo2.setLayoutParams(d2Params);
        btnDemo2.setOnClickListener(v -> {
            if (listener != null) listener.onLoadDemoSong("Acid Bassline");
        });
        demosRow.addView(btnDemo2);

        card2.addView(demosRow);
        content.addView(card2);

        // 3. Audio Diagnostics Card
        LinearLayout card3 = createCardLayout(context);
        TextView h3 = createCardHeader(context, "AUDIO ENGINE DIAGNOSTICS", ThemeColors.ACCENT_PINK);
        card3.addView(h3);

        TextView diagText = new TextView(context);
        diagText.setText("Backend: Hibiki C++ Core (In-Process AAudio Stream)\nDriver: libaaudio.so\nSample Rate: 44,100 Hz / Stereo");
        diagText.setTextSize(11);
        diagText.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.NORMAL));
        diagText.setTextColor(ThemeColors.TEXT_SECONDARY);
        diagText.setLineSpacing(0, 1.2f);
        card3.addView(diagText);

        Button btnRestart = new Button(context);
        btnRestart.setText("🔄 RESTART AUDIO ENGINE");
        btnRestart.setTextSize(11);
        btnRestart.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        btnRestart.setTextColor(ThemeColors.ACCENT_PINK);
        btnRestart.setBackgroundColor(ThemeColors.BG_PANEL_CARD);
        LinearLayout.LayoutParams rParams = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, (int) (40 * density));
        rParams.topMargin = (int) (8 * density);
        btnRestart.setLayoutParams(rParams);
        btnRestart.setOnClickListener(v -> {
            if (listener != null) listener.onResetEngine();
        });
        card3.addView(btnRestart);

        content.addView(card3);

        addView(content);
    }

    private LinearLayout createCardLayout(Context context) {
        float density = getResources().getDisplayMetrics().density;
        LinearLayout card = new LinearLayout(context);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackgroundColor(ThemeColors.BG_PANEL_DARK);
        int pad = (int) (12 * density);
        card.setPadding(pad, pad, pad, pad);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.bottomMargin = (int) (14 * density);
        card.setLayoutParams(lp);
        return card;
    }

    private TextView createCardHeader(Context context, String title, int color) {
        float density = getResources().getDisplayMetrics().density;
        TextView h = new TextView(context);
        h.setText(title);
        h.setTextSize(12);
        h.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        h.setTextColor(color);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.bottomMargin = (int) (8 * density);
        h.setLayoutParams(lp);
        return h;
    }

    public void setProjectInfo(String name, double bpm, int bufferLatencyMs) {
        this.projectName = name;
        this.bpm = bpm;
        this.bufferLatencyMs = bufferLatencyMs;
        titleText.setText("Title: " + projectName);
        infoText.setText(String.format(Locale.US, "Tempo: %d BPM  |  Audio Buffer: %dms (AAudio Low Latency)", (int) bpm, bufferLatencyMs));
    }

    public void setOnProjectActionListener(OnProjectActionListener listener) {
        this.listener = listener;
    }
}
