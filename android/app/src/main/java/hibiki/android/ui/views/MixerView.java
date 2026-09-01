package hibiki.android.ui.views;

import android.content.Context;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.view.Gravity;
import android.widget.Button;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.TextView;
import hibiki.android.model.ChannelState;
import hibiki.android.ui.components.KnobView;
import hibiki.android.ui.components.TouchFaderView;
import hibiki.android.ui.theme.ThemeColors;
import java.util.ArrayList;
import java.util.List;

/**
 * Multi-channel Mixer View with Touch Faders, Pan Dials, Mute/Solo.
 */
public class MixerView extends HorizontalScrollView {
    private final List<ChannelState> channels = new ArrayList<>();
    private LinearLayout container;
    private OnMixerActionListener listener;

    public interface OnMixerActionListener {
        void onVolumeChange(int channelIdx, float newVolume);
        void onPanChange(int channelIdx, float newPan);
        void onToggleMute(int channelIdx);
        void onToggleSolo(int channelIdx);
    }

    public MixerView(Context context) {
        super(context);
        init(context);
    }

    public MixerView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(context);
    }

    private void init(Context context) {
        setBackgroundColor(ThemeColors.BG_OLED_BLACK);
        float density = getResources().getDisplayMetrics().density;
        int pad = (int) (8 * density);
        setPadding(pad, pad, pad, pad);

        container = new LinearLayout(context);
        container.setOrientation(LinearLayout.HORIZONTAL);
        LayoutParams params = new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.MATCH_PARENT);
        container.setLayoutParams(params);
        addView(container);
    }

    public void setChannels(List<ChannelState> newChannels) {
        this.channels.clear();
        if (newChannels != null) {
            this.channels.addAll(newChannels);
        }
        rebuildMixerStrips();
    }

    public void setOnMixerActionListener(OnMixerActionListener listener) {
        this.listener = listener;
    }

    private void rebuildMixerStrips() {
        container.removeAllViews();
        Context context = getContext();
        float density = getResources().getDisplayMetrics().density;

        for (int i = 0; i < channels.size(); i++) {
            final int channelIdx = i;
            ChannelState ch = channels.get(i);

            LinearLayout strip = new LinearLayout(context);
            strip.setOrientation(LinearLayout.VERTICAL);
            strip.setGravity(Gravity.CENTER_HORIZONTAL);
            strip.setBackgroundColor(ThemeColors.BG_PANEL_DARK);
            int pad = (int) (6 * density);
            strip.setPadding(pad, pad, pad, pad);
            LinearLayout.LayoutParams stripParams =
                    new LinearLayout.LayoutParams((int) (76 * density), LinearLayout.LayoutParams.MATCH_PARENT);
            stripParams.rightMargin = (int) (8 * density);
            strip.setLayoutParams(stripParams);

            // Track Name
            TextView nameText = new TextView(context);
            nameText.setText("T" + (i + 1) + "\n" + (ch.getName().length() > 5 ? ch.getName().substring(0, 5) : ch.getName()));
            nameText.setTextSize(11);
            nameText.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
            nameText.setTextColor(ch.getColor());
            nameText.setGravity(Gravity.CENTER);
            strip.addView(nameText);

            // Pan Knob
            KnobView panKnob = new KnobView(context);
            panKnob.setParams("PAN", (ch.getPan() + 1.0f) / 2.0f, ch.getColor());
            LinearLayout.LayoutParams panParams = new LinearLayout.LayoutParams((int) (60 * density), (int) (60 * density));
            panParams.topMargin = (int) (4 * density);
            panParams.bottomMargin = (int) (4 * density);
            panKnob.setLayoutParams(panParams);
            panKnob.setOnValueChangeListener(newVal -> {
                float pan = (newVal * 2.0f) - 1.0f;
                if (listener != null) listener.onPanChange(channelIdx, pan);
            });
            strip.addView(panKnob);

            // Mute / Solo Buttons Row
            LinearLayout msRow = new LinearLayout(context);
            msRow.setOrientation(LinearLayout.HORIZONTAL);
            msRow.setGravity(Gravity.CENTER);
            LinearLayout.LayoutParams msParams =
                    new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, (int) (26 * density));
            msParams.bottomMargin = (int) (4 * density);
            msRow.setLayoutParams(msParams);

            Button btnMute = new Button(context);
            btnMute.setText("M");
            btnMute.setTextSize(9);
            btnMute.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
            btnMute.setTextColor(ch.isMuted() ? ThemeColors.TEXT_PRIMARY : ThemeColors.TEXT_MUTED);
            btnMute.setBackgroundColor(ch.isMuted() ? ThemeColors.ACCENT_RED : ThemeColors.BG_PANEL_CARD);
            LinearLayout.LayoutParams mParams = new LinearLayout.LayoutParams((int) (26 * density), (int) (26 * density));
            mParams.rightMargin = (int) (2 * density);
            btnMute.setLayoutParams(mParams);
            btnMute.setPadding(0, 0, 0, 0);
            btnMute.setOnClickListener(v -> {
                if (listener != null) listener.onToggleMute(channelIdx);
            });
            msRow.addView(btnMute);

            Button btnSolo = new Button(context);
            btnSolo.setText("S");
            btnSolo.setTextSize(9);
            btnSolo.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
            btnSolo.setTextColor(ch.isSoloed() ? ThemeColors.BG_OLED_BLACK : ThemeColors.TEXT_MUTED);
            btnSolo.setBackgroundColor(ch.isSoloed() ? ThemeColors.ACCENT_AMBER : ThemeColors.BG_PANEL_CARD);
            LinearLayout.LayoutParams sParams = new LinearLayout.LayoutParams((int) (26 * density), (int) (26 * density));
            sParams.leftMargin = (int) (2 * density);
            btnSolo.setLayoutParams(sParams);
            btnSolo.setPadding(0, 0, 0, 0);
            btnSolo.setOnClickListener(v -> {
                if (listener != null) listener.onToggleSolo(channelIdx);
            });
            msRow.addView(btnSolo);

            strip.addView(msRow);

            // Volume Fader
            TouchFaderView fader = new TouchFaderView(context);
            fader.setParams(ch.getVolume(), ch.getColor());
            LinearLayout.LayoutParams faderParams =
                    new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1.0f);
            fader.setLayoutParams(faderParams);
            fader.setOnValueChangeListener(newVal -> {
                if (listener != null) listener.onVolumeChange(channelIdx, newVal);
            });
            strip.addView(fader);

            container.addView(strip);
        }
    }
}
