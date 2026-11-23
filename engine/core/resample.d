module engine.core.resample;

import core.stdc.math : ceil, fabs, floor, round, sin, sqrt;
import engine.core.math : pi;

extern (C++, "hibiki") {

/// Modified Bessel function of the first kind, order 0 (I0), for Kaiser window.
double d_bessel_i0(double x) @nogc nothrow pure @safe {
    double sum = 1.0;
    double term = 1.0;
    double half_x = x / 2.0;
    foreach (k; 1 .. 30) {
        double r = half_x / cast(double)k;
        term *= r * r;
        sum += term;
        if (term < sum * 1e-15) {
            break;
        }
    }
    return sum;
}

/// Kaiser window parameterized by t in [-1, 1].
double d_kaiser_window(double t, double beta) @nogc nothrow {
    if (t <= -1.0 || t >= 1.0) {
        return 0.0;
    }
    return d_bessel_i0(beta * sqrt(1.0 - t * t)) / d_bessel_i0(beta);
}

/// Normalized sinc function sin(pi*x)/(pi*x).
double d_sinc(double x) @nogc nothrow {
    if (fabs(x) < 1e-12) {
        return 1.0;
    }
    double px = pi * x;
    return sin(px) / px;
}

/// Continued-fraction rational approximation a/b ~= ratio with b <= max_den.
void d_rational_approx(double ratio, int max_den, int* out_a, int* out_b) @nogc nothrow {
    if (ratio <= 0.0) {
        *out_a = 0;
        *out_b = 1;
        return;
    }
    int best_a = cast(int)round(ratio);
    int best_b = 1;
    double best_err = fabs(ratio - cast(double)best_a);

    int p0 = 0, q0 = 1;
    int p1 = 1, q1 = 0;
    double r = ratio;

    for (int i = 0; i < 50 && best_err > 1e-12; ++i) {
        int a_i = cast(int)floor(r);
        int p2 = a_i * p1 + p0;
        int q2 = a_i * q1 + q0;
        if (q2 > max_den) {
            int k = (max_den - q0) / q1;
            if (k > 0) {
                int pk = k * p1 + p0;
                int qk = k * q1 + q0;
                double err = fabs(ratio - cast(double)pk / cast(double)qk);
                if (err < best_err) {
                    best_a = pk;
                    best_b = qk;
                    best_err = err;
                }
            }
            break;
        }
        double err = fabs(ratio - cast(double)p2 / cast(double)q2);
        if (err < best_err) {
            best_a = p2;
            best_b = q2;
            best_err = err;
        }
        if (fabs(r - cast(double)a_i) < 1e-12) {
            break;
        }
        r = 1.0 / (r - cast(double)a_i);
        p0 = p1;
        q0 = q1;
        p1 = p2;
        q1 = q2;
    }
    *out_a = best_a;
    *out_b = best_b;
}

/// Builds a flattened polyphase filter bank [factor_b * filter_len] in @nogc D.
void d_build_polyphase_bank(
    int factor_b,
    int filter_radius,
    double cutoff,
    double kaiser_beta,
    float* out_bank
) @nogc nothrow {
    int filter_len = 2 * filter_radius;
    for (int p = 0; p < factor_b; ++p) {
        float* phase_row = out_bank + p * filter_len;
        double phase_offset = cast(double)p / cast(double)factor_b;
        double energy = 0.0;
        for (int k = 0; k < filter_len; ++k) {
            double t = cast(double)(k - filter_radius) + phase_offset;
            double window = d_kaiser_window(t / cast(double)filter_radius, kaiser_beta);
            double h = 2.0 * cutoff * d_sinc(2.0 * cutoff * t) * window;
            phase_row[k] = cast(float)h;
            energy += h;
        }
        if (fabs(energy) > 1e-10) {
            float inv_energy = cast(float)(1.0 / energy);
            for (int k = 0; k < filter_len; ++k) {
                phase_row[k] *= inv_energy;
            }
        }
    }
}

/// Executes polyphase FIR resampling into preallocated output buffer in @nogc D.
void d_resample_process(
    const(float)* input,
    int num_input_frames,
    int num_channels,
    int factor_a,
    int factor_b,
    int filter_radius,
    const(float)* flat_bank,
    float* output,
    size_t num_output_frames
) @nogc nothrow pure {
    int filter_len = 2 * filter_radius;
    long accum = 0;
    for (size_t m = 0; m < num_output_frames; ++m) {
        int p = cast(int)(accum % factor_b);
        int q = cast(int)(accum / factor_b);
        const(float)* phase_filter = flat_bank + p * filter_len;

        for (int ch = 0; ch < num_channels; ++ch) {
            double sum = 0.0;
            for (int k = 0; k < filter_len; ++k) {
                int src_idx = q - filter_radius + k;
                if (src_idx >= 0 && src_idx < num_input_frames) {
                    sum += phase_filter[k] * input[src_idx * num_channels + ch];
                }
            }
            output[m * cast(size_t)num_channels + cast(size_t)ch] = cast(float)sum;
        }
        accum += factor_a;
    }
}

} // extern (C++, "hibiki")
