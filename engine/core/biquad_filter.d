module engine.core.biquad_filter;

import core.stdc.math : cosf, powf, sinf, sqrtf;
import engine.core.math : clampVal, pi;

extern (C++, "hibiki") {

/// Biquad filter with LP/HP/BP/Shelf/Bell modes using RBJ Audio EQ Cookbook formulas.
/// Directly ABI-compatible with C++ `hibiki::BiquadFilter`.
extern (C++, class) struct BiquadFilter {
public:
    enum Type : int {
        OFF = -1,
        LOWPASS = 0,
        HIGHPASS = 1,
        BANDPASS = 2,
        LOW_SHELF = 3,
        HIGH_SHELF = 4,
        BELL = 5,
    }

    void setParams(Type type, float cutoff_hz, float q, float gain_db, float sample_rate) @nogc nothrow @trusted {
        if (cutoff_hz == cutoff_ && q == q_ && type == type_ && sample_rate == sr_ && gain_db == gain_db_) {
            return;
        }
        type_ = type;
        cutoff_ = cutoff_hz;
        q_ = q;
        gain_db_ = gain_db;
        sr_ = sample_rate;
        recalc();
    }

    void setModulatedCutoff(float base_hz, float depth, float env_value, float q, float gain_db, float sample_rate) @nogc nothrow @trusted {
        float mod_oct = depth * env_value * 4.0f;
        float mod_hz = base_hz * powf(2.0f, mod_oct);
        mod_hz = clampVal(mod_hz, 20.0f, 20000.0f);
        setParams(type_, mod_hz, q, gain_db, sample_rate);
    }

    float process(float x) @nogc nothrow pure @safe {
        if (type_ == Type.OFF) {
            return x;
        }
        float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = x;
        y2_ = y1_;
        y1_ = y;
        return y;
    }

    void reset() @nogc nothrow pure @safe {
        x1_ = 0.0f;
        x2_ = 0.0f;
        y1_ = 0.0f;
        y2_ = 0.0f;
    }

    float getMagnitudeSq(double cos_w, double cos_2w, double sin_w, double sin_2w) const @nogc nothrow pure @safe {
        if (type_ == Type.OFF) {
            return 1.0f;
        }
        double num_re = b0_ + b1_ * cos_w + b2_ * cos_2w;
        double num_im = -(b1_ * sin_w + b2_ * sin_2w);
        double den_re = 1.0 + a1_ * cos_w + a2_ * cos_2w;
        double den_im = -(a1_ * sin_w + a2_ * sin_2w);
        return cast(float)((num_re * num_re + num_im * num_im) / (den_re * den_re + den_im * den_im));
    }

    static float normToCutoff(float norm) @nogc nothrow {
        return 20.0f * powf(1000.0f, norm);
    }

    static float normToQ(float norm) @nogc nothrow pure @safe {
        return 0.5f + norm * 19.5f;
    }

    static Type normToType(float norm) @nogc nothrow pure @safe {
        if (norm < 0.2f) return Type.LOWPASS;
        if (norm < 0.4f) return Type.HIGHPASS;
        if (norm < 0.6f) return Type.BANDPASS;
        if (norm < 0.8f) return Type.LOW_SHELF;
        return Type.HIGH_SHELF;
    }

    static Type normToTypeV2(float norm) @nogc nothrow pure @safe {
        if (norm < 0.1f) return Type.OFF;
        if (norm < 0.3f) return Type.LOWPASS;
        if (norm < 0.5f) return Type.HIGHPASS;
        if (norm < 0.7f) return Type.LOW_SHELF;
        if (norm < 0.9f) return Type.HIGH_SHELF;
        return Type.BELL;
    }

private:
    void recalc() @nogc nothrow @trusted {
        float w0 = 2.0f * cast(float)pi * cutoff_ / sr_;
        float cw = cosf(w0);
        float sw = sinf(w0);
        float alpha = sw / (2.0f * q_);
        float A = powf(10.0f, gain_db_ / 40.0f);

        float a0 = 1.0f;
        switch (type_) {
            case Type.LOWPASS:
                b0_ = (1.0f - cw) / 2.0f;
                b1_ = 1.0f - cw;
                b2_ = (1.0f - cw) / 2.0f;
                a0 = 1.0f + alpha;
                a1_ = -2.0f * cw;
                a2_ = 1.0f - alpha;
                break;
            case Type.HIGHPASS:
                b0_ = (1.0f + cw) / 2.0f;
                b1_ = -(1.0f + cw);
                b2_ = (1.0f + cw) / 2.0f;
                a0 = 1.0f + alpha;
                a1_ = -2.0f * cw;
                a2_ = 1.0f - alpha;
                break;
            case Type.BANDPASS:
                b0_ = alpha;
                b1_ = 0.0f;
                b2_ = -alpha;
                a0 = 1.0f + alpha;
                a1_ = -2.0f * cw;
                a2_ = 1.0f - alpha;
                break;
            case Type.LOW_SHELF: {
                float sqA = sqrtf(A);
                float two_sqA_alpha = 2.0f * sqA * alpha;
                b0_ = A * ((A + 1.0f) - (A - 1.0f) * cw + two_sqA_alpha);
                b1_ = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw);
                b2_ = A * ((A + 1.0f) - (A - 1.0f) * cw - two_sqA_alpha);
                a0 = (A + 1.0f) + (A - 1.0f) * cw + two_sqA_alpha;
                a1_ = -2.0f * ((A - 1.0f) + (A + 1.0f) * cw);
                a2_ = (A + 1.0f) + (A - 1.0f) * cw - two_sqA_alpha;
                break;
            }
            case Type.HIGH_SHELF: {
                float sqA = sqrtf(A);
                float two_sqA_alpha = 2.0f * sqA * alpha;
                b0_ = A * ((A + 1.0f) + (A - 1.0f) * cw + two_sqA_alpha);
                b1_ = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw);
                b2_ = A * ((A + 1.0f) + (A - 1.0f) * cw - two_sqA_alpha);
                a0 = (A + 1.0f) - (A - 1.0f) * cw + two_sqA_alpha;
                a1_ = 2.0f * ((A - 1.0f) - (A + 1.0f) * cw);
                a2_ = (A + 1.0f) - (A - 1.0f) * cw - two_sqA_alpha;
                break;
            }
            case Type.BELL:
            default:
                b0_ = 1.0f + alpha * A;
                b1_ = -2.0f * cw;
                b2_ = 1.0f - alpha * A;
                a0 = 1.0f + alpha / A;
                a1_ = -2.0f * cw;
                a2_ = 1.0f - alpha / A;
                break;
        }
        b0_ /= a0;
        b1_ /= a0;
        b2_ /= a0;
        a1_ /= a0;
        a2_ /= a0;
    }

    Type type_ = Type.LOWPASS;
    float cutoff_ = 20000.0f;
    float q_ = 0.707f;
    float gain_db_ = 0.0f;
    float sr_ = 44100.0f;
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float x1_ = 0.0f, x2_ = 0.0f, y1_ = 0.0f, y2_ = 0.0f;
}

} // extern (C++, "hibiki")
