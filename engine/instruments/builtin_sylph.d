module engine.instruments.builtin_sylph;

import core.stdc.math : cosf, expf, fabsf, powf, sinf, tanf, tanhf;
import engine.core.math : clampVal, midiNoteToHz, pi;
import engine.instruments.adsr : Adsr;

extern (C++, "hibiki") {

enum kSylphMaxVoices = 8;
enum kSylphMaxUnison = 7;
enum kSylphDelayMax = 49152;
enum kSylphChorusMax = 4096;
enum kSylphNumParams = 46;
enum kSylphNumPresets = 6;

enum SylphWaveform {
    SAW = 0,
    SQUARE = 1,
    TRIANGLE = 2,
    SINE = 3,
}

enum SylphPlayMode {
    POLY = 0,
    MONO = 1,
    LEGATO = 2,
}

enum SylphFilterType {
    LP24 = 0,
    LP12 = 1,
    BP12 = 2,
    HP12 = 3,
}

enum SylphParamId {
    P_OSC1_WAVE = 0,
    P_OSC1_OCT = 1,
    P_OSC1_SEMI = 2,
    P_OSC1_UNISON = 3,
    P_OSC1_DETUNE = 4,
    P_OSC1_STEREO = 5,
    P_OSC1_RETRIG = 6,
    P_OSC1_VOL = 7,

    P_OSC2_WAVE = 8,
    P_OSC2_OCT = 9,
    P_OSC2_SEMI = 10,
    P_OSC2_UNISON = 11,
    P_OSC2_DETUNE = 12,
    P_OSC2_STEREO = 13,
    P_OSC2_RETRIG = 14,
    P_OSC2_VOL = 15,

    P_SUB_WAVE = 16,
    P_SUB_OCT = 17,
    P_SUB_VOL = 18,
    P_CLICK_NOISE = 19,
    P_PITCH_AMT = 20,
    P_PITCH_DECAY = 21,
    P_PITCH_CURVE = 22,

    P_AMP_A = 23,
    P_AMP_D = 24,
    P_AMP_S = 25,
    P_AMP_R = 26,
    P_PLAY_MODE = 27,

    P_FILT_TYPE = 28,
    P_FILT_CUT = 29,
    P_FILT_RES = 30,
    P_FILT_ENV_AMT = 31,
    P_FILT_A = 32,
    P_FILT_D = 33,
    P_FILT_S = 34,
    P_FILT_R = 35,

    P_DRIVE = 36,
    P_LFO_RATE = 37,
    P_LFO_PITCH = 38,
    P_LFO_CUT = 39,
    P_CHORUS_MIX = 40,
    P_DELAY_MIX = 41,
    P_DELAY_TIME = 42,
    P_REVERB_MIX = 43,
    P_GLIDE = 44,
    P_VOLUME = 45,
}

struct SylphMidiEvent {
    int pitch;
    float velocity;
    bool is_note_on;
    float pan = 0.0f;
}

/// Zero-delay-feedback (Cytomic TPT) state-variable filter stage.
struct SylphZdfSvf {
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;

    void reset() nothrow @nogc @safe {
        ic1eq = 0.0f;
        ic2eq = 0.0f;
    }

    /// Evaluates one filter step; `mode` selects LP (0), BP (1), or HP (2).
    float process(float v0, float g, float k, int mode) nothrow @nogc @safe {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = v0 - ic2eq;
        const float v1 = a1 * ic1eq + a2 * v3;
        const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        if (mode == 2) return v0 - k * v1 - v2; // HP
        if (mode == 1) return v1;                 // BP
        return v2;                                // LP
    }
}

struct SylphVoice {
    bool active = false;
    bool held = false;
    int note = -1;
    float velocity = 0.0f;
    float pan = 0.0f;
    float cur_freq = 440.0f;
    float target_freq = 440.0f;
    double[kSylphMaxUnison] osc1_phase = 0.0;
    double[kSylphMaxUnison] osc2_phase = 0.0;
    double sub_phase = 0.0;
    float pitch_env = 0.0f;
    float click_env = 0.0f;
    Adsr amp_env;
    Adsr filt_env;
    SylphZdfSvf filt1_l;
    SylphZdfSvf filt2_l;
    SylphZdfSvf filt1_r;
    SylphZdfSvf filt2_r;
    ulong age = 0;
}

/// Fixed-size Schroeder stereo reverb + chorus + ping-pong delay FX state.
struct SylphFxState {
    float[kSylphChorusMax] chorus_l = 0.0f;
    float[kSylphChorusMax] chorus_r = 0.0f;
    int chorus_write = 0;
    double chorus_lfo = 0.0;

    float[kSylphDelayMax] delay_l = 0.0f;
    float[kSylphDelayMax] delay_r = 0.0f;
    int delay_write = 0;

    // 4 parallel stereo combs + 2 series allpasses per side (prime tap lengths at 44.1kHz)
    float[1557] c1_l = 0.0f;
    float[1617] c2_l = 0.0f;
    float[1491] c3_l = 0.0f;
    float[1422] c4_l = 0.0f;
    float[1580] c1_r = 0.0f;
    float[1640] c2_r = 0.0f;
    float[1514] c3_r = 0.0f;
    float[1445] c4_r = 0.0f;
    float[556] a1_l = 0.0f;
    float[441] a2_l = 0.0f;
    float[579] a1_r = 0.0f;
    float[464] a2_r = 0.0f;
    int[4] comb_idx = 0;
    int[4] comb_idx_r = 0;
    int[2] ap_idx = 0;
    int[2] ap_idx_r = 0;

    void reset() nothrow @nogc @safe {
        chorus_l[] = 0.0f;
        chorus_r[] = 0.0f;
        chorus_write = 0;
        chorus_lfo = 0.0;
        delay_l[] = 0.0f;
        delay_r[] = 0.0f;
        delay_write = 0;
        c1_l[] = 0.0f;
        c2_l[] = 0.0f;
        c3_l[] = 0.0f;
        c4_l[] = 0.0f;
        c1_r[] = 0.0f;
        c2_r[] = 0.0f;
        c3_r[] = 0.0f;
        c4_r[] = 0.0f;
        a1_l[] = 0.0f;
        a2_l[] = 0.0f;
        a1_r[] = 0.0f;
        a2_r[] = 0.0f;
        comb_idx[] = 0;
        comb_idx_r[] = 0;
        ap_idx[] = 0;
        ap_idx_r[] = 0;
    }
}

struct SylphDspState {
    float[kSylphNumParams] params = 0.0f;
    SylphVoice[kSylphMaxVoices] voices;
    SylphFxState fx;
    ulong voice_counter = 0;
    double lfo_phase = 0.0;
    uint rng_state = 0x51A9F3B7u;
    double sample_rate = 44100.0;
    float last_mono_freq = 440.0f;
    bool has_last_freq = false;

    void reset() nothrow @nogc @safe {
        d_sylph_apply_preset(this, 0);
        fx.reset();
        voice_counter = 0;
        lfo_phase = 0.0;
        rng_state = 0x51A9F3B7u;
        last_mono_freq = 440.0f;
        has_last_freq = false;
        foreach (ref v; voices) {
            v = SylphVoice.init;
        }
    }
}

// Asymmetric JP-8000 / Sylenth1-inspired unison detune multipliers around center.
extern (D) private immutable float[kSylphMaxUnison] UNISON_DETUNE_OFFSETS = [
    0.0f, -0.214f, 0.214f, -0.517f, 0.517f, -1.0f, 1.0f,
];
extern (D) private immutable float[kSylphMaxUnison] UNISON_PAN_OFFSETS = [
    0.0f, -0.38f, 0.38f, -0.72f, 0.72f, -1.0f, 1.0f,
];
extern (D) private immutable float[kSylphMaxUnison] UNISON_PHASE_SEED = [
    0.0f, 0.173f, 0.618f, 0.341f, 0.829f, 0.487f, 0.913f,
];

/// Curated factory patch values (`6 x 46`):
/// 0 = Trance Anthem Lead, 1 = Hard EDM Kick, 2 = Tight 909 Punch,
/// 3 = Euphoric Pluck, 4 = Hive Supersaw Pad, 5 = Rolling Psy Bass.
extern (D) private immutable float[kSylphNumParams][kSylphNumPresets] SYLPH_PRESETS = [
    // 0: Trance Anthem Lead — lush wide dual hypersaw + LP24 + chorus + ping-pong delay + reverb
    [
        0.00f, 0.50f, 0.50f, 1.00f, 0.38f, 0.88f, 0.00f, 0.86f, // Osc1: Saw 7-unison
        0.00f, 0.75f, 0.50f, 0.67f, 0.46f, 0.92f, 0.00f, 0.56f, // Osc2: Saw +1 oct 5-unison
        0.00f, 0.50f, 0.24f, 0.08f, 0.50f, 0.25f, 0.25f,        // Sub + slight air + 0 pitch snap
        0.15f, 0.72f, 0.78f, 0.58f, 0.00f,                      // Amp ADSR (4ms A, 460ms D, 0.78 S, 210ms R) + Poly
        0.00f, 0.72f, 0.22f, 0.72f, 0.10f, 0.70f, 0.44f, 0.58f, // LP24 Filter + Env
        0.22f, 0.32f, 0.05f, 0.12f, 0.36f, 0.32f, 0.58f, 0.30f, // Drive, LFO, Chorus, Delay, Reverb
        0.06f, 0.75f,                                           // Glide, Volume
    ],
    // 1: Hard EDM Kick — phase-retriggered sine+tri+click with fast +42st punch & tight low-end drive
    [
        0.85f, 0.50f, 0.50f, 0.00f, 0.00f, 0.00f, 1.00f, 0.95f, // Osc1: Sine 1-voice RETRIG
        0.55f, 0.50f, 0.50f, 0.00f, 0.00f, 0.00f, 1.00f, 0.24f, // Osc2: Tri harmonic punch RETRIG
        0.00f, 0.85f, 0.55f, 0.58f, 0.94f, 0.36f, 0.16f,        // Sub locked, Click 0.58, Pitch +42st ~23ms whip
        0.00f, 0.64f, 0.00f, 0.44f, 0.50f,                      // Tight 233ms EDM kick body + 58ms rel + Mono
        0.00f, 0.42f, 0.18f, 0.82f, 0.00f, 0.55f, 0.12f, 0.42f, // LP24 transient snap (~108ms sweep)
        0.58f, 0.20f, 0.00f, 0.00f, 0.00f, 0.00f, 0.40f, 0.00f, // Tight Drive, zero stereo mud
        0.00f, 0.84f,                                           // Glide, Volume
    ],
    // 2: Tight 909 Punch — -1 oct tuned for C2/C3 MIDI triggers, snappy click + thick warm body
    [
        0.85f, 0.25f, 0.50f, 0.00f, 0.00f, 0.00f, 1.00f, 0.92f, // Osc1: Sine -1 oct RETRIG
        0.28f, 0.25f, 0.50f, 0.00f, 0.00f, 0.00f, 1.00f, 0.18f, // Osc2: Square subtle saturation body
        0.00f, 0.50f, 0.38f, 0.46f, 0.88f, 0.44f, 0.28f,        // Sub -1 oct, click 0.46, ~33ms pitch whip
        0.00f, 0.68f, 0.00f, 0.48f, 0.50f,                      // 327ms 909 kick decay + Mono
        0.00f, 0.36f, 0.24f, 0.84f, 0.00f, 0.58f, 0.08f, 0.45f, // LP24 sweep
        0.66f, 0.18f, 0.00f, 0.00f, 0.00f, 0.00f, 0.40f, 0.00f, // Punchy warm drive
        0.00f, 0.84f,                                           // Glide, Volume
    ],
    // 3: Euphoric Pluck — bright 5-voice saw+square pluck with dotted stereo delay
    [
        0.00f, 0.50f, 0.50f, 0.67f, 0.32f, 0.78f, 0.00f, 0.82f, // Osc1: Saw 5-unison
        0.28f, 0.75f, 0.50f, 0.50f, 0.38f, 0.82f, 0.00f, 0.48f, // Osc2: Square +1 oct
        0.00f, 0.50f, 0.18f, 0.05f, 0.54f, 0.18f, 0.10f,        // Tiny pitch click
        0.00f, 0.68f, 0.08f, 0.56f, 0.00f,                      // Pluck Amp ADSR (~327ms decay) + Poly
        0.00f, 0.28f, 0.26f, 0.90f, 0.00f, 0.62f, 0.10f, 0.54f, // Bright LP24 pluck sweep (~200ms)
        0.16f, 0.38f, 0.00f, 0.08f, 0.30f, 0.46f, 0.62f, 0.34f, // Dotted delay + reverb
        0.00f, 0.78f,                                           // Glide, Volume
    ],
    // 4: Hive Supersaw Pad — lush 7-voice detuned stereosphere + slow swell + shimmer reverb
    [
        0.00f, 0.50f, 0.50f, 1.00f, 0.54f, 1.00f, 0.00f, 0.76f, // Osc1: Wide 7-unison Saw
        0.00f, 0.50f, 0.65f, 1.00f, 0.48f, 1.00f, 0.00f, 0.62f, // Osc2: 7-unison +7 semitone fifth/warmth
        0.35f, 0.50f, 0.30f, 0.12f, 0.50f, 0.20f, 0.25f,        // Tri sub + air
        0.68f, 0.80f, 0.84f, 0.72f, 0.00f,                      // Slow pad Amp ADSR (~327ms A, ~750ms R)
        0.00f, 0.54f, 0.18f, 0.64f, 0.64f, 0.78f, 0.52f, 0.70f, // Lush moving LP24
        0.12f, 0.24f, 0.08f, 0.28f, 0.52f, 0.26f, 0.55f, 0.52f, // Wide Chorus + Reverb
        0.18f, 0.72f,                                           // Glide, Volume
    ],
    // 5: Rolling Psy Bass — mono phase-locked -1 oct saw + sub + fast zap envelope
    [
        0.00f, 0.25f, 0.50f, 0.00f, 0.00f, 0.00f, 1.00f, 0.88f, // Osc1: Tight RETRIG Saw -1 oct
        0.00f, 0.25f, 0.50f, 0.17f, 0.10f, 0.18f, 1.00f, 0.30f, // Osc2: Tight subtle thickness
        0.00f, 0.25f, 0.45f, 0.04f, 0.50f, 0.22f, 0.15f,        // Clean sub anchor
        0.00f, 0.54f, 0.32f, 0.40f, 0.80f,                      // Tight ~100ms bass envelope + Legato
        0.00f, 0.24f, 0.34f, 0.86f, 0.00f, 0.52f, 0.05f, 0.40f, // Snappy ~85ms LP24 psy zap
        0.40f, 0.30f, 0.00f, 0.00f, 0.00f, 0.00f, 0.40f, 0.00f, // Tight saturated low-end
        0.12f, 0.80f,                                           // Fast glide, Volume
    ],
];

/// Copies factory preset `preset_idx` into `state.params` and returns the count of params updated.
int d_sylph_apply_preset(ref SylphDspState state, int preset_idx) nothrow @nogc @safe {
    int idx = preset_idx;
    if (idx < 0 || idx >= kSylphNumPresets) idx = 0;
    foreach (i; 0 .. kSylphNumParams) {
        d_sylph_set_parameter(state, cast(uint)i, SYLPH_PRESETS[idx][i]);
    }
    return kSylphNumParams;
}

pure nothrow @nogc @safe double d_sylph_get_default_value(int id) {
    if (id < 0 || id >= kSylphNumParams) return 0.0;
    return cast(double)SYLPH_PRESETS[0][id];
}

pure nothrow @nogc @safe SylphWaveform d_sylph_norm_to_wave(float norm) {
    if (norm < 0.25f) return SylphWaveform.SAW;
    if (norm < 0.50f) return SylphWaveform.SQUARE;
    if (norm < 0.75f) return SylphWaveform.TRIANGLE;
    return SylphWaveform.SINE;
}

pure nothrow @nogc @safe SylphPlayMode d_sylph_norm_to_play_mode(float norm) {
    if (norm < 0.34f) return SylphPlayMode.POLY;
    if (norm < 0.67f) return SylphPlayMode.MONO;
    return SylphPlayMode.LEGATO;
}

pure nothrow @nogc @safe SylphFilterType d_sylph_norm_to_filter_type(float norm) {
    if (norm < 0.25f) return SylphFilterType.LP24;
    if (norm < 0.50f) return SylphFilterType.LP12;
    if (norm < 0.75f) return SylphFilterType.BP12;
    return SylphFilterType.HP12;
}

pure nothrow @nogc @safe int d_sylph_norm_to_unison(float norm) {
    int u = cast(int)(clampVal(norm, 0.0f, 1.0f) * 6.0f + 0.5f) + 1;
    if (u < 1) u = 1;
    if (u > kSylphMaxUnison) u = kSylphMaxUnison;
    return u;
}

pure nothrow @nogc @safe int d_sylph_norm_to_oct(float norm) {
    int o = cast(int)((clampVal(norm, 0.0f, 1.0f) - 0.5f) * 4.0f + (norm >= 0.5f ? 0.5f : -0.5f));
    return clampVal(o, -2, 2);
}

pure nothrow @nogc @safe int d_sylph_norm_to_semi(float norm) {
    float s = (clampVal(norm, 0.0f, 1.0f) - 0.5f) * 24.0f;
    int si = cast(int)(s >= 0.0f ? s + 0.5f : s - 0.5f);
    return clampVal(si, -12, 12);
}

private pure nothrow @nogc @safe double polyBlep(double t, double dt) {
    if (dt <= 0.0) return 0.0;
    if (t < dt) {
        double x = t / dt;
        return x + x - x * x - 1.0;
    }
    if (t > 1.0 - dt) {
        double x = (t - 1.0) / dt;
        return x * x + x + x + 1.0;
    }
    return 0.0;
}

private nothrow @nogc @trusted float renderOscSample(SylphWaveform wf, double phase, double dt) {
    final switch (wf) {
        case SylphWaveform.SAW:
            return cast(float)(2.0 * phase - 1.0 - polyBlep(phase, dt));
        case SylphWaveform.SQUARE: {
            double v = phase < 0.5 ? 1.0 : -1.0;
            v += polyBlep(phase, dt);
            double p2 = phase + 0.5;
            if (p2 >= 1.0) p2 -= 1.0;
            v -= polyBlep(p2, dt);
            return cast(float)v;
        }
        case SylphWaveform.TRIANGLE:
            return cast(float)(phase < 0.5 ? (4.0 * phase - 1.0) : (3.0 - 4.0 * phase));
        case SylphWaveform.SINE:
            return sinf(cast(float)(phase * (2.0 * pi)));
    }
}

private float nextNoise(ref uint rng) nothrow @nogc @safe {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (cast(float)(rng & 0xFFFFu) / 32768.0f) - 1.0f;
}

private void initVoiceTrigger(ref SylphDspState state, ref SylphVoice v, int pitch, float velocity, float pan, bool is_legato_slur) nothrow @nogc @safe {
    const float newFreq = midiNoteToHz(cast(float)pitch);
    v.active = true;
    v.held = true;
    v.note = pitch;
    v.velocity = clampVal(velocity, 0.0f, 1.0f);
    v.pan = clampVal(pan, -1.0f, 1.0f);
    v.target_freq = newFreq;

    if (!is_legato_slur) {
        const float glideNorm = state.params[SylphParamId.P_GLIDE];
        v.cur_freq = (glideNorm > 0.005f && state.has_last_freq) ? state.last_mono_freq : newFreq;
        state.last_mono_freq = newFreq;
        state.has_last_freq = true;

        const bool r1 = state.params[SylphParamId.P_OSC1_RETRIG] >= 0.5f;
        const bool r2 = state.params[SylphParamId.P_OSC2_RETRIG] >= 0.5f;
        foreach (u; 0 .. kSylphMaxUnison) {
            v.osc1_phase[u] = r1 ? 0.0 : cast(double)UNISON_PHASE_SEED[u];
            v.osc2_phase[u] = r2 ? 0.0 : cast(double)(UNISON_PHASE_SEED[u] * 0.73f);
        }
        v.sub_phase = 0.0;
        v.pitch_env = 1.0f;
        v.click_env = 1.0f;
        v.filt1_l.reset();
        v.filt2_l.reset();
        v.filt1_r.reset();
        v.filt2_r.reset();

        v.amp_env.setNormalized(
            state.params[SylphParamId.P_AMP_A],
            state.params[SylphParamId.P_AMP_D],
            state.params[SylphParamId.P_AMP_S],
            state.params[SylphParamId.P_AMP_R]
        );
        v.filt_env.setNormalized(
            state.params[SylphParamId.P_FILT_A],
            state.params[SylphParamId.P_FILT_D],
            state.params[SylphParamId.P_FILT_S],
            state.params[SylphParamId.P_FILT_R]
        );
        v.amp_env.noteOn();
        v.filt_env.noteOn();
    } else {
        state.last_mono_freq = newFreq;
        state.has_last_freq = true;
        v.age = ++state.voice_counter;
        return;
    }
    v.age = ++state.voice_counter;
}

void d_sylph_note_on(ref SylphDspState state, int pitch, float velocity, float pan = 0.0f) nothrow @nogc @safe {
    const mode = d_sylph_norm_to_play_mode(state.params[SylphParamId.P_PLAY_MODE]);
    if (mode != SylphPlayMode.POLY) {
        // Mono / Legato always routes onto voice 0 cleanly
        foreach (i; 1 .. kSylphMaxVoices) {
            state.voices[i].active = false;
            state.voices[i].held = false;
        }
        auto v = &state.voices[0];
        const bool isLegatoSlur = (mode == SylphPlayMode.LEGATO) && v.active && v.held;
        initVoiceTrigger(state, *v, pitch, velocity, pan, isLegatoSlur);
        return;
    }

    int target = -1;
    foreach (i; 0 .. kSylphMaxVoices) {
        if (!state.voices[i].active && state.voices[i].amp_env.isIdle()) {
            target = cast(int)i;
            break;
        }
    }
    if (target < 0) {
        ulong min_age = ulong.max;
        foreach (i; 0 .. kSylphMaxVoices) {
            if (state.voices[i].age < min_age) {
                min_age = state.voices[i].age;
                target = cast(int)i;
            }
        }
    }
    if (target < 0) target = 0;
    initVoiceTrigger(state, state.voices[target], pitch, velocity, pan, false);
}

void d_sylph_note_off(ref SylphDspState state, int pitch) nothrow @nogc @safe {
    foreach (ref v; state.voices) {
        if (v.active && (pitch < 0 || pitch > 127 || v.note == pitch)) {
            v.held = false;
            v.amp_env.noteOff();
            v.filt_env.noteOff();
        }
    }
}

void d_sylph_set_parameter(ref SylphDspState state, uint id, float value) nothrow @nogc @safe {
    if (id >= kSylphNumParams) return;
    state.params[id] = clampVal(value, 0.0f, 1.0f);
    if (id >= SylphParamId.P_AMP_A && id <= SylphParamId.P_AMP_R) {
        foreach (ref v; state.voices) {
            v.amp_env.setNormalized(
                state.params[SylphParamId.P_AMP_A],
                state.params[SylphParamId.P_AMP_D],
                state.params[SylphParamId.P_AMP_S],
                state.params[SylphParamId.P_AMP_R]
            );
        }
    }
    if (id >= SylphParamId.P_FILT_A && id <= SylphParamId.P_FILT_R) {
        foreach (ref v; state.voices) {
            v.filt_env.setNormalized(
                state.params[SylphParamId.P_FILT_A],
                state.params[SylphParamId.P_FILT_D],
                state.params[SylphParamId.P_FILT_S],
                state.params[SylphParamId.P_FILT_R]
            );
        }
    }
}

SylphDspState* d_sylph_create() nothrow @nogc @trusted {
    import core.stdc.stdlib : malloc;
    auto s = cast(SylphDspState*)malloc(SylphDspState.sizeof);
    if (s !is null) {
        *s = SylphDspState.init;
        s.reset();
    }
    return s;
}

void d_sylph_destroy(SylphDspState* state) nothrow @nogc @trusted {
    import core.stdc.stdlib : free;
    if (state !is null) {
        free(state);
    }
}

void d_sylph_reset(SylphDspState* state) nothrow @nogc @safe {
    if (state !is null) {
        state.reset();
    }
}

float d_sylph_get_param(const(SylphDspState)* state, uint id) nothrow @nogc @safe {
    if (state !is null && id < kSylphNumParams) {
        return state.params[id];
    }
    return 0.0f;
}

extern (D) private float processAllpass(T)(ref T buf, ref int idx, float input, float fb) nothrow @nogc @safe {
    const float delayed = buf[idx];
    const float outVal = -input + delayed;
    buf[idx] = input + delayed * fb;
    if (++idx >= cast(int)buf.length) idx = 0;
    return outVal;
}

extern (D) private float processComb(T)(ref T buf, ref int idx, float input, float fb, float damp) nothrow @nogc @safe {
    const float delayed = buf[idx];
    buf[idx] = input + delayed * fb * (1.0f - damp);
    if (++idx >= cast(int)buf.length) idx = 0;
    return delayed;
}

void d_sylph_process(
    ref SylphDspState state,
    float* out_l,
    float* out_r,
    int num_samples,
    double sample_rate,
    const(SylphMidiEvent)* events,
    int num_events
) nothrow @nogc @trusted {
    if (num_samples <= 0 || out_l is null || out_r is null) return;
    if (sample_rate <= 100.0) sample_rate = 44100.0;
    state.sample_rate = sample_rate;
    const float sr = cast(float)sample_rate;

    if (events !is null) {
        foreach (i; 0 .. num_events) {
            if (events[i].is_note_on && events[i].velocity > 0.0f) {
                d_sylph_note_on(state, events[i].pitch, events[i].velocity, events[i].pan);
            } else {
                d_sylph_note_off(state, events[i].pitch);
            }
        }
    }

    const wf1 = d_sylph_norm_to_wave(state.params[SylphParamId.P_OSC1_WAVE]);
    const int oct1 = d_sylph_norm_to_oct(state.params[SylphParamId.P_OSC1_OCT]);
    const int semi1 = d_sylph_norm_to_semi(state.params[SylphParamId.P_OSC1_SEMI]);
    const int uni1 = d_sylph_norm_to_unison(state.params[SylphParamId.P_OSC1_UNISON]);
    const float det1 = state.params[SylphParamId.P_OSC1_DETUNE];
    const float st1 = state.params[SylphParamId.P_OSC1_STEREO];
    const float vol1 = state.params[SylphParamId.P_OSC1_VOL];
    const float ratio1 = powf(2.0f, cast(float)oct1 + cast(float)semi1 / 12.0f);
    const float spread1 = det1 * det1 * 0.055f;
    const float uniGain1 = vol1 / powf(cast(float)uni1, 0.52f);

    const wf2 = d_sylph_norm_to_wave(state.params[SylphParamId.P_OSC2_WAVE]);
    const int oct2 = d_sylph_norm_to_oct(state.params[SylphParamId.P_OSC2_OCT]);
    const int semi2 = d_sylph_norm_to_semi(state.params[SylphParamId.P_OSC2_SEMI]);
    const int uni2 = d_sylph_norm_to_unison(state.params[SylphParamId.P_OSC2_UNISON]);
    const float det2 = state.params[SylphParamId.P_OSC2_DETUNE];
    const float st2 = state.params[SylphParamId.P_OSC2_STEREO];
    const float vol2 = state.params[SylphParamId.P_OSC2_VOL];
    const float ratio2 = powf(2.0f, cast(float)oct2 + cast(float)semi2 / 12.0f);
    const float spread2 = det2 * det2 * 0.055f;
    const float uniGain2 = vol2 / powf(cast(float)uni2, 0.52f);

    const float subNorm = state.params[SylphParamId.P_SUB_WAVE];
    const SylphWaveform subWf = subNorm < 0.34f ? SylphWaveform.SINE
                              : (subNorm < 0.67f ? SylphWaveform.TRIANGLE : SylphWaveform.SQUARE);
    const float subOctNorm = state.params[SylphParamId.P_SUB_OCT];
    const float subRatio = subOctNorm < 0.34f ? 0.25f : (subOctNorm < 0.67f ? 0.5f : 1.0f);
    const float subVol = state.params[SylphParamId.P_SUB_VOL];
    const float clickNoise = state.params[SylphParamId.P_CLICK_NOISE];

    // Pitch snap envelope (for tight kicks / plucks)
    const float pitchSt = (state.params[SylphParamId.P_PITCH_AMT] - 0.5f) * 96.0f;
    const float pDecSec = 0.004f * powf(125.0f, state.params[SylphParamId.P_PITCH_DECAY]);
    const float pEnvCoef = expf(-1.0f / (pDecSec * sr));
    const float pCurveExp = 3.0f - state.params[SylphParamId.P_PITCH_CURVE] * 2.1f; // 3.0=whip .. 0.9=punch
    const float clickCoef = expf(-1.0f / (0.003f * sr)); // ~3ms beater click

    const float glideNorm = state.params[SylphParamId.P_GLIDE];
    const float glideCoef = (glideNorm > 0.005f)
        ? (1.0f - expf(-1.0f / ((0.004f + glideNorm * 0.35f) * sr)))
        : 1.0f;

    const filtType = d_sylph_norm_to_filter_type(state.params[SylphParamId.P_FILT_TYPE]);
    const float cutBaseHz = 20.0f * powf(900.0f, state.params[SylphParamId.P_FILT_CUT]);
    const float resNorm = state.params[SylphParamId.P_FILT_RES];
    const float kRes = 2.0f * (1.0f - clampVal(resNorm * 0.94f, 0.0f, 0.94f));
    const float filtEnvOct = (state.params[SylphParamId.P_FILT_ENV_AMT] - 0.5f) * 10.0f;

    const float driveNorm = state.params[SylphParamId.P_DRIVE];
    const float driveGain = 1.0f + driveNorm * 4.5f;
    const float driveInv = 1.0f / tanhf(driveGain);

    const float lfoRateHz = 0.1f * powf(200.0f, state.params[SylphParamId.P_LFO_RATE]);
    const double lfoInc = cast(double)lfoRateHz / sample_rate;
    const float lfoPitchOct = state.params[SylphParamId.P_LFO_PITCH] * (1.0f / 12.0f);
    const float lfoCutOct = state.params[SylphParamId.P_LFO_CUT] * 3.0f;

    const float chorusMix = state.params[SylphParamId.P_CHORUS_MIX];
    const float delayMix = state.params[SylphParamId.P_DELAY_MIX];
    int delaySamp = cast(int)((0.08f + state.params[SylphParamId.P_DELAY_TIME] * 0.47f) * sr);
    if (delaySamp < 64) delaySamp = 64;
    if (delaySamp >= kSylphDelayMax - 1) delaySamp = kSylphDelayMax - 2;
    const float reverbMix = state.params[SylphParamId.P_REVERB_MIX];
    const float masterVol = state.params[SylphParamId.P_VOLUME];

    foreach (i; 0 .. num_samples) {
        float mixL = 0.0f;
        float mixR = 0.0f;

        const float lfo = sinf(cast(float)(state.lfo_phase * (2.0 * pi)));
        state.lfo_phase += lfoInc;
        if (state.lfo_phase >= 1.0) state.lfo_phase -= 1.0;

        const float lfoPitchMul = (lfoPitchOct > 0.0001f) ? powf(2.0f, lfo * lfoPitchOct) : 1.0f;

        foreach (ref v; state.voices) {
            if (!v.active) continue;

            const float envGain = v.amp_env.process(sr);
            const float envFilt = v.filt_env.process(sr);
            if (v.amp_env.isIdle()) {
                v.active = false;
                continue;
            }

            // Portamento glide
            v.cur_freq += (v.target_freq - v.cur_freq) * glideCoef;

            // Shaped exponential Pitch Snap envelope for hard tight kicks
            v.pitch_env *= pEnvCoef;
            v.click_env *= clickCoef;
            float pitchModMul = lfoPitchMul;
            if (fabsf(pitchSt) > 0.05f && v.pitch_env > 0.0001f) {
                const float shapedP = powf(v.pitch_env, pCurveExp);
                pitchModMul *= powf(2.0f, (pitchSt * shapedP) / 12.0f);
            }
            const float instFreq = clampVal(v.cur_freq * pitchModMul, 10.0f, sr * 0.45f);

            float vl = 0.0f;
            float vr = 0.0f;

            // Osc 1 Unison
            if (uniGain1 > 0.0005f) {
                const float baseF1 = instFreq * ratio1;
                foreach (u; 0 .. uni1) {
                    const float df = 1.0f + UNISON_DETUNE_OFFSETS[u] * spread1;
                    const double dt = clampVal(cast(double)(baseF1 * df) / sample_rate, 0.00001, 0.45);
                    const float smp = renderOscSample(wf1, v.osc1_phase[u], dt) * uniGain1;
                    const float panU = UNISON_PAN_OFFSETS[u] * st1;
                    vl += smp * (0.5f * (1.0f - panU));
                    vr += smp * (0.5f * (1.0f + panU));
                    v.osc1_phase[u] += dt;
                    if (v.osc1_phase[u] >= 1.0) v.osc1_phase[u] -= 1.0;
                }
            }

            // Osc 2 Unison
            if (uniGain2 > 0.0005f) {
                const float baseF2 = instFreq * ratio2;
                foreach (u; 0 .. uni2) {
                    const float df = 1.0f + UNISON_DETUNE_OFFSETS[u] * spread2;
                    const double dt = clampVal(cast(double)(baseF2 * df) / sample_rate, 0.00001, 0.45);
                    const float smp = renderOscSample(wf2, v.osc2_phase[u], dt) * uniGain2;
                    const float panU = UNISON_PAN_OFFSETS[u] * st2;
                    vl += smp * (0.5f * (1.0f - panU));
                    vr += smp * (0.5f * (1.0f + panU));
                    v.osc2_phase[u] += dt;
                    if (v.osc2_phase[u] >= 1.0) v.osc2_phase[u] -= 1.0;
                }
            }

            // Phase-locked Mono Sub Anchor
            if (subVol > 0.0005f) {
                const double dtSub = clampVal(cast(double)(instFreq * subRatio) / sample_rate, 0.00001, 0.45);
                const float subSmp = renderOscSample(subWf, v.sub_phase, dtSub) * subVol * 0.55f;
                vl += subSmp;
                vr += subSmp;
                v.sub_phase += dtSub;
                if (v.sub_phase >= 1.0) v.sub_phase -= 1.0;
            }

            // Air noise + kick transient beater click
            if (clickNoise > 0.0005f) {
                const float nz = nextNoise(state.rng_state);
                const float airGain = clickNoise * 0.14f;
                const float beaterGain = (fabsf(pitchSt) > 3.0f) ? (clickNoise * v.click_env * 0.85f) : 0.0f;
                const float cSmp = nz * (airGain + beaterGain);
                vl += cSmp;
                vr += cSmp;
            }

            // Warm analog / tight kick Drive saturation BEFORE/AT filter
            if (driveNorm > 0.01f) {
                vl = tanhf(vl * driveGain) * driveInv;
                vr = tanhf(vr * driveGain) * driveInv;
            }

            // ZDF SVF Filter (12dB or cascaded 24dB LP)
            const float totalModOct = envFilt * filtEnvOct + lfo * lfoCutOct;
            float dynCut = cutBaseHz * powf(2.0f, totalModOct);
            dynCut = clampVal(dynCut, 20.0f, sr * 0.42f);
            const float g = tanf(cast(float)pi * dynCut / sr);
            final switch (filtType) {
                case SylphFilterType.LP24:
                    vl = v.filt2_l.process(v.filt1_l.process(vl, g, kRes, 0), g, kRes * 0.65f, 0);
                    vr = v.filt2_r.process(v.filt1_r.process(vr, g, kRes, 0), g, kRes * 0.65f, 0);
                    break;
                case SylphFilterType.LP12:
                    vl = v.filt1_l.process(vl, g, kRes, 0);
                    vr = v.filt1_r.process(vr, g, kRes, 0);
                    break;
                case SylphFilterType.BP12:
                    vl = v.filt1_l.process(vl, g, kRes, 1);
                    vr = v.filt1_r.process(vr, g, kRes, 1);
                    break;
                case SylphFilterType.HP12:
                    vl = v.filt1_l.process(vl, g, kRes, 2);
                    vr = v.filt1_r.process(vr, g, kRes, 2);
                    break;
            }

            const float gPanL = 1.0f - clampVal(v.pan, 0.0f, 1.0f);
            const float gPanR = 1.0f + clampVal(v.pan, -1.0f, 0.0f);
            const float vGain = envGain * v.velocity * 0.42f;
            mixL += vl * vGain * gPanL;
            mixR += vr * vGain * gPanR;
        }

        // Stereo Ensemble Chorus
        if (chorusMix > 0.005f) {
            state.fx.chorus_l[state.fx.chorus_write] = mixL;
            state.fx.chorus_r[state.fx.chorus_write] = mixR;
            const float cLfo = sinf(cast(float)(state.fx.chorus_lfo * (2.0 * pi)));
            state.fx.chorus_lfo += 0.65 / sample_rate;
            if (state.fx.chorus_lfo >= 1.0) state.fx.chorus_lfo -= 1.0;

            const int tapL = cast(int)((0.011f + 0.004f * cLfo) * sr);
            const int tapR = cast(int)((0.014f - 0.004f * cLfo) * sr);
            const int idxL = (state.fx.chorus_write - tapL + kSylphChorusMax) & (kSylphChorusMax - 1);
            const int idxR = (state.fx.chorus_write - tapR + kSylphChorusMax) & (kSylphChorusMax - 1);
            const float wetL = state.fx.chorus_r[idxL];
            const float wetR = state.fx.chorus_l[idxR];
            mixL = mixL * (1.0f - 0.35f * chorusMix) + wetL * (0.65f * chorusMix);
            mixR = mixR * (1.0f - 0.35f * chorusMix) + wetR * (0.65f * chorusMix);
            state.fx.chorus_write = (state.fx.chorus_write + 1) & (kSylphChorusMax - 1);
        }

        // Ping-Pong Stereo Delay
        if (delayMix > 0.005f) {
            const int rdL = (state.fx.delay_write - delaySamp + kSylphDelayMax) % kSylphDelayMax;
            const int rdR = (state.fx.delay_write - (delaySamp * 3) / 4 + kSylphDelayMax) % kSylphDelayMax;
            const float dOutL = state.fx.delay_l[rdL];
            const float dOutR = state.fx.delay_r[rdR];
            state.fx.delay_l[state.fx.delay_write] = mixL + dOutR * 0.42f;
            state.fx.delay_r[state.fx.delay_write] = mixR + dOutL * 0.42f;
            if (++state.fx.delay_write >= kSylphDelayMax) state.fx.delay_write = 0;
            mixL += dOutL * delayMix * 0.65f;
            mixR += dOutR * delayMix * 0.65f;
        }

        // Stereo Trance Hall Reverb
        if (reverbMix > 0.005f) {
            const float revInL = mixL * 0.25f;
            const float revInR = mixR * 0.25f;
            const float fb = 0.80f;
            const float damp = 0.14f;
            float rL = processComb(state.fx.c1_l, state.fx.comb_idx[0], revInL, fb, damp)
                     + processComb(state.fx.c2_l, state.fx.comb_idx[1], revInL, fb * 0.99f, damp)
                     + processComb(state.fx.c3_l, state.fx.comb_idx[2], revInL, fb * 0.98f, damp)
                     + processComb(state.fx.c4_l, state.fx.comb_idx[3], revInL, fb * 0.97f, damp);
            float rR = processComb(state.fx.c1_r, state.fx.comb_idx_r[0], revInR, fb, damp)
                     + processComb(state.fx.c2_r, state.fx.comb_idx_r[1], revInR, fb * 0.99f, damp)
                     + processComb(state.fx.c3_r, state.fx.comb_idx_r[2], revInR, fb * 0.98f, damp)
                     + processComb(state.fx.c4_r, state.fx.comb_idx_r[3], revInR, fb * 0.97f, damp);
            rL = processAllpass(state.fx.a1_l, state.fx.ap_idx[0], rL, 0.5f);
            rL = processAllpass(state.fx.a2_l, state.fx.ap_idx[1], rL, 0.5f);
            rR = processAllpass(state.fx.a1_r, state.fx.ap_idx_r[0], rR, 0.5f);
            rR = processAllpass(state.fx.a2_r, state.fx.ap_idx_r[1], rR, 0.5f);
            mixL += rL * reverbMix * 0.45f;
            mixR += rR * reverbMix * 0.45f;
        }

        // Clean soft-clipped master output limiter stage
        out_l[i] = tanhf(mixL * masterVol * 1.4f);
        out_r[i] = tanhf(mixR * masterVol * 1.4f);
    }
}

} // extern (C++, "hibiki")
