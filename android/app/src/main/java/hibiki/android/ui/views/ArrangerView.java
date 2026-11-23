package hibiki.android.ui.views;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import hibiki.android.model.ArrangerPattern;
import hibiki.android.model.ChannelState;
import hibiki.android.ui.theme.ThemeColors;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Multi-track Song Arranger View.
 */
public class ArrangerView extends View {
    private final List<ChannelState> channels = new ArrayList<>();
    private final List<ArrangerPattern> patterns = new ArrayList<>();
    private float playheadBar = 0.0f;
    private int totalBars = 16;
    private float scrollXOffset = 0.0f;

    private OnArrangerActionListener listener;

    private final Paint bgPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint linePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint clipPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF clipRect = new RectF();

    private float lastTouchX;
    private boolean isDraggingScroll;

    public interface OnArrangerActionListener {
        void onAddPattern(int trackIndex, float startBar);
        void onRemovePattern(String patternId);
        void onToggleMute(int trackIndex);
    }

    public ArrangerView(Context context) {
        super(context);
        init();
    }

    public ArrangerView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    private void init() {
        bgPaint.setStyle(Paint.Style.FILL);
        clipPaint.setStyle(Paint.Style.FILL);
        linePaint.setStyle(Paint.Style.STROKE);
        textPaint.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
    }

    public void setArrangement(List<ChannelState> newChannels, List<ArrangerPattern> newPatterns, float playheadBar) {
        channels.clear();
        if (newChannels != null) {
            channels.addAll(newChannels);
        }
        patterns.clear();
        if (newPatterns != null) {
            patterns.addAll(newPatterns);
        }
        this.playheadBar = playheadBar;
        invalidate();
    }

    public void setPlayheadBar(float playheadBar) {
        this.playheadBar = playheadBar;
        invalidate();
    }

    public void setOnArrangerActionListener(OnArrangerActionListener listener) {
        this.listener = listener;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        float density = getResources().getDisplayMetrics().density;
        float headerH = 28.0f * density;
        float trackHeaderW = 80.0f * density;
        float barW = 60.0f * density;
        int chCount = channels.size();
        if (chCount == 0) return super.onTouchEvent(event);
        float trackH = (getHeight() - headerH) / Math.max(1, chCount);

        switch (event.getAction()) {
            case MotionEvent.ACTION_DOWN:
                lastTouchX = event.getX();
                isDraggingScroll = false;
                return true;
            case MotionEvent.ACTION_MOVE:
                float dx = lastTouchX - event.getX();
                if (Math.abs(dx) > 4 * density) {
                    isDraggingScroll = true;
                    scrollXOffset = Math.max(0, scrollXOffset + dx);
                    lastTouchX = event.getX();
                    invalidate();
                }
                return true;
            case MotionEvent.ACTION_UP:
                if (!isDraggingScroll) {
                    float x = event.getX();
                    float y = event.getY();

                    if (x < trackHeaderW) {
                        // Track header clicked -> toggle mute
                        int trackIdx = (int) ((y - headerH) / trackH);
                        if (trackIdx >= 0 && trackIdx < chCount && listener != null) {
                            listener.onToggleMute(trackIdx);
                        }
                    } else if (y >= headerH) {
                        int trackIdx = (int) ((y - headerH) / trackH);
                        float timelineX = x - trackHeaderW + scrollXOffset;
                        float clickedBar = (float) Math.floor(timelineX / barW);

                        // Check if tapped on existing clip
                        ArrangerPattern tappedClip = null;
                        for (ArrangerPattern p : patterns) {
                            if (p.getTrackIndex() == trackIdx &&
                                    clickedBar >= p.getStartBar() &&
                                    clickedBar < (p.getStartBar() + p.getLengthBars())) {
                                tappedClip = p;
                                break;
                            }
                        }

                        if (tappedClip != null) {
                            if (listener != null) listener.onRemovePattern(tappedClip.getId());
                        } else if (trackIdx >= 0 && trackIdx < chCount) {
                            if (listener != null) listener.onAddPattern(trackIdx, clickedBar);
                        }
                    }
                }
                return true;
        }
        return super.onTouchEvent(event);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int chCount = channels.size();
        if (chCount == 0) return;

        float density = getResources().getDisplayMetrics().density;
        float headerH = 28.0f * density;
        float trackHeaderW = 80.0f * density;
        float barW = 60.0f * density;
        float trackH = (getHeight() - headerH) / Math.max(1, chCount);

        // Background
        bgPaint.setColor(ThemeColors.BG_OLED_BLACK);
        canvas.drawRect(0, 0, getWidth(), getHeight(), bgPaint);

        // Ruler Bar Header
        bgPaint.setColor(ThemeColors.BG_PANEL_DARK);
        canvas.drawRect(0, 0, getWidth(), headerH, bgPaint);

        textPaint.setColor(ThemeColors.TEXT_MUTED);
        textPaint.setTextSize(10.0f * density);
        textPaint.setTextAlign(Paint.Align.CENTER);
        canvas.drawText("TRACK", trackHeaderW / 2.0f, headerH / 2.0f + 3.5f * density, textPaint);

        // Bars Ruler
        for (int b = 1; b <= totalBars; b++) {
            float barX = trackHeaderW + (b - 1) * barW - scrollXOffset;
            if (barX + barW < trackHeaderW || barX > getWidth()) continue;

            linePaint.setColor(ThemeColors.BG_PANEL_CARD);
            linePaint.setStrokeWidth(1.0f * density);
            canvas.drawLine(barX, 0, barX, getHeight(), linePaint);

            textPaint.setColor(ThemeColors.TEXT_SECONDARY);
            textPaint.setTextSize(9.0f * density);
            canvas.drawText("BAR " + b, barX + barW / 2.0f, headerH / 2.0f + 3.0f * density, textPaint);
        }

        // Tracks Lane & Clips
        for (int t = 0; t < chCount; t++) {
            ChannelState ch = channels.get(t);
            float top = headerH + t * trackH;

            // Lane separator
            linePaint.setColor(ThemeColors.BG_PANEL_CARD);
            canvas.drawLine(0, top + trackH, getWidth(), top + trackH, linePaint);

            // Pattern Clips
            for (ArrangerPattern pat : patterns) {
                if (pat.getTrackIndex() != t) continue;
                float startX = trackHeaderW + pat.getStartBar() * barW - scrollXOffset;
                float clipW = pat.getLengthBars() * barW;

                if (startX + clipW < trackHeaderW || startX > getWidth()) continue;

                clipRect.set(Math.max(trackHeaderW, startX + 2 * density), top + 3 * density,
                        startX + clipW - 2 * density, top + trackH - 3 * density);

                clipPaint.setColor((pat.getColor() & 0x00FFFFFF) | 0xDD000000);
                canvas.drawRoundRect(clipRect, 4 * density, 4 * density, clipPaint);

                textPaint.setColor(ThemeColors.BG_OLED_BLACK);
                textPaint.setTextSize(10.0f * density);
                textPaint.setTextAlign(Paint.Align.LEFT);
                canvas.drawText(pat.getName(), clipRect.left + 4 * density, top + trackH / 2.0f + 3.5f * density, textPaint);
            }

            // Track Header on top of scrolling lanes
            bgPaint.setColor(ThemeColors.BG_PANEL_DARK);
            canvas.drawRect(0, top, trackHeaderW, top + trackH, bgPaint);

            textPaint.setColor(ch.getColor());
            textPaint.setTextSize(11.0f * density);
            textPaint.setTextAlign(Paint.Align.LEFT);
            canvas.drawText(ch.getName(), 6 * density, top + trackH / 2.0f - 2 * density, textPaint);

            textPaint.setColor(ch.isMuted() ? ThemeColors.ACCENT_RED : ThemeColors.TEXT_MUTED);
            textPaint.setTextSize(9.0f * density);
            canvas.drawText(ch.isMuted() ? "[MUTED]" : "[ACTIVE]", 6 * density, top + trackH / 2.0f + 12 * density, textPaint);
        }

        // Playhead indicator
        if (playheadBar >= 0.0f) {
            float playheadX = trackHeaderW + playheadBar * barW - scrollXOffset;
            if (playheadX >= trackHeaderW && playheadX <= getWidth()) {
                linePaint.setColor(ThemeColors.ACCENT_LIME);
                linePaint.setStrokeWidth(2.5f * density);
                canvas.drawLine(playheadX, 0, playheadX, getHeight(), linePaint);
            }
        }
    }
}
