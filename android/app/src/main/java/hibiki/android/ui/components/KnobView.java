package hibiki.android.ui.components;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import hibiki.android.ui.theme.ThemeColors;
import java.util.Locale;

/**
 * Modern rotary knob UI component for parameter manipulation.
 */
public class KnobView extends View {
    private String name = "KNOB";
    private float value = 0.5f; // 0.0f to 1.0f
    private int accentColor = ThemeColors.ACCENT_CYAN;
    private OnValueChangeListener listener;

    private final Paint arcPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF arcBounds = new RectF();
    private float lastTouchY;

    public interface OnValueChangeListener {
        void onValueChanged(float newValue);
    }

    public KnobView(Context context) {
        super(context);
        init();
    }

    public KnobView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    private void init() {
        arcPaint.setStyle(Paint.Style.STROKE);
        arcPaint.setStrokeCap(Paint.Cap.ROUND);
        textPaint.setTextAlign(Paint.Align.CENTER);
        textPaint.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
    }

    public void setParams(String name, float value, int accentColor) {
        this.name = name;
        this.value = Math.max(0.0f, Math.min(1.0f, value));
        this.accentColor = accentColor;
        invalidate();
    }

    public void setValue(float value) {
        this.value = Math.max(0.0f, Math.min(1.0f, value));
        invalidate();
    }

    public float getValue() {
        return value;
    }

    public void setOnValueChangeListener(OnValueChangeListener listener) {
        this.listener = listener;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        switch (event.getAction()) {
            case MotionEvent.ACTION_DOWN:
                lastTouchY = event.getY();
                getParent().requestDisallowInterceptTouchEvent(true);
                return true;
            case MotionEvent.ACTION_MOVE:
                float dy = lastTouchY - event.getY();
                lastTouchY = event.getY();
                float delta = dy / (getHeight() * 0.8f);
                float newVal = Math.max(0.0f, Math.min(1.0f, value + delta));
                if (Math.abs(newVal - value) > 0.0001f) {
                    value = newVal;
                    invalidate();
                    if (listener != null) {
                        listener.onValueChanged(value);
                    }
                }
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                getParent().requestDisallowInterceptTouchEvent(false);
                return true;
        }
        return super.onTouchEvent(event);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int width = getWidth();
        int height = getHeight();
        if (width <= 0 || height <= 0) return;

        float density = getResources().getDisplayMetrics().density;
        float strokeWidth = 4.0f * density;
        float padding = 8.0f * density;
        float diameter = Math.min(width - padding * 2, height - padding * 2 - 20 * density);
        float radius = diameter / 2.0f;
        float cx = width / 2.0f;
        float cy = padding + radius;

        arcBounds.set(cx - radius + strokeWidth / 2f, cy - radius + strokeWidth / 2f,
                cx + radius - strokeWidth / 2f, cy + radius - strokeWidth / 2f);

        // Background Track Arc (270 degrees total, starts at 135)
        arcPaint.setStrokeWidth(strokeWidth);
        arcPaint.setColor(ThemeColors.BG_PANEL_CARD);
        canvas.drawArc(arcBounds, 135f, 270f, false, arcPaint);

        // Active Arc
        float sweep = value * 270f;
        if (sweep > 1.0f) {
            arcPaint.setColor(accentColor);
            canvas.drawArc(arcBounds, 135f, sweep, false, arcPaint);
        }

        // Indicator dot
        double angleRad = Math.toRadians(135.0 + sweep);
        float indicatorRadius = radius - strokeWidth;
        float dotX = (float) (cx + indicatorRadius * Math.cos(angleRad));
        float dotY = (float) (cy + indicatorRadius * Math.sin(angleRad));
        Paint dotPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        dotPaint.setStyle(Paint.Style.FILL);
        dotPaint.setColor(ThemeColors.TEXT_PRIMARY);
        canvas.drawCircle(dotX, dotY, 3.0f * density, dotPaint);

        // Name text
        textPaint.setColor(ThemeColors.TEXT_SECONDARY);
        textPaint.setTextSize(9.0f * density);
        canvas.drawText(name, cx, cy + radius + 10 * density, textPaint);

        // Value text
        textPaint.setColor(accentColor);
        textPaint.setTextSize(9.0f * density);
        String valText = String.format(Locale.US, "%d%%", (int) (value * 100));
        canvas.drawText(valText, cx, cy + radius + 20 * density, textPaint);
    }
}
