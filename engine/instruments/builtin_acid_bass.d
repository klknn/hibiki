module engine.instruments.builtin_acid_bass;

import core.stdc.math : exp, pow, round, tan, tanh;
import engine.core.math : pi;

extern (C++, "hibiki") {

enum kMaxAcidNoteStack = 32;

struct AcidMidiEvent {
    int pitch;
    float velocity;
    bool is_note_on;
}

struct AcidBassDspState {
    int[kMaxAcidNoteStack] note_stack = 0;
    int note_stack_count = 0;
    bool note_active = false;
    int current_pitch = -1;
    double target_freq = 0.0;
    double current_freq = 0.0;

    double osc_phase = 0.0;
    double env_val = 0.0;
    double amp_gain = 0.0;
    bool accent_active = false;

    double s1 = 0.0;
    double s2 = 0.0;
    double s3 = 0.0;
    double s4 = 0.0;

    void reset() pure nothrow @nogc @safe {
        note_stack[] = 0;
        note_stack_count = 0;
        note_active = false;
        current_pitch = -1;
        target_freq = 0.0;
        current_freq = 0.0;
        osc_phase = 0.0;
        env_val = 0.0;
        amp_gain = 0.0;
        accent_active = false;
        s1 = s2 = s3 = s4 = 0.0;
    }
}

pure nothrow @nogc @safe double d_polyblep(double t, double dt) {
    if (t < dt) {
        double x = t / dt;
        return x + x - x * x - 1.0;
    } else if (t > 1.0 - dt) {
        double x = (t - 1.0) / dt;
        return x + x + x * x + 1.0;
    }
    return 0.0;
}

nothrow @nogc @trusted float d_acid_norm_to_cutoff_hz(double norm) {
    return cast(float)(100.0 * pow(30.0, norm));
}

nothrow @nogc @trusted float d_acid_norm_to_decay_s(double norm) {
    return cast(float)(0.05 * pow(60.0, norm));
}

void d_acid_bass_process(
    ref AcidBassDspState state,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    const(AcidMidiEvent)* events,
    int num_events,
    double waveform_norm,
    double cutoff_norm,
    double resonance_norm,
    double env_mod_norm,
    double decay_norm,
    double accent_norm,
    double overdrive_norm,
    double volume_norm,
    double transpose_norm,
    double slide_norm,
    double accent_switch_norm
) nothrow @nogc @trusted {
    int transpose_octaves = cast(int)round(transpose_norm * 4.0) - 2;
    int transpose_semitones = transpose_octaves * 12;

    // Process MIDI events
    foreach (e; 0 .. num_events) {
        const ev = events[e];
        int pitch = ev.pitch + transpose_semitones;
        if (pitch < 0) pitch = 0;
        if (pitch > 127) pitch = 127;

        if (ev.is_note_on && ev.velocity > 0) {
            // Find in note stack
            int found_idx = -1;
            foreach (i; 0 .. state.note_stack_count) {
                if (state.note_stack[i] == pitch) {
                    found_idx = cast(int)i;
                    break;
                }
            }
            if (found_idx >= 0) {
                foreach (i; found_idx .. state.note_stack_count - 1) {
                    state.note_stack[i] = state.note_stack[i + 1];
                }
                state.note_stack_count--;
            }
            if (state.note_stack_count < kMaxAcidNoteStack) {
                state.note_stack[state.note_stack_count++] = pitch;
            }

            double note_freq = 440.0 * pow(2.0, (pitch - 69.0) / 12.0);
            state.target_freq = note_freq;
            state.current_pitch = pitch;

            if (state.note_stack_count == 1) {
                state.current_freq = note_freq;
                state.env_val = 1.0;
                state.note_active = true;
            } else {
                bool slide_on = slide_norm >= 0.5;
                if (!slide_on) {
                    state.current_freq = note_freq;
                }
            }
            state.accent_active = (ev.velocity > 0.8f) || (accent_switch_norm >= 0.5);
        } else {
            // Note off
            int found_idx = -1;
            foreach (i; 0 .. state.note_stack_count) {
                if (state.note_stack[i] == pitch) {
                    found_idx = cast(int)i;
                    break;
                }
            }
            if (found_idx >= 0) {
                foreach (i; found_idx .. state.note_stack_count - 1) {
                    state.note_stack[i] = state.note_stack[i + 1];
                }
                state.note_stack_count--;
            }

            if (state.note_stack_count == 0) {
                state.note_active = false;
                state.current_pitch = -1;
                state.accent_active = false;
            } else {
                int prev_pitch = state.note_stack[state.note_stack_count - 1];
                double note_freq = 440.0 * pow(2.0, (prev_pitch - 69.0) / 12.0);
                state.target_freq = note_freq;
                state.current_pitch = prev_pitch;

                bool slide_on = slide_norm >= 0.5;
                if (!slide_on) {
                    state.current_freq = note_freq;
                }
            }
        }
    }

    if (state.note_active) {
        if (accent_switch_norm >= 0.5) {
            state.accent_active = true;
        }
    } else {
        state.accent_active = false;
    }

    foreach (i; 0 .. num_samples) {
        out_l[i] = 0.0f;
        out_r[i] = 0.0f;
    }

    double master_vol = volume_norm;
    double resonance = resonance_norm;
    double env_mod = env_mod_norm;
    double overdrive = overdrive_norm;
    bool is_square = waveform_norm >= 0.5;

    double cutoff_hz = d_acid_norm_to_cutoff_hz(cutoff_norm);
    double decay_s = d_acid_norm_to_decay_s(decay_norm);

    if (state.accent_active) {
        decay_s *= 0.5;
        if (decay_s < 0.05) decay_s = 0.05;
        if (decay_s > 3.0) decay_s = 3.0;
    }

    double env_decay_rate = exp(-1.0 / (decay_s * sample_rate));
    double amp_attack_coeff = 1.0 - exp(-1.0 / (0.002 * sample_rate));
    double amp_release_coeff = 1.0 - exp(-1.0 / (0.08 * sample_rate));
    double glide_coeff = 1.0 - exp(-1.0 / (0.08 * sample_rate));
    double res_fb = resonance * 3.8;

    foreach (i; 0 .. num_samples) {
        if (state.note_active) {
            bool slide_on = slide_norm >= 0.5;
            if (slide_on) {
                state.current_freq += (state.target_freq - state.current_freq) * glide_coeff;
            } else {
                state.current_freq = state.target_freq;
            }
        }

        double freq = state.current_freq;
        if (freq < 1.0) freq = 1.0;

        double dt = freq / sample_rate;
        state.osc_phase += dt;
        if (state.osc_phase >= 1.0) {
            state.osc_phase -= 1.0;
        }

        double saw = 2.0 * state.osc_phase - 1.0 - d_polyblep(state.osc_phase, dt);
        double osc_out = saw;
        if (is_square) {
            double phase_offset = state.osc_phase + 0.5;
            if (phase_offset >= 1.0) phase_offset -= 1.0;
            double saw_offset = 2.0 * phase_offset - 1.0 - d_polyblep(phase_offset, dt);
            osc_out = saw - saw_offset;
        }

        state.env_val *= env_decay_rate;

        double target_amp = state.note_active ? 1.0 : 0.0;
        if (state.note_active) {
            state.amp_gain += (target_amp - state.amp_gain) * amp_attack_coeff;
        } else {
            state.amp_gain += (target_amp - state.amp_gain) * amp_release_coeff;
        }

        if (state.amp_gain < 0.0001 && !state.note_active) {
            state.amp_gain = 0.0;
            continue;
        }

        double mod_depth_hz = env_mod * 4000.0;
        if (state.accent_active) {
            mod_depth_hz *= 1.4;
        }
        double current_cutoff = cutoff_hz + state.env_val * mod_depth_hz;
        if (current_cutoff < 40.0) current_cutoff = 40.0;
        if (current_cutoff > 18000.0) current_cutoff = 18000.0;

        double x = osc_out;
        foreach (step; 0 .. 2) {
            double g = tan(pi * current_cutoff / (2.0 * sample_rate));
            if (g < 0.0) g = 0.0;
            if (g > 0.999) g = 0.999;

            double fb = state.s4 * res_fb;
            double input_fb = x - tanh(fb);

            double v1 = (input_fb - state.s1) * g / (1.0 + g);
            double o1 = state.s1 + v1;
            state.s1 = o1 + v1;

            double v2 = (o1 - state.s2) * g / (1.0 + g);
            double o2 = state.s2 + v2;
            state.s2 = o2 + v2;

            double v3 = (o2 - state.s3) * g / (1.0 + g);
            double o3 = state.s3 + v3;
            state.s3 = o3 + v3;

            double v4 = (o3 - state.s4) * g / (1.0 + g);
            double o4 = state.s4 + v4;
            state.s4 = o4 + v4;
        }

        double filter_out = state.s4;
        double drive = overdrive * 6.0;
        double sat_in = filter_out * (1.0 + drive);
        if (state.accent_active) {
            sat_in *= 1.3;
        }
        double saturated = tanh(sat_in);

        double output_val = saturated * state.amp_gain * master_vol;
        if (state.accent_active) {
            output_val *= 1.2;
        }

        out_l[i] = cast(float)output_val;
        out_r[i] = cast(float)output_val;
    }
}

}
