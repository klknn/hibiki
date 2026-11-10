module engine.instruments.builtin_3xosc;

import core.stdc.math : cos, pow, sin;
import engine.core.biquad_filter : BiquadFilter;
import engine.core.math : pi;
import engine.instruments.adsr : Adsr;

extern (C++, "hibiki") {

enum kMax3xOscVoices = 8;
enum kNum3xOsc = 3;
enum kParamsPer3xOsc = 5;
enum k3xOscParams = kNum3xOsc * kParamsPer3xOsc;
enum kTotal3xOscParams = k3xOscParams + 14;

enum ThreeXOscWaveform {
    SINE = 0,
    SAW = 1,
    SQUARE = 2,
    TRIANGLE = 3
}

enum ThreeXOscParamId {
    P_WAVEFORM = 0,
    P_COARSE = 1,
    P_FINE = 2,
    P_VOL = 3,
    P_PAN = 4,
    P_GAIN_A = 15,
    P_GAIN_D = 16,
    P_GAIN_S = 17,
    P_GAIN_R = 18,
    P_FILT_TYPE = 19,
    P_FILT_CUT = 20,
    P_FILT_RES = 21,
    P_FILT_A = 22,
    P_FILT_D = 23,
    P_FILT_S = 24,
    P_FILT_R = 25,
    P_FILT_DEPTH = 26,
    P_VOLUME = 27,
    P_ENABLE = 28
}

struct ThreeXOscMidiEvent {
    int pitch;
    float velocity;
    bool is_note_on;
}

struct ThreeXOscVoice {
    bool active = false;
    int note = -1;
    float velocity = 0.0f;
    float base_freq = 0.0f;
    double[kNum3xOsc] phase = 0.0;
    Adsr gain_env;
    Adsr filter_env;
    BiquadFilter filter_l;
    BiquadFilter filter_r;
    ulong age = 0;
}

struct ThreeXOscDspState {
    float[kTotal3xOscParams] params = 0.0f;
    ThreeXOscVoice[kMax3xOscVoices] voices;
    ulong voice_counter = 0;
    double sample_rate = 44100.0;
    bool enabled = true;

    void reset() nothrow @nogc @safe {
        foreach (i; 0 .. kTotal3xOscParams) {
            params[i] = cast(float)d_3xosc_get_default_value(cast(int)i);
        }
        enabled = true;
        foreach (ref v; voices) {
            v.active = false;
            v.note = -1;
            v.velocity = 0.0f;
            v.base_freq = 0.0f;
            v.phase[] = 0.0;
            v.gain_env = Adsr.init;
            v.filter_env = Adsr.init;
            v.filter_l.reset();
            v.filter_r.reset();
            v.age = 0;
        }
        voice_counter = 0;
    }
}

pure nothrow @nogc @safe double d_3xosc_get_default_value(int id) {
    if (id == ThreeXOscParamId.P_VOL) return 1.0;
    if (id == 5 + ThreeXOscParamId.P_VOL) return 0.0;
    if (id == 10 + ThreeXOscParamId.P_VOL) return 0.0;
    if (id % kParamsPer3xOsc == ThreeXOscParamId.P_PAN && id < k3xOscParams) return 0.5;
    if (id % kParamsPer3xOsc == ThreeXOscParamId.P_COARSE && id < k3xOscParams) return 0.5;
    if (id % kParamsPer3xOsc == ThreeXOscParamId.P_FINE && id < k3xOscParams) return 0.5;
    if (id == ThreeXOscParamId.P_GAIN_A) return 0.0;
    if (id == ThreeXOscParamId.P_GAIN_D) return 0.2;
    if (id == ThreeXOscParamId.P_GAIN_S) return 0.7;
    if (id == ThreeXOscParamId.P_GAIN_R) return 0.3;
    if (id == ThreeXOscParamId.P_FILT_CUT) return 1.0;
    if (id == ThreeXOscParamId.P_FILT_RES) return 0.0;
    if (id == ThreeXOscParamId.P_FILT_DEPTH) return 0.5;
    if (id == ThreeXOscParamId.P_VOLUME) return 0.8;
    if (id == ThreeXOscParamId.P_ENABLE) return 1.0;
    return 0.0;
}

pure nothrow @nogc @safe ThreeXOscWaveform d_3xosc_norm_to_waveform(float norm) {
    if (norm < 0.25f) return ThreeXOscWaveform.SINE;
    if (norm < 0.5f) return ThreeXOscWaveform.SAW;
    if (norm < 0.75f) return ThreeXOscWaveform.SQUARE;
    return ThreeXOscWaveform.TRIANGLE;
}

nothrow @nogc @trusted float d_3xosc_generate_osc(ThreeXOscWaveform wf, double phase) {
    float p = cast(float)phase;
    switch (wf) {
    case ThreeXOscWaveform.SINE:
        return sin(p * 6.28318530718f);
    case ThreeXOscWaveform.SAW:
        return 2.0f * p - 1.0f;
    case ThreeXOscWaveform.SQUARE:
        return p < 0.5f ? 1.0f : -1.0f;
    case ThreeXOscWaveform.TRIANGLE:
        return p < 0.5f ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
    default:
        return 0.0f;
    }
}

void d_3xosc_note_on(ref ThreeXOscDspState state, int pitch, float velocity) nothrow @nogc @safe {
    int target = -1;
    foreach (i; 0 .. kMax3xOscVoices) {
        if (!state.voices[i].active && state.voices[i].gain_env.isIdle()) {
            target = cast(int)i;
            break;
        }
    }
    if (target < 0) {
        ulong min_age = ulong.max;
        foreach (i; 0 .. kMax3xOscVoices) {
            if (state.voices[i].age < min_age) {
                min_age = state.voices[i].age;
                target = cast(int)i;
            }
        }
    }
    if (target < 0) target = 0;

    auto v = &state.voices[target];
    v.active = true;
    v.note = pitch;
    v.velocity = velocity;
    v.base_freq = 440.0f * pow(2.0f, (pitch - 69) / 12.0f);
    v.phase[] = 0.0;
    v.age = ++state.voice_counter;
    v.filter_l.reset();
    v.filter_r.reset();

    v.gain_env.setNormalized(
        state.params[ThreeXOscParamId.P_GAIN_A],
        state.params[ThreeXOscParamId.P_GAIN_D],
        state.params[ThreeXOscParamId.P_GAIN_S],
        state.params[ThreeXOscParamId.P_GAIN_R]
    );
    v.filter_env.setNormalized(
        state.params[ThreeXOscParamId.P_FILT_A],
        state.params[ThreeXOscParamId.P_FILT_D],
        state.params[ThreeXOscParamId.P_FILT_S],
        state.params[ThreeXOscParamId.P_FILT_R]
    );
    v.gain_env.noteOn();
    v.filter_env.noteOn();
}

void d_3xosc_note_off(ref ThreeXOscDspState state, int pitch) nothrow @nogc @safe {
    foreach (ref v; state.voices) {
        if (v.active && (pitch < 0 || pitch > 127 || v.note == pitch)) {
            v.gain_env.noteOff();
            v.filter_env.noteOff();
        }
    }
}

void d_3xosc_set_parameter(ref ThreeXOscDspState state, uint id, float value) nothrow @nogc @safe {
    if (id >= kTotal3xOscParams) return;
    state.params[id] = value;
    if (id == ThreeXOscParamId.P_ENABLE) state.enabled = value >= 0.5f;

    if (id >= ThreeXOscParamId.P_GAIN_A && id <= ThreeXOscParamId.P_GAIN_R) {
        foreach (ref v; state.voices) {
            v.gain_env.setNormalized(
                state.params[ThreeXOscParamId.P_GAIN_A],
                state.params[ThreeXOscParamId.P_GAIN_D],
                state.params[ThreeXOscParamId.P_GAIN_S],
                state.params[ThreeXOscParamId.P_GAIN_R]
            );
        }
    }
    if (id >= ThreeXOscParamId.P_FILT_A && id <= ThreeXOscParamId.P_FILT_R) {
        foreach (ref v; state.voices) {
            v.filter_env.setNormalized(
                state.params[ThreeXOscParamId.P_FILT_A],
                state.params[ThreeXOscParamId.P_FILT_D],
                state.params[ThreeXOscParamId.P_FILT_S],
                state.params[ThreeXOscParamId.P_FILT_R]
            );
        }
    }
}

ThreeXOscDspState* d_3xosc_create() nothrow @nogc @trusted {
    import core.stdc.stdlib : malloc;
    auto s = cast(ThreeXOscDspState*)malloc(ThreeXOscDspState.sizeof);
    if (s !is null) {
        *s = ThreeXOscDspState.init;
        s.reset();
    }
    return s;
}

void d_3xosc_destroy(ThreeXOscDspState* state) nothrow @nogc @trusted {
    import core.stdc.stdlib : free;
    if (state !is null) {
        free(state);
    }
}

void d_3xosc_reset(ThreeXOscDspState* state) nothrow @nogc @safe {
    if (state !is null) {
        state.reset();
    }
}

float d_3xosc_get_param(const(ThreeXOscDspState)* state, uint id) nothrow @nogc @safe {
    if (state !is null && id < kTotal3xOscParams) {
        return state.params[id];
    }
    return 0.0f;
}

void d_3xosc_process(
    ref ThreeXOscDspState state,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    const(ThreeXOscMidiEvent)* events,
    int num_events
) nothrow @nogc @trusted {
    state.sample_rate = sample_rate;

    foreach (e; 0 .. num_events) {
        const ev = events[e];
        if (ev.is_note_on && ev.velocity > 0) {
            d_3xosc_note_on(state, ev.pitch, ev.velocity);
        } else {
            d_3xosc_note_off(state, ev.pitch);
        }
    }

    foreach (i; 0 .. num_samples) {
        out_l[i] = 0.0f;
        out_r[i] = 0.0f;
    }

    if (!state.enabled) return;

    float master_vol = state.params[ThreeXOscParamId.P_VOLUME];
    float filt_cut_norm = state.params[ThreeXOscParamId.P_FILT_CUT];
    float filt_res_norm = state.params[ThreeXOscParamId.P_FILT_RES];
    float filt_depth = (state.params[ThreeXOscParamId.P_FILT_DEPTH] - 0.5f) * 2.0f;
    auto filt_type = BiquadFilter.normToType(state.params[ThreeXOscParamId.P_FILT_TYPE]);
    float base_cutoff = BiquadFilter.normToCutoff(filt_cut_norm);
    float filt_q = BiquadFilter.normToQ(filt_res_norm);
    float sr_f = cast(float)sample_rate;

    foreach (v_idx; 0 .. kMax3xOscVoices) {
        auto v = &state.voices[v_idx];
        if (!v.active && v.gain_env.isIdle()) continue;

        foreach (i; 0 .. num_samples) {
            float gain_val = v.gain_env.process(sr_f);
            float filt_val = v.filter_env.process(sr_f);

            if (v.gain_env.isIdle()) {
                v.active = false;
                break;
            }

            float mix_l = 0.0f;
            float mix_r = 0.0f;

            foreach (o; 0 .. kNum3xOsc) {
                int base = o * kParamsPer3xOsc;
                float osc_vol = state.params[base + ThreeXOscParamId.P_VOL];
                if (osc_vol < 0.001f) continue;

                float osc_pan = state.params[base + ThreeXOscParamId.P_PAN];
                int coarse = cast(int)((state.params[base + ThreeXOscParamId.P_COARSE] - 0.5f) * 48.0f);
                float fine = (state.params[base + ThreeXOscParamId.P_FINE] - 0.5f) * 200.0f;

                float freq = v.base_freq * pow(2.0f, (coarse + fine / 100.0f) / 12.0f);
                float sample = d_3xosc_generate_osc(
                    d_3xosc_norm_to_waveform(state.params[base + ThreeXOscParamId.P_WAVEFORM]),
                    v.phase[o]
                );

                v.phase[o] += freq / sample_rate;
                if (v.phase[o] >= 1.0) v.phase[o] -= 1.0;

                float pan_l = cos(osc_pan * 1.5708f);
                float pan_r = sin(osc_pan * 1.5708f);
                mix_l += sample * osc_vol * pan_l;
                mix_r += sample * osc_vol * pan_r;
            }

            v.filter_l.setParams(filt_type, base_cutoff, filt_q, 0.0f, sr_f);
            v.filter_r.setParams(filt_type, base_cutoff, filt_q, 0.0f, sr_f);
            v.filter_l.setModulatedCutoff(base_cutoff, filt_depth, filt_val, filt_q, 0.0f, sr_f);
            v.filter_r.setModulatedCutoff(base_cutoff, filt_depth, filt_val, filt_q, 0.0f, sr_f);

            float filt_l = v.filter_l.process(mix_l);
            float filt_r = v.filter_r.process(mix_r);

            out_l[i] += filt_l * gain_val * v.velocity * master_vol;
            out_r[i] += filt_r * gain_val * v.velocity * master_vol;
        }
    }
}

}
