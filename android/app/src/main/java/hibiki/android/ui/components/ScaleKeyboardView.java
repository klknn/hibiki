package hibiki.android.ui.components;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import hibiki.android.model.ScaleType;
import hibiki.android.ui.theme.ThemeColors;
import java.util.ArrayList;
import java.util.List;

/**
 * Scale-locked touch keyboard view preventing accidental wrong notes.
 */
public class ScaleKeyboardView extends View {
    private static final String[] NOTE_NAMES = new String[] {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    private int rootNoteIndex = 0; // C
    private ScaleType scaleType = ScaleType.PENTATONIC_MINOR;
    private int octave = 4;
    private int keyColor = ThemeColors.ACCENT_PINK;
    private int pressedKeyIndex = -1;

    private final List<KeyNote> scaleNotes = new ArrayList<>();
    private OnNoteTriggerListener listener;

    private final Paint keyBgPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint borderPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF keyRect = new RectF();

    public static class KeyNote {
        public final int midiNote;
        public final String name;
        public final boolean isRoot;

        public KeyNote(int midiNote, String name, boolean isRoot) {
            this.midiNote = midiNote;
            this.name = name;
            this.isRoot = isRoot;
        }
    }

    public interface OnNoteTriggerListener {
        void onNoteTriggered(int midiNote, String noteName);
    }

    public ScaleKeyboardView(Context context) {
        super(context);
        init();
    }

    public ScaleKeyboardView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    private void init() {
        keyBgPaint.setStyle(Paint.Style.FILL);
        borderPaint.setStyle(Paint.Style.STROKE);
        textPaint.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        textPaint.setTextAlign(Paint.Align.CENTER);
        recomputeScaleNotes();
    }

    public void setScaleConfig(int rootNoteIndex, ScaleType scaleType, int octave, int keyColor) {
        this.rootNoteIndex = rootNoteIndex;
        this.scaleType = scaleType;
        this.octave = octave;
        this.keyColor = keyColor;
        recomputeScaleNotes();
        invalidate();
    }

    public void setOnNoteTriggerListener(OnNoteTriggerListener listener) {
        this.listener = listener;
    }

    private void recomputeScaleNotes() {
        scaleNotes.clear();
        int[] intervals = scaleType.getIntervals();
        for (int oct = octave; oct <= octave + 1; oct++) {
            for (int interval : intervals) {
                int noteVal = (rootNoteIndex + interval) % 12;
                int midiNum = (oct * 12) + noteVal;
                String noteName = NOTE_NAMES[noteVal] + oct;
                boolean isRoot = noteVal == (rootNoteIndex % 12);
                scaleNotes.add(new KeyNote(midiNum, noteName, isRoot));
            }
        }
        while (scaleNotes.size() > 12) {
            scaleNotes.remove(scaleNotes.size() - 1);
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int count = scaleNotes.size();
        if (count == 0) return super.onTouchEvent(event);

        float density = getResources().getDisplayMetrics().density;
        float spacing = 3.0f * density;
        float keyW = (getWidth() - spacing * (count + 1)) / count;

        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int actionIndex = event.getActionIndex();
                float x = event.getX(actionIndex);
                int idx = (int) ((x - spacing) / (keyW + spacing));
                if (idx >= 0 && idx < count) {
                    pressedKeyIndex = idx;
                    invalidate();
                    if (listener != null) {
                        KeyNote note = scaleNotes.get(idx);
                        listener.onNoteTriggered(note.midiNote, note.name);
                    }
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL:
                pressedKeyIndex = -1;
                invalidate();
                return true;
        }
        return super.onTouchEvent(event);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int count = scaleNotes.size();
        if (count == 0) return;

        float density = getResources().getDisplayMetrics().density;
        float spacing = 3.0f * density;
        float keyW = (getWidth() - spacing * (count + 1)) / count;
        float keyH = getHeight() - spacing * 2.0f;

        for (int i = 0; i < count; i++) {
            KeyNote key = scaleNotes.get(i);
            float left = spacing + i * (keyW + spacing);
            float top = spacing;
            keyRect.set(left, top, left + keyW, top + keyH);

            boolean isPressed = (i == pressedKeyIndex);

            // Background
            if (isPressed) {
                keyBgPaint.setColor(keyColor);
            } else if (key.isRoot) {
                keyBgPaint.setColor((keyColor & 0x00FFFFFF) | 0x44000000);
            } else {
                keyBgPaint.setColor(ThemeColors.BG_PANEL_CARD);
            }
            canvas.drawRoundRect(keyRect, 6.0f * density, 6.0f * density, keyBgPaint);

            // Border
            if (key.isRoot && !isPressed) {
                borderPaint.setStrokeWidth(1.0f * density);
                borderPaint.setColor(keyColor);
                canvas.drawRoundRect(keyRect, 6.0f * density, 6.0f * density, borderPaint);
            }

            // Note Text
            if (isPressed) {
                textPaint.setColor(ThemeColors.BG_OLED_BLACK);
            } else if (key.isRoot) {
                textPaint.setColor(keyColor);
            } else {
                textPaint.setColor(ThemeColors.TEXT_PRIMARY);
            }
            textPaint.setTextSize(10.0f * density);
            canvas.drawText(key.name, left + keyW / 2.0f, top + keyH - 8.0f * density, textPaint);
        }
    }
}
