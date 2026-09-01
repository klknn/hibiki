package hibiki.android.ui.components;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import hibiki.android.model.DrumPadItem;
import hibiki.android.ui.theme.ThemeColors;
import java.util.ArrayList;
import java.util.List;

/**
 * 4x2 / 4x4 velocity-sensitive drum pad matrix for live touch performance.
 */
public class DrumPadsView extends View {
    private final List<DrumPadItem> pads = new ArrayList<>();
    private int pressedIndex = -1;
    private OnPadTriggerListener listener;

    private final Paint padBgPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint borderPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint subTextPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF padRect = new RectF();

    public interface OnPadTriggerListener {
        void onPadTriggered(DrumPadItem pad);
    }

    public DrumPadsView(Context context) {
        super(context);
        init();
    }

    public DrumPadsView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    private void init() {
        padBgPaint.setStyle(Paint.Style.FILL);
        borderPaint.setStyle(Paint.Style.STROKE);
        textPaint.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        subTextPaint.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.NORMAL));
    }

    public void setPads(List<DrumPadItem> newPads) {
        pads.clear();
        if (newPads != null) {
            pads.addAll(newPads);
        }
        invalidate();
    }

    public void setOnPadTriggerListener(OnPadTriggerListener listener) {
        this.listener = listener;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int count = pads.size();
        if (count == 0) return super.onTouchEvent(event);

        int cols = 4;
        int rows = (count + cols - 1) / cols;

        float density = getResources().getDisplayMetrics().density;
        float spacing = 8.0f * density;
        float padW = (getWidth() - spacing * (cols + 1)) / cols;
        float padH = (getHeight() - spacing * (rows + 1)) / rows;

        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int actionIndex = event.getActionIndex();
                float x = event.getX(actionIndex);
                float y = event.getY(actionIndex);

                int c = (int) ((x - spacing) / (padW + spacing));
                int r = (int) ((y - spacing) / (padH + spacing));

                if (c >= 0 && c < cols && r >= 0 && r < rows) {
                    int idx = r * cols + c;
                    if (idx < count) {
                        pressedIndex = idx;
                        invalidate();
                        if (listener != null) {
                            listener.onPadTriggered(pads.get(idx));
                        }
                    }
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL:
                pressedIndex = -1;
                invalidate();
                return true;
        }
        return super.onTouchEvent(event);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int count = pads.size();
        if (count == 0) return;

        int cols = 4;
        int rows = (count + cols - 1) / cols;

        float density = getResources().getDisplayMetrics().density;
        float spacing = 8.0f * density;
        float padW = (getWidth() - spacing * (cols + 1)) / cols;
        float padH = (getHeight() - spacing * (rows + 1)) / rows;

        for (int i = 0; i < count; i++) {
            DrumPadItem pad = pads.get(i);
            int r = i / cols;
            int c = i % cols;

            float left = spacing + c * (padW + spacing);
            float top = spacing + r * (padH + spacing);
            padRect.set(left, top, left + padW, top + padH);

            boolean isPressed = (i == pressedIndex);

            // Background
            padBgPaint.setColor(isPressed ? pad.getColor() : ThemeColors.BG_PANEL_CARD);
            canvas.drawRoundRect(padRect, 8.0f * density, 8.0f * density, padBgPaint);

            // Border
            borderPaint.setStrokeWidth(1.5f * density);
            borderPaint.setColor(isPressed ? 0xFFFFFFFF : (pad.getColor() & 0x00FFFFFF) | 0x88000000);
            canvas.drawRoundRect(padRect, 8.0f * density, 8.0f * density, borderPaint);

            // Text
            subTextPaint.setColor(isPressed ? ThemeColors.BG_OLED_BLACK : ThemeColors.TEXT_MUTED);
            subTextPaint.setTextSize(9.0f * density);
            canvas.drawText("PAD " + (pad.getIndex() + 1), left + 8.0f * density, top + 16.0f * density, subTextPaint);

            textPaint.setColor(isPressed ? ThemeColors.BG_OLED_BLACK : ThemeColors.TEXT_PRIMARY);
            textPaint.setTextSize(13.0f * density);
            canvas.drawText(pad.getName(), left + 8.0f * density, top + padH - 10.0f * density, textPaint);
        }
    }
}
