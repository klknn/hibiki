module engine.core.math;

import core.stdc.math : cosf, fabsf, floorf, log10f, powf, sinf, sqrtf;

/// High-precision mathematical constant pi (matching hibiki::pi).
enum double pi = 3.14159265358979323846;

/// Clamps a value to [lo, hi] in @nogc nothrow pure @safe code.
pragma(inline, true)
T clampVal(T)(T v, T lo, T hi) pure nothrow @nogc @safe {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/// Converts decibels (dB) to linear amplitude ratio.
pragma(inline, true)
float dbToLinear(float db) nothrow @nogc @trusted {
    return powf(10.0f, db / 20.0f);
}

/// Converts linear amplitude ratio to decibels (dB), floored at -200 dB.
pragma(inline, true)
float linearToDb(float linear) nothrow @nogc @trusted {
    if (linear <= 1e-10f) {
        return -200.0f;
    }
    return 20.0f * log10f(linear);
}

/// Converts MIDI note number (0..127) to frequency in Hz (A4 = 440 Hz).
pragma(inline, true)
float midiNoteToHz(float note) nothrow @nogc @trusted {
    return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
}

/// Computes constant-power stereo pan gains (left, right) for pan in [-1, 1].
pragma(inline, true)
void constantPowerPan(float pan, out float gainL, out float gainR) nothrow @nogc @trusted {
    float p = clampVal(pan, -1.0f, 1.0f);
    float angle = (p + 1.0f) * 0.25f * cast(float)pi;
    gainL = cosf(angle);
    gainR = sinf(angle);
}
