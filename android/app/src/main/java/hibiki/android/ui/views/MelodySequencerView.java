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
import hibiki.android.engine.HibikiEngine;
import hibiki.android.model.MelodySequence;
import hibiki.android.model.MelodyStep;
import hibiki.android.ui.theme.ThemeColors;
import java.util.Locale;

/**
 * 1-Track Sine Melody Sequencer View.
 * Provides:
 * 1. 16-step beat grid (4 beats of 4 sixteenth notes) with real-time playhead scanning.
 * 2. Instant step selection and pitch/velocity parameter editing.
 * 3. Responsive touch piano keyboard with zero-latency sine audition sound.
 */
public class MelodySequencerView extends LinearLayout {

    private MelodySequence sequence;
    private int selectedStepIndex = 0;
    private int currentPlayheadStep = -1;
    private int keyboardOctave = 4;

    private StepGridView stepGridView;
    private TextView inspectorInfoText;
    private PianoKeyboardView pianoKeyboardView;
    private final Button[] octaveButtons = new Button[5]; // C2 to C6

    public MelodySequencerView(Context context) {
        super(context);
        init(context);
    }

    public MelodySequencerView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(context);
    }

    private void init(Context context) {
        setOrientation(VERTICAL);
        setBackgroundColor(ThemeColors.BG_OLED_BLACK);
        float density = getResources().getDisplayMetrics().density;
        int pad = (int) (8 * density);
        setPadding(pad, pad, pad, pad);

        sequence = MelodySequence.createDemoMelody();
        HibikiEngine.setMelodySequence(sequence);

        // 1. Top Control Strip
        buildTopStrip(context, density);

        // 2. 16-Step Beat Grid
        stepGridView = new StepGridView(context);
        LayoutParams gridParams = new LayoutParams(LayoutParams.MATCH_PARENT, (int) (220 * density));
        gridParams.topMargin = (int) (6 * density);
        gridParams.bottomMargin = (int) (6 * density);
        stepGridView.setLayoutParams(gridParams);
        addView(stepGridView);

        // 3. Step Inspector & Parameter Bar
        buildInspectorBar(context, density);

        // 4. Touch Piano Keyboard Bar (Octave Selector + Keys)
        buildKeyboardSection(context, density);

        updateInspectorText();
    }

    private void updateSequence(MelodySequence newSeq) {
        this.sequence = newSeq;
        HibikiEngine.setMelodySequence(sequence);
        stepGridView.invalidate();
        updateInspectorText();
    }

    private void previewNote(int pitch, float velocity) {
        HibikiEngine.triggerAudition(pitch, velocity);
        postDelayed(HibikiEngine::releaseAudition, 120);
    }

    private void buildTopStrip(Context context, float density) {
        LinearLayout topStrip = new LinearLayout(context);
        topStrip.setOrientation(HORIZONTAL);
        topStrip.setGravity(Gravity.CENTER_VERTICAL);
        topStrip.setLayoutParams(new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));

        TextView titleText = new TextView(context);
        titleText.setText("SINE MELODY • 16 STEPS");
        titleText.setTextSize(12);
        titleText.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        titleText.setTextColor(ThemeColors.ACCENT_CYAN);
        titleText.setLayoutParams(new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1.0f));
        topStrip.addView(titleText);

        Button btnDemo = createStyledButton(context, "DEMO", ThemeColors.BG_PANEL_CARD, ThemeColors.ACCENT_LIME, density);
        btnDemo.setOnClickListener(v -> updateSequence(MelodySequence.createDemoMelody()));
        topStrip.addView(btnDemo);

        Button btnClear = createStyledButton(context, "CLEAR", ThemeColors.BG_PANEL_CARD, ThemeColors.ACCENT_PINK, density);
        btnClear.setOnClickListener(v -> updateSequence(new MelodySequence(16)));
        topStrip.addView(btnClear);

        addView(topStrip);
    }

    private void buildInspectorBar(Context context, float density) {
        LinearLayout inspector = new LinearLayout(context);
        inspector.setOrientation(VERTICAL);
        inspector.setBackgroundColor(ThemeColors.BG_PANEL_DARK);
        inspector.setPadding((int) (8 * density), (int) (6 * density), (int) (8 * density), (int) (6 * density));
        LayoutParams inspParams = new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
        inspParams.bottomMargin = (int) (6 * density);
        inspector.setLayoutParams(inspParams);

        // Row 1: Inspector Info Text
        inspectorInfoText = new TextView(context);
        inspectorInfoText.setTextSize(11);
        inspectorInfoText.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        inspectorInfoText.setTextColor(ThemeColors.TEXT_PRIMARY);
        inspector.addView(inspectorInfoText);

        // Row 2: Action Buttons
        LinearLayout btnRow = new LinearLayout(context);
        btnRow.setOrientation(HORIZONTAL);
        btnRow.setGravity(Gravity.CENTER_VERTICAL);
        LayoutParams brParams = new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
        brParams.topMargin = (int) (4 * density);
        btnRow.setLayoutParams(brParams);

        Button btnToggle = createStyledButton(context, "ON/OFF", ThemeColors.BG_PANEL_CARD, ThemeColors.ACCENT_CYAN, density);
        btnToggle.setOnClickListener(v -> {
            MelodyStep s = sequence.getStep(selectedStepIndex);
            updateSequence(sequence.withStep(selectedStepIndex, s.withActive(!s.isActive())));
        });
        btnRow.addView(btnToggle);

        Button btnOctDown = createStyledButton(context, "OCT -", ThemeColors.BG_PANEL_CARD, ThemeColors.TEXT_PRIMARY, density);
        btnOctDown.setOnClickListener(v -> shiftStepPitch(-12));
        btnRow.addView(btnOctDown);

        Button btnOctUp = createStyledButton(context, "OCT +", ThemeColors.BG_PANEL_CARD, ThemeColors.TEXT_PRIMARY, density);
        btnOctUp.setOnClickListener(v -> shiftStepPitch(12));
        btnRow.addView(btnOctUp);

        // Velocity buttons: 50%, 80%, 100%
        float[] vels = {0.5f, 0.8f, 1.0f};
        String[] labels = {"50%", "80%", "100%"};
        for (int i = 0; i < vels.length; i++) {
            float vel = vels[i];
            Button b = createStyledButton(context, labels[i], ThemeColors.BG_PANEL_CARD,
                    i == 2 ? ThemeColors.ACCENT_AMBER : ThemeColors.TEXT_SECONDARY, density);
            b.setOnClickListener(v -> {
                MelodyStep s = sequence.getStep(selectedStepIndex);
                updateSequence(sequence.withStep(selectedStepIndex, s.withVelocity(vel)));
            });
            btnRow.addView(b);
        }

        inspector.addView(btnRow);
        addView(inspector);
    }

    private void shiftStepPitch(int delta) {
        MelodyStep s = sequence.getStep(selectedStepIndex);
        int newPitch = s.getPitch() + delta;
        if (newPitch >= 24 && newPitch <= 96) {
            updateSequence(sequence.withStep(selectedStepIndex, s.withPitch(newPitch)));
            previewNote(newPitch, s.getVelocity());
        }
    }

    private void buildKeyboardSection(Context context, float density) {
        LinearLayout octRow = new LinearLayout(context);
        octRow.setOrientation(HORIZONTAL);
        octRow.setGravity(Gravity.CENTER_VERTICAL);
        LayoutParams octParams = new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
        octParams.bottomMargin = (int) (4 * density);
        octRow.setLayoutParams(octParams);

        TextView octLabel = new TextView(context);
        octLabel.setText("KEYBOARD OCTAVE:");
        octLabel.setTextSize(10);
        octLabel.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        octLabel.setTextColor(ThemeColors.TEXT_MUTED);
        octLabel.setLayoutParams(new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1.0f));
        octRow.addView(octLabel);

        for (int oct = 2; oct <= 6; oct++) {
            final int o = oct;
            Button b = createStyledButton(context, "C" + o,
                    o == keyboardOctave ? ThemeColors.ACCENT_CYAN : ThemeColors.BG_PANEL_CARD,
                    o == keyboardOctave ? ThemeColors.BG_OLED_BLACK : ThemeColors.TEXT_PRIMARY, density);
            b.setOnClickListener(v -> selectOctave(o));
            octaveButtons[oct - 2] = b;
            octRow.addView(b);
        }
        addView(octRow);

        pianoKeyboardView = new PianoKeyboardView(context, keyboardOctave);
        pianoKeyboardView.setLayoutParams(new LayoutParams(LayoutParams.MATCH_PARENT, 0, 1.0f));
        pianoKeyboardView.setOnKeyTouchListener((midiPitch, isDown) -> {
            if (isDown) {
                HibikiEngine.triggerAudition(midiPitch, 0.85f);
                MelodyStep prev = sequence.getStep(selectedStepIndex);
                updateSequence(sequence.withStep(selectedStepIndex, prev.withPitch(midiPitch).withActive(true)));
            } else {
                HibikiEngine.releaseAudition(midiPitch);
            }
        });
        addView(pianoKeyboardView);
    }

    private void selectOctave(int oct) {
        keyboardOctave = oct;
        pianoKeyboardView.setBaseOctave(oct);
        for (int i = 0; i < octaveButtons.length; i++) {
            boolean sel = (i + 2 == oct);
            octaveButtons[i].setBackgroundColor(sel ? ThemeColors.ACCENT_CYAN : ThemeColors.BG_PANEL_CARD);
            octaveButtons[i].setTextColor(sel ? ThemeColors.BG_OLED_BLACK : ThemeColors.TEXT_PRIMARY);
        }
    }

    private void updateInspectorText() {
        MelodyStep s = sequence.getStep(selectedStepIndex);
        int beat = (selectedStepIndex / 4) + 1;
        int sub = (selectedStepIndex % 4) + 1;
        String status = s.isActive() ? "ON" : "OFF";
        String text = String.format(Locale.US,
                "STEP %02d [BEAT %d.%d]  STATUS: %s  NOTE: %s (%.1f Hz)  VEL: %d%%",
                selectedStepIndex + 1, beat, sub, status, s.getNoteName(), s.getFrequencyHz(),
                (int) (s.getVelocity() * 100));
        inspectorInfoText.setText(text);
    }

    public void setCurrentStepIndex(int stepIndex) {
        if (this.currentPlayheadStep != stepIndex) {
            this.currentPlayheadStep = stepIndex;
            stepGridView.invalidate();
        }
    }

    private Button createStyledButton(Context context, String text, int bgColor, int textColor, float density) {
        Button btn = new Button(context);
        btn.setText(text);
        btn.setTextSize(10);
        btn.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        btn.setTextColor(textColor);
        btn.setBackgroundColor(bgColor);
        btn.setPadding((int) (6 * density), 0, (int) (6 * density), 0);
        btn.setMinimumHeight(0);
        btn.setMinimumWidth(0);
        LayoutParams lp = new LayoutParams(LayoutParams.WRAP_CONTENT, (int) (30 * density));
        lp.leftMargin = (int) (3 * density);
        lp.rightMargin = (int) (3 * density);
        btn.setLayoutParams(lp);
        return btn;
    }

    /**
     * 16-Step Grid (4 beats of 4 sixteenth notes).
     */
    private class StepGridView extends View {
        private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint borderPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF rect = new RectF();

        public StepGridView(Context context) {
            super(context);
            fillPaint.setStyle(Paint.Style.FILL);
            borderPaint.setStyle(Paint.Style.STROKE);
            textPaint.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
            textPaint.setTextAlign(Paint.Align.CENTER);
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            if (event.getAction() == MotionEvent.ACTION_DOWN) {
                int col = (int) (event.getX() / (getWidth() / 4.0f));
                int row = (int) (event.getY() / (getHeight() / 4.0f));
                if (col >= 0 && col < 4 && row >= 0 && row < 4) {
                    int stepIdx = row * 4 + col;
                    MelodyStep s = sequence.getStep(stepIdx);
                    if (stepIdx == selectedStepIndex) {
                        updateSequence(sequence.withStep(stepIdx, s.withActive(!s.isActive())));
                    } else {
                        selectedStepIndex = stepIdx;
                        invalidate();
                        updateInspectorText();
                    }
                    if (sequence.getStep(stepIdx).isActive()) {
                        previewNote(s.getPitch(), s.getVelocity());
                    }
                    return true;
                }
            }
            return super.onTouchEvent(event);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            float density = getResources().getDisplayMetrics().density;
            float w = getWidth();
            float h = getHeight();
            float padW = w / 4.0f;
            float padH = h / 4.0f;
            float spacing = 2.0f * density;

            for (int step = 0; step < 16; step++) {
                int col = step % 4;
                int row = step / 4;
                float left = col * padW + spacing;
                float top = row * padH + spacing;
                float right = (col + 1) * padW - spacing;
                float bottom = (row + 1) * padH - spacing;
                rect.set(left, top, right, bottom);

                MelodyStep s = sequence.getStep(step);
                boolean isSelected = (step == selectedStepIndex);
                boolean isPlaying = (step == currentPlayheadStep);

                // Pad Background
                if (isPlaying) {
                    fillPaint.setColor(ThemeColors.ACCENT_CYAN);
                } else if (s.isActive()) {
                    fillPaint.setColor(ThemeColors.BG_CELL_ACTIVE);
                } else {
                    fillPaint.setColor((row % 2 == 0) ? ThemeColors.BG_CELL_STEP_EVEN : ThemeColors.BG_CELL_STEP_ODD);
                }
                canvas.drawRoundRect(rect, 4.0f * density, 4.0f * density, fillPaint);

                // Velocity Bar inside pad (bottom strip)
                if (s.isActive()) {
                    float velH = 4.0f * density;
                    float velWidth = (right - left - 4.0f * density) * s.getVelocity();
                    fillPaint.setColor(isPlaying ? ThemeColors.BG_OLED_BLACK : ThemeColors.ACCENT_LIME);
                    canvas.drawRect(left + 2.0f * density, bottom - velH - 2.0f * density,
                            left + 2.0f * density + velWidth, bottom - 2.0f * density, fillPaint);
                }

                // Selection Border
                if (isSelected) {
                    borderPaint.setStrokeWidth(2.5f * density);
                    borderPaint.setColor(ThemeColors.ACCENT_AMBER);
                    canvas.drawRoundRect(rect, 4.0f * density, 4.0f * density, borderPaint);
                } else if (s.isActive() && !isPlaying) {
                    borderPaint.setStrokeWidth(1.0f * density);
                    borderPaint.setColor(ThemeColors.ACCENT_CYAN);
                    canvas.drawRoundRect(rect, 4.0f * density, 4.0f * density, borderPaint);
                }

                // Text: Step number and Note Name
                float centerX = (left + right) / 2.0f;
                float centerY = (top + bottom) / 2.0f;

                // Step number (small, top-left)
                textPaint.setTextSize(9.0f * density);
                textPaint.setColor(isPlaying ? ThemeColors.BG_OLED_BLACK : ThemeColors.TEXT_MUTED);
                canvas.drawText(String.format(Locale.US, "%02d", step + 1), left + 10.0f * density, top + 12.0f * density, textPaint);

                // Note Name (bold, center)
                textPaint.setTextSize(13.0f * density);
                if (isPlaying) {
                    textPaint.setColor(ThemeColors.BG_OLED_BLACK);
                } else if (s.isActive()) {
                    textPaint.setColor(ThemeColors.ACCENT_CYAN);
                } else {
                    textPaint.setColor(ThemeColors.TEXT_MUTED);
                }
                String noteLabel = s.isActive() ? s.getNoteName() : "·";
                canvas.drawText(noteLabel, centerX, centerY + 4.0f * density, textPaint);
            }
        }
    }

    /**
     * Interactive Piano Touch Keyboard.
     */
    public interface OnKeyTouchListener {
        void onKeyTouch(int midiPitch, boolean isDown);
    }

    private static class PianoKeyboardView extends View {
        private int baseOctave;
        private int pressedKeyPitch = -1;
        private OnKeyTouchListener listener;

        private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint borderPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF keyRect = new RectF();

        // 8 white keys: C, D, E, F, G, A, B, C'
        private static final int[] WHITE_KEY_OFFSETS = { 0, 2, 4, 5, 7, 9, 11, 12 };
        private static final String[] WHITE_KEY_NAMES = { "C", "D", "E", "F", "G", "A", "B", "C" };

        // 5 black keys: C#, D#, F#, G#, A#
        // Positioned between white keys (index 0, 1, 3, 4, 5)
        private static final int[] BLACK_KEY_OFFSETS = { 1, 3, 6, 8, 10 };
        private static final int[] BLACK_KEY_SLOTS = { 0, 1, 3, 4, 5 };
        private static final String[] BLACK_KEY_NAMES = { "C#", "D#", "F#", "G#", "A#" };

        public PianoKeyboardView(Context context, int baseOctave) {
            super(context);
            this.baseOctave = baseOctave;
            fillPaint.setStyle(Paint.Style.FILL);
            borderPaint.setStyle(Paint.Style.STROKE);
            textPaint.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
            textPaint.setTextAlign(Paint.Align.CENTER);
        }

        public void setBaseOctave(int octave) {
            this.baseOctave = octave;
            invalidate();
        }

        public void setOnKeyTouchListener(OnKeyTouchListener listener) {
            this.listener = listener;
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            float density = getResources().getDisplayMetrics().density;
            float w = getWidth();
            float h = getHeight();
            int numWhiteKeys = 8;
            float whiteKeyW = w / numWhiteKeys;
            float blackKeyW = whiteKeyW * 0.65f;
            float blackKeyH = h * 0.58f;

            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_MOVE) {
                float x = event.getX();
                float y = event.getY();
                int detectedPitch = -1;

                // First check black keys (they sit on top)
                if (y <= blackKeyH) {
                    for (int i = 0; i < BLACK_KEY_SLOTS.length; i++) {
                        int slot = BLACK_KEY_SLOTS[i];
                        float bkLeft = (slot + 1) * whiteKeyW - (blackKeyW / 2.0f);
                        float bkRight = bkLeft + blackKeyW;
                        if (x >= bkLeft && x <= bkRight) {
                            detectedPitch = MelodyStep.octaveAndOffsetToPitch(baseOctave, BLACK_KEY_OFFSETS[i]);
                            break;
                        }
                    }
                }

                // If not black key, check white keys
                if (detectedPitch == -1) {
                    int wkIdx = (int) (x / whiteKeyW);
                    if (wkIdx >= 0 && wkIdx < numWhiteKeys) {
                        detectedPitch = MelodyStep.octaveAndOffsetToPitch(baseOctave, WHITE_KEY_OFFSETS[wkIdx]);
                    }
                }

                if (detectedPitch != -1 && detectedPitch != pressedKeyPitch) {
                    pressedKeyPitch = detectedPitch;
                    invalidate();
                    if (listener != null) {
                        listener.onKeyTouch(pressedKeyPitch, true);
                    }
                }
                return true;
            } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                if (pressedKeyPitch != -1) {
                    if (listener != null) {
                        listener.onKeyTouch(pressedKeyPitch, false);
                    }
                    pressedKeyPitch = -1;
                    invalidate();
                }
                return true;
            }
            return super.onTouchEvent(event);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            float density = getResources().getDisplayMetrics().density;
            float w = getWidth();
            float h = getHeight();
            int numWhiteKeys = 8;
            float whiteKeyW = w / numWhiteKeys;
            float blackKeyW = whiteKeyW * 0.65f;
            float blackKeyH = h * 0.58f;

            // 1. Draw White Keys
            for (int i = 0; i < numWhiteKeys; i++) {
                int pitch = MelodyStep.octaveAndOffsetToPitch(baseOctave, WHITE_KEY_OFFSETS[i]);
                boolean isPressed = (pitch == pressedKeyPitch);
                float left = i * whiteKeyW;
                float right = (i + 1) * whiteKeyW;
                keyRect.set(left + 1.0f, 0, right - 1.0f, h - 2.0f);

                fillPaint.setColor(isPressed ? ThemeColors.ACCENT_CYAN : 0xFFEEEEEE);
                canvas.drawRoundRect(keyRect, 4.0f * density, 4.0f * density, fillPaint);
                borderPaint.setStrokeWidth(1.0f * density);
                borderPaint.setColor(0xFF33384B);
                canvas.drawRoundRect(keyRect, 4.0f * density, 4.0f * density, borderPaint);

                // Key label at bottom
                textPaint.setColor(isPressed ? ThemeColors.BG_OLED_BLACK : 0xFF222530);
                textPaint.setTextSize(11.0f * density);
                String label = WHITE_KEY_NAMES[i] + (i == 7 ? (baseOctave + 1) : baseOctave);
                canvas.drawText(label, (left + right) / 2.0f, h - 8.0f * density, textPaint);
            }

            // 2. Draw Black Keys
            for (int i = 0; i < BLACK_KEY_SLOTS.length; i++) {
                int slot = BLACK_KEY_SLOTS[i];
                int pitch = MelodyStep.octaveAndOffsetToPitch(baseOctave, BLACK_KEY_OFFSETS[i]);
                boolean isPressed = (pitch == pressedKeyPitch);
                float left = (slot + 1) * whiteKeyW - (blackKeyW / 2.0f);
                float right = left + blackKeyW;
                keyRect.set(left, 0, right, blackKeyH);

                fillPaint.setColor(isPressed ? ThemeColors.ACCENT_CYAN : 0xFF1E212E);
                canvas.drawRoundRect(keyRect, 3.0f * density, 3.0f * density, fillPaint);
                borderPaint.setStrokeWidth(1.5f * density);
                borderPaint.setColor(0xFF11131A);
                canvas.drawRoundRect(keyRect, 3.0f * density, 3.0f * density, borderPaint);

                textPaint.setColor(isPressed ? ThemeColors.BG_OLED_BLACK : 0xFFA0A5B5);
                textPaint.setTextSize(9.0f * density);
                canvas.drawText(BLACK_KEY_NAMES[i], (left + right) / 2.0f, blackKeyH - 8.0f * density, textPaint);
            }
        }
    }
}
