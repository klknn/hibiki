package hibiki.android.ui.views;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import hibiki.android.model.ChannelState;
import hibiki.android.model.TrackerCell;
import hibiki.android.ui.theme.ThemeColors;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Modern touch-first Tracker / Step Sequencer View.
 */
public class TrackerView extends LinearLayout {
    private final List<ChannelState> channels = new ArrayList<>();
    private int currentStepIndex = 0;
    private int selectedChannel = 0;
    private int selectedStep = 0;

    private TrackerMatrixView matrixView;
    private TextView infoText;
    private OnTrackerCellEditedListener cellListener;
    private OnChannelToggleListener channelToggleListener;

    public interface OnTrackerCellEditedListener {
        void onCellEdited(int channelIdx, int stepIdx, TrackerCell newCell);
    }

    public interface OnChannelToggleListener {
        void onToggleMute(int channelIdx);
        void onToggleSolo(int channelIdx);
    }

    public TrackerView(Context context) {
        super(context);
        init(context);
    }

    public TrackerView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(context);
    }

    private void init(Context context) {
        setOrientation(VERTICAL);
        setBackgroundColor(ThemeColors.BG_OLED_BLACK);
        float density = getResources().getDisplayMetrics().density;
        setPadding((int) (4 * density), (int) (4 * density), (int) (4 * density), (int) (4 * density));

        // Step Matrix Custom Canvas View
        matrixView = new TrackerMatrixView(context);
        LayoutParams matrixParams = new LayoutParams(LayoutParams.MATCH_PARENT, 0, 1.0f);
        matrixView.setLayoutParams(matrixParams);
        addView(matrixView);

        // Bottom Fast Quick Entry Bar
        LinearLayout bottomBar = new LinearLayout(context);
        bottomBar.setOrientation(HORIZONTAL);
        bottomBar.setGravity(Gravity.CENTER_VERTICAL);
        bottomBar.setBackgroundColor(ThemeColors.BG_PANEL_DARK);
        int pad = (int) (6 * density);
        bottomBar.setPadding(pad, pad, pad, pad);
        LayoutParams bottomParams = new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
        bottomParams.topMargin = (int) (4 * density);
        bottomBar.setLayoutParams(bottomParams);

        infoText = new TextView(context);
        infoText.setText("T1 [Step 00]: ···");
        infoText.setTextSize(11);
        infoText.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        infoText.setTextColor(ThemeColors.ACCENT_CYAN);
        LayoutParams infoParams = new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1.0f);
        infoText.setLayoutParams(infoParams);
        bottomBar.addView(infoText);

        Button btnAddNote = new Button(context);
        btnAddNote.setText("+ NOTE");
        btnAddNote.setTextSize(10);
        btnAddNote.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        btnAddNote.setTextColor(ThemeColors.ACCENT_LIME);
        btnAddNote.setBackgroundColor(ThemeColors.BG_PANEL_CARD);
        btnAddNote.setOnClickListener(v -> {
            if (cellListener != null && selectedChannel < channels.size()) {
                TrackerCell newCell = new TrackerCell("C-", 4, 100, selectedChannel, "00", true);
                cellListener.onCellEdited(selectedChannel, selectedStep, newCell);
            }
        });
        bottomBar.addView(btnAddNote);

        Button btnClear = new Button(context);
        btnClear.setText("CLEAR");
        btnClear.setTextSize(10);
        btnClear.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        btnClear.setTextColor(ThemeColors.ACCENT_PINK);
        btnClear.setBackgroundColor(ThemeColors.BG_PANEL_CARD);
        btnClear.setOnClickListener(v -> {
            if (cellListener != null && selectedChannel < channels.size()) {
                TrackerCell newCell = new TrackerCell("---", 4, 0, 0, "00", false);
                cellListener.onCellEdited(selectedChannel, selectedStep, newCell);
            }
        });
        bottomBar.addView(btnClear);

        addView(bottomBar);
    }

    public void setChannels(List<ChannelState> newChannels, int currentStep) {
        this.channels.clear();
        if (newChannels != null) {
            this.channels.addAll(newChannels);
        }
        this.currentStepIndex = currentStep;
        matrixView.invalidate();
        updateInfoText();
    }

    public void setCurrentStepIndex(int step) {
        this.currentStepIndex = step;
        matrixView.invalidate();
    }

    public void setOnTrackerCellEditedListener(OnTrackerCellEditedListener listener) {
        this.cellListener = listener;
    }

    public void setOnChannelToggleListener(OnChannelToggleListener listener) {
        this.channelToggleListener = listener;
    }

    private void updateInfoText() {
        if (selectedChannel < channels.size()) {
            ChannelState ch = channels.get(selectedChannel);
            TrackerCell cell = (selectedStep < ch.getSteps().size()) ? ch.getSteps().get(selectedStep) : new TrackerCell();
            infoText.setText(String.format(Locale.US, "T%d [Step %02d]: %s",
                    selectedChannel + 1, selectedStep, cell.getDisplayNote()));
        }
    }

    private class TrackerMatrixView extends View {
        private final Paint bgPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint borderPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF cellRect = new RectF();

        public TrackerMatrixView(Context context) {
            super(context);
            bgPaint.setStyle(Paint.Style.FILL);
            borderPaint.setStyle(Paint.Style.STROKE);
            textPaint.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.NORMAL));
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            if (event.getAction() == MotionEvent.ACTION_DOWN) {
                int chCount = channels.size();
                if (chCount == 0) return false;

                float density = getResources().getDisplayMetrics().density;
                float headerH = 36.0f * density;
                float stepColW = 36.0f * density;
                float chW = (getWidth() - stepColW) / chCount;
                int totalSteps = 16;
                float rowH = (getHeight() - headerH) / totalSteps;

                float x = event.getX();
                float y = event.getY();

                if (y < headerH) {
                    // Tap on Header -> select channel or toggle mute
                    int ch = (int) ((x - stepColW) / chW);
                    if (ch >= 0 && ch < chCount) {
                        selectedChannel = ch;
                        invalidate();
                        updateInfoText();
                    }
                } else {
                    // Tap on step matrix
                    int step = (int) ((y - headerH) / rowH);
                    int ch = (int) ((x - stepColW) / chW);
                    if (ch >= 0 && ch < chCount && step >= 0 && step < totalSteps) {
                        selectedChannel = ch;
                        selectedStep = step;
                        invalidate();
                        updateInfoText();
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
            float headerH = 36.0f * density;
            float stepColW = 36.0f * density;
            float chW = (getWidth() - stepColW) / chCount;
            int totalSteps = 16;
            float rowH = (getHeight() - headerH) / totalSteps;

            // Draw Header Row
            bgPaint.setColor(ThemeColors.BG_PANEL_DARK);
            canvas.drawRect(0, 0, getWidth(), headerH, bgPaint);

            textPaint.setColor(ThemeColors.TEXT_MUTED);
            textPaint.setTextSize(9.0f * density);
            textPaint.setTextAlign(Paint.Align.CENTER);
            canvas.drawText("STEP", stepColW / 2.0f, headerH / 2.0f + 3.0f * density, textPaint);

            for (int c = 0; c < chCount; c++) {
                ChannelState ch = channels.get(c);
                float left = stepColW + c * chW;
                boolean isSel = (c == selectedChannel);

                cellRect.set(left + 2 * density, 2 * density, left + chW - 2 * density, headerH - 2 * density);
                bgPaint.setColor(isSel ? ((ch.getColor() & 0x00FFFFFF) | 0x33000000) : ThemeColors.BG_PANEL_CARD);
                canvas.drawRoundRect(cellRect, 4 * density, 4 * density, bgPaint);

                if (isSel) {
                    borderPaint.setColor(ch.getColor());
                    borderPaint.setStrokeWidth(1.0f * density);
                    canvas.drawRoundRect(cellRect, 4 * density, 4 * density, borderPaint);
                }

                textPaint.setColor(isSel ? ch.getColor() : ThemeColors.TEXT_PRIMARY);
                textPaint.setTextSize(10.0f * density);
                String title = String.format(Locale.US, "T%d %s", c + 1, ch.getName().length() > 4 ? ch.getName().substring(0, 4) : ch.getName());
                canvas.drawText(title, left + chW / 2.0f, headerH / 2.0f + 3.0f * density, textPaint);
            }

            // Draw Matrix Rows
            for (int s = 0; s < totalSteps; s++) {
                float top = headerH + s * rowH;
                boolean isCurrent = (s == currentStepIndex);
                boolean isBeat = (s % 4 == 0);

                // Row background
                if (isCurrent) {
                    bgPaint.setColor((ThemeColors.ACCENT_CYAN & 0x00FFFFFF) | 0x55000000);
                } else if (isBeat) {
                    bgPaint.setColor(ThemeColors.BG_CELL_STEP_EVEN);
                } else {
                    bgPaint.setColor(ThemeColors.BG_CELL_STEP_ODD);
                }
                canvas.drawRect(0, top, getWidth(), top + rowH - 1.0f * density, bgPaint);

                // Step number
                textPaint.setColor(isCurrent ? ThemeColors.ACCENT_CYAN : (isBeat ? ThemeColors.TEXT_PRIMARY : ThemeColors.TEXT_MUTED));
                textPaint.setTextSize(10.0f * density);
                textPaint.setTextAlign(Paint.Align.CENTER);
                canvas.drawText(String.format(Locale.US, "%02d", s), stepColW / 2.0f, top + rowH / 2.0f + 3.5f * density, textPaint);

                // Cells
                for (int c = 0; c < chCount; c++) {
                    ChannelState ch = channels.get(c);
                    TrackerCell cell = (s < ch.getSteps().size()) ? ch.getSteps().get(s) : new TrackerCell();
                    float left = stepColW + c * chW;
                    boolean isSelCell = (c == selectedChannel && s == selectedStep);

                    cellRect.set(left + 2 * density, top + 1 * density, left + chW - 2 * density, top + rowH - 2 * density);

                    if (isSelCell) {
                        bgPaint.setColor((ch.getColor() & 0x00FFFFFF) | 0x44000000);
                        canvas.drawRoundRect(cellRect, 3 * density, 3 * density, bgPaint);
                        borderPaint.setColor(ch.getColor());
                        borderPaint.setStrokeWidth(1.0f * density);
                        canvas.drawRoundRect(cellRect, 3 * density, 3 * density, borderPaint);
                    } else if (cell.isActive()) {
                        bgPaint.setColor(ThemeColors.BG_CELL_ACTIVE);
                        canvas.drawRoundRect(cellRect, 3 * density, 3 * density, bgPaint);
                    }

                    // Draw note text
                    textPaint.setColor(cell.isActive() ? ch.getColor() : ThemeColors.TEXT_MUTED);
                    textPaint.setTextSize(11.0f * density);
                    textPaint.setTextAlign(Paint.Align.CENTER);
                    canvas.drawText(cell.getDisplayNote(), left + chW / 2.0f, top + rowH / 2.0f + 3.5f * density, textPaint);
                }
            }
        }
    }
}
