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
 * Vertical touch fader component for mixer channel strips.
 */
public class TouchFaderView extends View {
    private float value = 0.8f; // 0.0f to 1.0f
    private int faderColor = ThemeColors.ACCENT_CYAN;
    private OnValueChangeListener listener;

    private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint capPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint linePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF slotRect = new RectF();
    private final RectF capRect = new RectF();
    private float lastTouchY;

    public interface OnValueChangeListener {
        void onValueChanged(float newValue);
    }

    public TouchFaderView(Context context) {
        super(context);
        init();
    }

    public TouchFaderView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    private void init() {
        fillPaint.setStyle(Paint.Style.FILL);
        capPaint.setStyle(Paint.Style.FILL);
        linePaint.setStyle(Paint.Style.STROKE);
        textPaint.setTextAlign(Paint.Align.CENTER);
        textPaint.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
    }

    public void setParams(float value, int color) {
        this.value = Math.max(0.0f, Math.min(1.0f, value));
        this.faderColor = color;
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
                float delta = dy / (getHeight() * 0.7f);
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
        float textHeight = 16.0f * density;
        float faderAreaHeight = height - textHeight;

        float slotWidth = 24.0f * density;
        float slotLeft = (width - slotWidth) / 2.0f;
        float slotTop = 4.0f * density;
        float slotBottom = faderAreaHeight - 4.0f * density;
        float totalTrackH = slotBottom - slotTop;

        // Background slot
        slotRect.set(slotLeft, slotTop, slotLeft + slotWidth, slotBottom);
        fillPaint.setColor(ThemeColors.BG_PANEL_CARD);
        canvas.drawRoundRect(slotRect, 4.0f * density, 4.0f * density, fillPaint);

        // Center groove
        fillPaint.setColor(ThemeColors.BG_OLED_BLACK);
        float grooveW = 4.0f * density;
        canvas.drawRect(width / 2.0f - grooveW / 2.0f, slotTop + 4.0f * density,
                width / 2.0f + grooveW / 2.0f, slotBottom - 4.0f * density, fillPaint);

        // Level fill
        float filledH = totalTrackH * value;
        if (filledH > 2.0f) {
            int alphaColor = (faderColor & 0x00FFFFFF) | 0x44000000;
            fillPaint.setColor(alphaColor);
            RectF fillRect = new RectF(slotLeft, slotBottom - filledH, slotLeft + slotWidth, slotBottom);
            canvas.drawRoundRect(fillRect, 4.0f * density, 4.0f * density, fillPaint);
        }

        // Cap Handle
        float capH = 16.0f * density;
        float capW = slotWidth - 4.0f * density;
        float capTop = slotBottom - (totalTrackH * value) - (capH / 2.0f);
        capTop = Math.max(slotTop, Math.min(slotBottom - capH, capTop));

        capRect.set((width - capW) / 2.0f, capTop, (width + capW) / 2.0f, capTop + capH);
        capPaint.setColor(faderColor);
        canvas.drawRoundRect(capRect, 3.0f * density, 3.0f * density, capPaint);

        // Center notch on handle
        linePaint.setColor(ThemeColors.BG_OLED_BLACK);
        linePaint.setStrokeWidth(2.0f * density);
        float notchY = capTop + capH / 2.0f;
        canvas.drawLine(capRect.left + 4.0f * density, notchY, capRect.right - 4.0f * density, notchY, linePaint);

        // dB Text readout
        textPaint.setColor(ThemeColors.TEXT_SECONDARY);
        textPaint.setTextSize(9.0f * density);
        String dbStr;
        if (value <= 0.001f) {
            dbStr = "-inf";
        } else {
            double db = 20.0 * Math.log10(value);
            dbStr = String.format(Locale.US, "%.1f dB", db);
        }
        canvas.drawText(dbStr, width / 2.0f, height - 4.0f * density, textPaint);
    }
}
