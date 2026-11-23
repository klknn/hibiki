module engine.instruments.builtin_sylph;

import core.stdc.math : cosf, expf, fabsf, powf, sinf, tanf, tanhf;
import engine.core.math : clampVal, midiNoteToHz, pi;
import engine.instruments.adsr : Adsr;

extern (C++,"hibiki") {

  enum kSylphMaxVoices = 8;
  enum kSylphMaxUnison = 7;
  enum kSylphDelayMax = 49152;
  enum kSylphChorusMax = 4096;
  enum kSylphNumParams = 71;
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
    // Part A: Osc A (0..7)
    P_OSC1_WAVE = 0,
    P_OSC1_OCT = 1,
    P_OSC1_SEMI = 2,
    P_OSC1_UNISON = 3,
    P_OSC1_DETUNE = 4,
    P_OSC1_STEREO = 5,
    P_OSC1_RETRIG = 6,
    P_OSC1_VOL = 7,

    // Part A: Filter A (8..12)
    P_FILTA_TYPE = 8,
    P_FILTA_CUT = 9,
    P_FILTA_RES = 10,
    P_FILTA_DRIVE = 11,
    P_FILTA_ENV_AMT = 12,

    // Part A: Envelopes A (13..20)
    P_AMPA_A = 13,
    P_AMPA_D = 14,
    P_AMPA_S = 15,
    P_AMPA_R = 16,
    P_FILTA_A = 17,
    P_FILTA_D = 18,
    P_FILTA_S = 19,
    P_FILTA_R = 20,

    // Part B: Osc B (21..28)
    P_OSC2_WAVE = 21,
    P_OSC2_OCT = 22,
    P_OSC2_SEMI = 23,
    P_OSC2_UNISON = 24,
    P_OSC2_DETUNE = 25,
    P_OSC2_STEREO = 26,
    P_OSC2_RETRIG = 27,
    P_OSC2_VOL = 28,

    // Part B: Filter B (29..33)
    P_FILTB_TYPE = 29,
    P_FILTB_CUT = 30,
    P_FILTB_RES = 31,
    P_FILTB_DRIVE = 32,
    P_FILTB_ENV_AMT = 33,

    // Part B: Envelopes B (34..41)
    P_AMPB_A = 34,
    P_AMPB_D = 35,
    P_AMPB_S = 36,
    P_AMPB_R = 37,
    P_FILTB_A = 38,
    P_FILTB_D = 39,
    P_FILTB_S = 40,
    P_FILTB_R = 41,

    // Sub / Punch / LFO / Global (42..54)
    P_SUB_WAVE = 42,
    P_SUB_OCT = 43,
    P_SUB_VOL = 44,
    P_CLICK_NOISE = 45,
    P_PITCH_AMT = 46,
    P_PITCH_DECAY = 47,
    P_PITCH_CURVE = 48,
    P_LFO_RATE = 49,
    P_LFO_PITCH = 50,
    P_LFO_CUT = 51,
    P_PLAY_MODE = 52,
    P_GLIDE = 53,
    P_VOLUME = 54,

    // Serial FX Chain (55..70)
    P_DIST_ON = 55,
    P_DIST_DRIVE = 56,
    P_DIST_TONE = 57,
    P_DIST_MIX = 58,
    P_CHORUS_ON = 59,
    P_CHORUS_RATE = 60,
    P_CHORUS_DEPTH = 61,
    P_CHORUS_MIX = 62,
    P_DELAY_ON = 63,
    P_DELAY_TIME = 64,
    P_DELAY_FB = 65,
    P_DELAY_MIX = 66,
    P_REVERB_ON = 67,
    P_REVERB_SIZE = 68,
    P_REVERB_DAMP = 69,
    P_REVERB_MIX = 70,
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
      if (mode == 2)
        return v0 - k * v1 - v2; // HP
      if (mode == 1)
        return v1; // BP
      return v2; // LP
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
    Adsr ampa_env;
    Adsr filta_env;
    Adsr ampb_env;
    Adsr filtb_env;
    SylphZdfSvf filta1_l;
    SylphZdfSvf filta2_l;
    SylphZdfSvf filta1_r;
    SylphZdfSvf filta2_r;
    SylphZdfSvf filtb1_l;
    SylphZdfSvf filtb2_l;
    SylphZdfSvf filtb1_r;
    SylphZdfSvf filtb2_r;
    ulong age = 0;
  }

  /// Fixed-size Serial Master FX Rack state (Dist Tone + Chorus + Ping-Pong Delay + Schroeder Reverb).
  struct SylphFxState {
    float dist_lp_l = 0.0f;
    float dist_lp_r = 0.0f;

    float[kSylphChorusMax] chorus_l = 0.0f;
    float[kSylphChorusMax] chorus_r = 0.0f;
    int chorus_write = 0;
    double chorus_lfo = 0.0;

    float[kSylphDelayMax] delay_l = 0.0f;
    float[kSylphDelayMax] delay_r = 0.0f;
    int delay_write = 0;

    // 4 parallel stereo combs + 2 series allpasses per side (prime tap lengths)
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
      dist_lp_l = 0.0f;
      dist_lp_r = 0.0f;
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

  extern (D) private immutable float[kSylphMaxUnison] UNISON_DETUNE_OFFSETS = [
    0.0f, -0.214f, 0.214f, -0.517f, 0.517f, -1.0f, 1.0f,
  ];
  extern (D) private immutable float[kSylphMaxUnison] UNISON_PAN_OFFSETS = [
    0.0f, -0.38f, 0.38f, -0.72f, 0.72f, -1.0f, 1.0f,
  ];
  extern (D) private immutable float[kSylphMaxUnison] UNISON_PHASE_SEED = [
    0.0f, 0.173f, 0.618f, 0.341f, 0.829f, 0.487f, 0.913f,
  ];

  /// Curated factory patch values (`6 x 71`):
  /// 0 = Trance Anthem Lead, 1 = Hard EDM Kick, 2 = Tight 909 Punch,
  /// 3 = Euphoric Pluck, 4 = Hive Supersaw Pad, 5 = Rolling Psy Bass.
  extern (D) private immutable float[kSylphNumParams][kSylphNumPresets] SYLPH_PRESETS = [
    // 0: Trance Anthem Lead — Dual hypersaw + independent Filter A (LP24) & Filter B (LP12) + Chorus->Delay->Reverb
    [
      0.00f, 0.50f, 0.50f, 1.00f, 0.38f, 0.88f, 0.00f, 0.86f, // Osc A: Saw 7-unison
      0.00f, 0.70f,
      0.22f, 0.18f,
      0.72f, // Filter A: LP24 warm drive sweep
      0.15f, 0.72f, 0.78f, 0.58f, 0.10f, 0.70f, 0.44f, 0.58f, // Amp Env A + Filt Env A
      0.00f, 0.75f,
      0.50f, 0.67f,
      0.46f, 0.92f, 0.00f, 0.58f, // Osc B: Saw +1 oct 5-unison
      0.35f, 0.84f, 0.28f, 0.12f, 0.64f, // Filter B: LP12 airy top cut
      0.12f, 0.68f, 0.72f, 0.56f, 0.08f,
      0.66f, 0.40f, 0.56f, // Amp Env B + Filt Env B
      0.00f, 0.50f, 0.24f, 0.08f, 0.50f, 0.25f, 0.25f, // Sub + slight air + 0 pitch snap
      0.32f, 0.05f, 0.12f,
      0.00f,
      0.06f, 0.75f, // LFO + Poly + Glide + Vol
      0.00f, 0.20f, 0.60f, 0.25f, // DIST (off by default, pre-dialed)
      1.00f, 0.35f, 0.55f, 0.36f, // CHORUS ON
      1.00f, 0.58f, 0.44f, 0.32f, // DELAY ON
      1.00f, 0.72f, 0.28f, 0.30f, // REVERB ON

    

  ], // 1: Hard EDM Kick — Part A Retrig Sub/Sine Body (LP24) + Part B Short Mid Punch (BP12) + Pitch +42st + Dist ON
    [
      0.85f, 0.50f, 0.50f, 0.00f, 0.00f, 0.00f, 1.00f, 0.96f, // Osc A: Sine 1-voice RETRIG
      0.00f, 0.42f,
      0.18f, 0.54f,
      0.82f, // Filter A: LP24 sub grip + punch drive
      0.00f, 0.64f, 0.00f, 0.44f, 0.00f, 0.55f, 0.10f, 0.42f, // Tight 233ms sub body AmpA + FiltA
      0.55f, 0.50f,
      0.50f, 0.00f,
      0.00f, 0.00f, 1.00f, 0.42f, // Osc B: Tri transient knock RETRIG
      0.62f, 0.56f, 0.34f, 0.62f, 0.78f, // Filter B: BP12 mid-transient focus
      0.00f, 0.38f, 0.00f, 0.30f, 0.00f,
      0.36f, 0.00f, 0.28f, // Ultra-fast 26ms knock AmpB
      0.00f, 0.85f, 0.56f, 0.58f, 0.94f, 0.36f, 0.16f, // Locked sub + Click + +42st Pitch Snap
      0.20f, 0.00f, 0.00f,
      0.50f,
      0.00f, 0.84f, // LFO off + Mono + Vol
      1.00f, 0.52f, 0.42f, 0.58f, // DIST ON (tight punch drive)
      0.00f, 0.30f, 0.40f, 0.20f, // CHORUS OFF
      0.00f, 0.40f, 0.30f, 0.20f, // DELAY OFF
      0.00f, 0.40f, 0.50f, 0.15f, // REVERB OFF
  ], // 2: Tight 909 Punch — -1 oct tuned for C2/C3 MIDI notes, warm saturation + sub lock
    [
      0.85f, 0.25f, 0.50f, 0.00f, 0.00f, 0.00f, 1.00f, 0.94f, // Osc A: Sine -1 oct RETRIG
      0.00f, 0.38f,
      0.24f, 0.64f,
      0.84f, // Filter A: LP24 warm drive
      0.00f, 0.68f, 0.00f, 0.48f, 0.00f, 0.58f, 0.08f, 0.45f, // 327ms 909 body
      0.28f, 0.25f,
      0.50f, 0.00f,
      0.00f, 0.00f, 1.00f, 0.32f, // Osc B: Square punch layer
      0.62f, 0.52f, 0.30f, 0.68f, 0.76f, // Filter B: BP12 beater focus
      0.00f, 0.42f, 0.00f, 0.32f, 0.00f,
      0.40f, 0.00f, 0.30f, // Short transient AmpB
      0.00f, 0.50f, 0.38f, 0.48f, 0.88f, 0.44f, 0.28f, // Sub -1 oct + click + whip
      0.18f, 0.00f, 0.00f,
      0.50f,
      0.00f, 0.84f, // Mono + Vol
      1.00f, 0.58f, 0.38f, 0.62f, // DIST ON
      0.00f, 0.30f, 0.40f, 0.20f, 0.00f, 0.40f,
      0.30f, 0.20f, 0.00f, 0.40f, 0.50f, 0.15f,
  ], // 3: Euphoric Pluck — bright dual-filter pluck + dotted delay + shimmer reverb
    [
      0.00f, 0.50f, 0.50f, 0.67f, 0.32f, 0.78f, 0.00f, 0.82f, // Osc A: Saw 5-unison
      0.00f, 0.28f,
      0.26f, 0.16f,
      0.90f, // Filter A: LP24 snappy sweep
      0.00f, 0.68f, 0.08f, 0.56f, 0.00f, 0.62f, 0.10f, 0.54f, // Pluck AmpA + FiltA
      0.28f, 0.75f,
      0.50f, 0.50f,
      0.38f, 0.82f, 0.00f, 0.48f, // Osc B: Square +1 oct
      0.35f, 0.42f, 0.22f, 0.14f, 0.86f, // Filter B: LP12 crisp transient
      0.00f, 0.58f, 0.04f, 0.48f, 0.00f,
      0.54f, 0.06f, 0.46f, // Fast top pluck AmpB
      0.00f, 0.50f, 0.18f, 0.05f, 0.54f, 0.18f, 0.10f,
      0.38f, 0.00f, 0.08f, 0.00f, 0.00f, 0.78f, 0.00f, 0.20f, 0.55f, 0.20f,
      1.00f, 0.30f, 0.48f, 0.30f, // CHORUS ON
      1.00f, 0.62f, 0.48f, 0.46f, // DELAY ON (dotted)
      1.00f, 0.76f, 0.26f, 0.34f, // REVERB ON
  ], // 4: Hive Supersaw Pad — lush slow-attack stereosphere across Filter A & B
    [
      0.00f, 0.50f, 0.50f, 1.00f, 0.54f, 1.00f, 0.00f, 0.76f, // Osc A: Wide 7-unison Saw
      0.00f, 0.54f,
      0.18f, 0.12f,
      0.64f, // Filter A: LP24
      0.68f, 0.80f, 0.84f, 0.72f, 0.64f, 0.78f, 0.52f, 0.70f, // Slow pad AmpA + FiltA
      0.00f, 0.50f,
      0.65f, 1.00f,
      0.48f, 1.00f, 0.00f, 0.62f, // Osc B: 7-unison +7st shimmer
      0.62f, 0.66f, 0.28f, 0.10f, 0.58f, // Filter B: BP12 lush air band
      0.74f, 0.82f, 0.80f, 0.74f, 0.68f,
      0.80f, 0.48f, 0.72f, // Swelling AmpB + FiltB
      0.35f, 0.50f, 0.30f, 0.12f, 0.50f, 0.20f, 0.25f,
      0.24f, 0.08f, 0.28f, 0.00f, 0.18f, 0.72f, 0.00f, 0.15f, 0.50f, 0.15f,
      1.00f, 0.28f, 0.68f, 0.52f, // CHORUS ON wide
      1.00f, 0.55f, 0.40f, 0.26f, // DELAY ON
      1.00f, 0.86f, 0.22f, 0.52f, // REVERB ON hall
  ], // 5: Rolling Psy Bass — mono Retrig Saw+Square -1 oct + dual zap filter + tight Dist ON
    [
      0.00f, 0.25f, 0.50f, 0.00f, 0.00f, 0.00f, 1.00f, 0.88f, // Osc A: Retrig Saw -1 oct
      0.00f, 0.24f,
      0.34f, 0.38f, 0.86f, // Filter A: LP24 psy zap
      0.00f, 0.54f, 0.32f, 0.40f, 0.00f, 0.52f, 0.05f,
      0.40f, 0.28f, 0.25f, 0.50f, 0.00f, 0.00f, 0.00f, 1.00f, 0.28f, // Osc B: Retrig Square snap
      0.62f,
      0.48f, 0.28f, 0.42f, 0.78f, // Filter B: BP12 acid edge
      0.00f, 0.42f, 0.15f, 0.30f, 0.00f, 0.44f,
      0.00f, 0.32f, 0.00f, 0.25f, 0.45f, 0.04f, 0.50f, 0.22f, 0.15f, 0.30f,
      0.00f, 0.00f, 0.80f, 0.12f, 0.80f, // Legato rolling bass
      1.00f, 0.38f, 0.46f, 0.42f, // DIST ON tight grit
      0.00f, 0.30f, 0.40f, 0.20f, 0.00f, 0.40f,
      0.30f, 0.20f, 0.00f, 0.40f, 0.50f, 0.15f,
  ],];

  int d_sylph_apply_preset(ref SylphDspState state, int preset_idx) nothrow @nogc @safe {
    int idx = preset_idx;
    if (idx < 0 || idx >= kSylphNumPresets)
      idx = 0;
    foreach (i; 0 .. kSylphNumParams) {
      d_sylph_set_parameter(state, cast(uint)i, SYLPH_PRESETS[idx][i]);
    }
    return kSylphNumParams;
  }

  pure nothrow @nogc @safe double d_sylph_get_default_value(int id) {
    if (id < 0 || id >= kSylphNumParams)
      return 0.0;
    return cast(double)SYLPH_PRESETS[0][id];
  }

  pure nothrow @nogc @safe SylphWaveform d_sylph_norm_to_wave(float norm) {
    if (norm < 0.25f)
      return SylphWaveform.SAW;
    if (norm < 0.50f)
      return SylphWaveform.SQUARE;
    if (norm < 0.75f)
      return SylphWaveform.TRIANGLE;
    return SylphWaveform.SINE;
  }

  pure nothrow @nogc @safe SylphPlayMode d_sylph_norm_to_play_mode(float norm) {
    if (norm < 0.34f)
      return SylphPlayMode.POLY;
    if (norm < 0.67f)
      return SylphPlayMode.MONO;
    return SylphPlayMode.LEGATO;
  }

  pure nothrow @nogc @safe SylphFilterType d_sylph_norm_to_filter_type(float norm) {
    if (norm < 0.25f)
      return SylphFilterType.LP24;
    if (norm < 0.50f)
      return SylphFilterType.LP12;
    if (norm < 0.75f)
      return SylphFilterType.BP12;
    return SylphFilterType.HP12;
  }

  pure nothrow @nogc @safe int d_sylph_norm_to_unison(float norm) {
    int u = cast(int)(clampVal(norm, 0.0f, 1.0f) * 6.0f + 0.5f) + 1;
    if (u < 1)
      u = 1;
    if (u > kSylphMaxUnison)
      u = kSylphMaxUnison;
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
    if (dt <= 0.0)
      return 0.0;
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

  private nothrow @nogc @trusted float renderOscSample(SylphWaveform wf, double phase,
      double dt) {
    final switch (wf) {
    case SylphWaveform.SAW:
      return cast(float)(2.0 * phase - 1.0 - polyBlep(phase, dt));
    case SylphWaveform.SQUARE: {
        double v = phase < 0.5 ? 1.0 : -1.0;
        v += polyBlep(phase, dt);
        double p2 = phase + 0.5;
        if (p2 >= 1.0)
          p2 -= 1.0;
        v -= polyBlep(p2, dt);
        return cast(float)v;
      }
    case SylphWaveform.TRIANGLE:
      return cast(float)(phase < 0.5 ?
          (4.0 * phase - 1.0) : (3.0 - 4.0 * phase));
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

  private void syncVoiceEnvs(const ref SylphDspState state, ref SylphVoice v) nothrow @nogc @safe {
    v.ampa_env.setNormalized(state.params[SylphParamId.P_AMPA_A],
        state.params[SylphParamId.P_AMPA_D],
        state.params[SylphParamId.P_AMPA_S],
        state.params[SylphParamId.P_AMPA_R]);
    v.filta_env.setNormalized(state.params[SylphParamId.P_FILTA_A],
        state.params[SylphParamId.P_FILTA_D],
        state.params[SylphParamId.P_FILTA_S], state.params[SylphParamId.P_FILTA_R]);
    v.ampb_env.setNormalized(state.params[SylphParamId.P_AMPB_A],
        state.params[SylphParamId.P_AMPB_D],
        state.params[SylphParamId.P_AMPB_S],
        state.params[SylphParamId.P_AMPB_R]);
    v.filtb_env.setNormalized(state.params[SylphParamId.P_FILTB_A],
        state.params[SylphParamId.P_FILTB_D],
        state.params[SylphParamId.P_FILTB_S], state.params[SylphParamId.P_FILTB_R]);
  }

  private void initVoiceTrigger(ref SylphDspState state, ref SylphVoice v,
      int pitch, float velocity, float pan, bool is_legato_slur) nothrow @nogc @safe {
    const float newFreq = midiNoteToHz(cast(float)pitch);
    v.active = true;
    v.held = true;
    v.note = pitch;
    v.velocity = clampVal(velocity, 0.0f, 1.0f);
    v.pan = clampVal(pan, -1.0f, 1.0f);
    v.target_freq = newFreq;

    if (!is_legato_slur) {
      const float glideNorm = state.params[SylphParamId.P_GLIDE];
      v.cur_freq = (glideNorm > 0.005f && state.has_last_freq) ? state
        .last_mono_freq : newFreq;
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
      v.filta1_l.reset();
      v.filta2_l.reset();
      v.filta1_r.reset();
      v.filta2_r.reset();
      v.filtb1_l.reset();
      v.filtb2_l.reset();
      v.filtb1_r.reset();
      v.filtb2_r.reset();

      syncVoiceEnvs(state, v);
      v.ampa_env.noteOn();
      v.filta_env.noteOn();
      v.ampb_env.noteOn();
      v.filtb_env.noteOn();
    } else {
      state.last_mono_freq = newFreq;
      state.has_last_freq = true;
    }
    v.age = ++state.voice_counter;
  }

  void d_sylph_note_on(ref SylphDspState state, int pitch, float velocity, float pan = 0.0f) nothrow @nogc @safe {
    const mode = d_sylph_norm_to_play_mode(state.params[SylphParamId.P_PLAY_MODE]);
    if (mode != SylphPlayMode.POLY) {
      foreach (i; 1 .. kSylphMaxVoices) {
        state.voices[i].active = false;
        state.voices[i].held = false;
      }
      auto v = &state.voices[0];
      const bool isLegatoSlur = (mode == SylphPlayMode.LEGATO) && v.active && v
        .held;
      initVoiceTrigger(state, *v, pitch, velocity, pan, isLegatoSlur);
      return;
    }

    int target = -1;
    foreach (i; 0 .. kSylphMaxVoices) {
      if (!state.voices[i].active && state.voices[i].ampa_env.isIdle() &&
          state.voices[i].ampb_env.isIdle()) {
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
    if (target < 0)
      target = 0;
    initVoiceTrigger(state, state.voices[target], pitch, velocity, pan, false);
  }

  void d_sylph_note_off(ref SylphDspState state, int pitch) nothrow @nogc @safe {
    foreach (ref v; state.voices) {
      if (v.active && (pitch < 0 || pitch > 127 || v.note == pitch)) {
        v.held = false;
        v.ampa_env.noteOff();
        v.filta_env.noteOff();
        v.ampb_env.noteOff();
        v.filtb_env.noteOff();
      }
    }
  }

  void d_sylph_set_parameter(ref SylphDspState state, uint id, float value) nothrow @nogc @safe {
    if (id >= kSylphNumParams)
      return;
    state.params[id] = clampVal(value, 0.0f, 1.0f);
    if ((id >= SylphParamId.P_AMPA_A && id <= SylphParamId.P_FILTA_R) ||
        (id >= SylphParamId.P_AMPB_A && id <= SylphParamId.P_FILTB_R)) {
      foreach (ref v; state.voices) {
        syncVoiceEnvs(state, v);
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
    if (++idx >= cast(int)buf.length)
      idx = 0;
    return outVal;
  }

  extern (D) private float processComb(T)(ref T buf, ref int idx, float input, float fb,
      float damp) nothrow @nogc @safe {
    const float delayed = buf[idx];
    buf[idx] = input + delayed * fb * (1.0f - damp);
    if (++idx >= cast(int)buf.length)
      idx = 0;
    return delayed;
  }

  private void applyFilterPair(ref SylphZdfSvf f1L, ref SylphZdfSvf f2L,
      ref SylphZdfSvf f1R, ref SylphZdfSvf f2R,
      ref float l, ref float r, SylphFilterType ftype, float cutBaseHz,
      float envNorm,
      float envOct, float lfoCut, float kRes, float driveNorm, float sr) nothrow @nogc @trusted {
    if (driveNorm > 0.01f) {
      const float dg = 1.0f + driveNorm * 4.5f;
      const float di = 1.0f / tanhf(dg);
      l = tanhf(l * dg) * di;
      r = tanhf(r * dg) * di;
    }
    float dynCut = cutBaseHz * powf(2.0f, envNorm * envOct + lfoCut);
    dynCut = clampVal(dynCut, 20.0f, sr * 0.42f);
    const float g = tanf(cast(float)pi * dynCut / sr);
    final switch (ftype) {
    case SylphFilterType.LP24:
      l = f2L.process(f1L.process(l, g, kRes, 0), g, kRes * 0.65f, 0);
      r = f2R.process(f1R.process(r, g, kRes, 0), g, kRes * 0.65f, 0);
      break;
    case SylphFilterType.LP12:
      l = f1L.process(l, g, kRes, 0);
      r = f1R.process(r, g, kRes, 0);
      break;
    case SylphFilterType.BP12:
      l = f1L.process(l, g, kRes, 1);
      r = f1R.process(r, g, kRes, 1);
      break;
    case SylphFilterType.HP12:
      l = f1L.process(l, g, kRes, 2);
      r = f1R.process(r, g, kRes, 2);
      break;
    }
  }

  void d_sylph_process(ref SylphDspState state, float* out_l, float* out_r,
      int num_samples,
      double sample_rate, const(SylphMidiEvent)* events, int num_events) nothrow @nogc @trusted {
    if (num_samples <= 0 || out_l is null || out_r is null)
      return;
    if (sample_rate <= 100.0)
      sample_rate = 44100.0;
    state.sample_rate = sample_rate;
    const float sr = cast(float)sample_rate;

    if (events !is null) {
      foreach (i; 0 .. num_events) {
        if (events[i].is_note_on && events[i].velocity > 0.0f) {
          d_sylph_note_on(state, events[i].pitch, events[i].velocity, events[i]
              .pan);
        } else {
          d_sylph_note_off(state, events[i].pitch);
        }
      }
    }

    // Part A config
    const wf1 = d_sylph_norm_to_wave(state.params[SylphParamId.P_OSC1_WAVE]);
    const float ratio1 = powf(2.0f, cast(float)d_sylph_norm_to_oct(
        state.params[SylphParamId.P_OSC1_OCT]) + cast(
        float)d_sylph_norm_to_semi(state.params[SylphParamId.P_OSC1_SEMI]) / 12.0f);
    const int uni1 = d_sylph_norm_to_unison(state.params[SylphParamId
        .P_OSC1_UNISON]);
    const float spread1 = state.params[SylphParamId.P_OSC1_DETUNE] *
      state.params[SylphParamId.P_OSC1_DETUNE] * 0.055f;
    const float st1 = state.params[SylphParamId.P_OSC1_STEREO];
    const float uniGain1 = state.params[SylphParamId.P_OSC1_VOL] / powf(cast(float)uni1,
        0.52f);

    const ftA = d_sylph_norm_to_filter_type(state.params[SylphParamId
        .P_FILTA_TYPE]);
    const float cutAHz = 20.0f * powf(900.0f, state.params[SylphParamId
        .P_FILTA_CUT]);
    const float kResA = 2.0f * (1.0f - clampVal(
        state.params[SylphParamId.P_FILTA_RES] * 0.94f, 0.0f, 0.94f));
    const float drvA = state.params[SylphParamId.P_FILTA_DRIVE];
    const float envOctA = (state.params[SylphParamId.P_FILTA_ENV_AMT] - 0.5f) * 10.0f;

    // Part B config
    const wf2 = d_sylph_norm_to_wave(state.params[SylphParamId.P_OSC2_WAVE]);
    const float ratio2 = powf(2.0f, cast(float)d_sylph_norm_to_oct(
        state.params[SylphParamId.P_OSC2_OCT]) + cast(
        float)d_sylph_norm_to_semi(state.params[SylphParamId.P_OSC2_SEMI]) / 12.0f);
    const int uni2 = d_sylph_norm_to_unison(state.params[SylphParamId
        .P_OSC2_UNISON]);
    const float spread2 = state.params[SylphParamId.P_OSC2_DETUNE] *
      state.params[SylphParamId.P_OSC2_DETUNE] * 0.055f;
    const float st2 = state.params[SylphParamId.P_OSC2_STEREO];
    const float uniGain2 = state.params[SylphParamId.P_OSC2_VOL] / powf(cast(float)uni2,
        0.52f);

    const ftB = d_sylph_norm_to_filter_type(state.params[SylphParamId
        .P_FILTB_TYPE]);
    const float cutBHz = 20.0f * powf(900.0f, state.params[SylphParamId
        .P_FILTB_CUT]);
    const float kResB = 2.0f * (1.0f - clampVal(
        state.params[SylphParamId.P_FILTB_RES] * 0.94f, 0.0f, 0.94f));
    const float drvB = state.params[SylphParamId.P_FILTB_DRIVE];
    const float envOctB = (state.params[SylphParamId.P_FILTB_ENV_AMT] - 0.5f) * 10.0f;

    // Sub / Punch / Mod config
    const float subNorm = state.params[SylphParamId.P_SUB_WAVE];
    const SylphWaveform subWf = subNorm < 0.34f ? SylphWaveform.SINE
      : (subNorm < 0.67f ? SylphWaveform.TRIANGLE : SylphWaveform.SQUARE);
    const float subOctNorm = state.params[SylphParamId.P_SUB_OCT];
    const float subRatio = subOctNorm < 0.34f ? 0.25f : (subOctNorm < 0.67f ? 0.5f : 1.0f);
    const float subVol = state.params[SylphParamId.P_SUB_VOL];
    const float clickNoise = state.params[SylphParamId.P_CLICK_NOISE];

    const float pitchSt = (state.params[SylphParamId.P_PITCH_AMT] - 0.5f) * 96.0f;
    const float pDecSec = 0.004f * powf(125.0f, state.params[SylphParamId
        .P_PITCH_DECAY]);
    const float pEnvCoef = expf(-1.0f / (pDecSec * sr));
    const float pCurveExp = 3.0f - state.params[SylphParamId.P_PITCH_CURVE] * 2.1f;
    const float clickCoef = expf(-1.0f / (0.003f * sr));

    const float glideNorm = state.params[SylphParamId.P_GLIDE];
    const float glideCoef = (glideNorm > 0.005f) ? (
        1.0f - expf(-1.0f / ((0.004f + glideNorm * 0.35f) * sr))) : 1.0f;

    const float lfoRateHz = 0.1f * powf(200.0f, state.params[SylphParamId
        .P_LFO_RATE]);
    const double lfoInc = cast(double)lfoRateHz / sample_rate;
    const float lfoPitchOct = state.params[SylphParamId.P_LFO_PITCH] * (1.0f / 12.0f);
    const float lfoCutOct = state.params[SylphParamId.P_LFO_CUT] * 3.0f;
    const float masterVol = state.params[SylphParamId.P_VOLUME];

    // Serial Master FX Chain config
    const bool distOn = state.params[SylphParamId.P_DIST_ON] >= 0.5f;
    const float distGain = 1.0f + state.params[SylphParamId.P_DIST_DRIVE] * 6.5f;
    const float distInv = 1.0f / tanhf(distGain);
    const float distToneCoef = 0.06f + state.params[SylphParamId.P_DIST_TONE] * 0.88f;
    const float distMix = state.params[SylphParamId.P_DIST_MIX];

    const bool chorusOn = state.params[SylphParamId.P_CHORUS_ON] >= 0.5f;
    const float chorusRateHz = 0.15f + state.params[SylphParamId.P_CHORUS_RATE] * 2.5f;
    const float chorusDepthSec = 0.001f + state.params[SylphParamId
        .P_CHORUS_DEPTH] * 0.006f;
    const float chorusMix = state.params[SylphParamId.P_CHORUS_MIX];

    const bool delayOn = state.params[SylphParamId.P_DELAY_ON] >= 0.5f;
    int delaySamp = cast(int)((0.08f + state.params[SylphParamId.P_DELAY_TIME] * 0.47f) * sr);
    if (delaySamp < 64)
      delaySamp = 64;
    if (delaySamp >= kSylphDelayMax - 1)
      delaySamp = kSylphDelayMax - 2;
    const float delayFb = clampVal(state.params[SylphParamId.P_DELAY_FB] * 0.82f, 0.0f,
        0.82f);
    const float delayMix = state.params[SylphParamId.P_DELAY_MIX];

    const bool reverbOn = state.params[SylphParamId.P_REVERB_ON] >= 0.5f;
    const float revFb = 0.62f + state.params[SylphParamId.P_REVERB_SIZE] * 0.25f;
    const float revDamp = 0.04f + state.params[SylphParamId.P_REVERB_DAMP] * 0.35f;
    const float reverbMix = state.params[SylphParamId.P_REVERB_MIX];

    foreach (i; 0 .. num_samples) {
      float mixL = 0.0f;
      float mixR = 0.0f;

      const float lfo = sinf(cast(float)(state.lfo_phase * (2.0 * pi)));
      state.lfo_phase += lfoInc;
      if (state.lfo_phase >= 1.0)
        state.lfo_phase -= 1.0;

      const float lfoPitchMul = (lfoPitchOct > 0.0001f) ? powf(2.0f, lfo * lfoPitchOct) : 1.0f;
      const float lfoCutMod = lfo * lfoCutOct;

      foreach (ref v; state.voices) {
        if (!v.active)
          continue;

        const float gainA = v.ampa_env.process(sr);
        const float envFiltA = v.filta_env.process(sr);
        const float gainB = v.ampb_env.process(sr);
        const float envFiltB = v.filtb_env.process(sr);
        if (v.ampa_env.isIdle() && v.ampb_env.isIdle()) {
          v.active = false;
          continue;
        }

        v.cur_freq += (v.target_freq - v.cur_freq) * glideCoef;
        v.pitch_env *= pEnvCoef;
        v.click_env *= clickCoef;
        float pitchModMul = lfoPitchMul;
        if (fabsf(pitchSt) > 0.05f && v.pitch_env > 0.0001f) {
          pitchModMul *= powf(2.0f, (pitchSt * powf(v.pitch_env, pCurveExp)) / 12.0f);
        }
        const float instFreq = clampVal(v.cur_freq * pitchModMul, 10.0f, sr * 0.45f);

        // --- Part A (Osc A + Sub + Click -> Filter A -> Amp Env A) ---
        float partAL = 0.0f;
        float partAR = 0.0f;
        if (uniGain1 > 0.0005f) {
          const float baseF1 = instFreq * ratio1;
          foreach (u; 0 .. uni1) {
            const float df = 1.0f + UNISON_DETUNE_OFFSETS[u] * spread1;
            const double dt = clampVal(cast(double)(baseF1 * df) / sample_rate, 0.00001,
                0.45);
            const float smp = renderOscSample(wf1, v.osc1_phase[u], dt) * uniGain1;
            const float panU = UNISON_PAN_OFFSETS[u] * st1;
            partAL += smp * (0.5f * (1.0f - panU));
            partAR += smp * (0.5f * (1.0f + panU));
            v.osc1_phase[u] += dt;
            if (v.osc1_phase[u] >= 1.0)
              v.osc1_phase[u] -= 1.0;
          }
        }
        if (subVol > 0.0005f) {
          const double dtSub = clampVal(
              cast(double)(instFreq * subRatio) / sample_rate, 0.00001, 0.45);
          const float subSmp = renderOscSample(subWf, v.sub_phase, dtSub) * subVol * 0.55f;
          partAL += subSmp;
          partAR += subSmp;
          v.sub_phase += dtSub;
          if (v.sub_phase >= 1.0)
            v.sub_phase -= 1.0;
        }
        if (clickNoise > 0.0005f) {
          const float nz = nextNoise(state.rng_state);
          const float beaterGain = (fabsf(pitchSt) > 3.0f) ? (clickNoise * v
              .click_env * 0.85f) : 0.0f;
          const float cSmp = nz * (clickNoise * 0.14f + beaterGain);
          partAL += cSmp;
          partAR += cSmp;
        }
        applyFilterPair(v.filta1_l, v.filta2_l, v.filta1_r, v.filta2_r, partAL,
            partAR, ftA, cutAHz, envFiltA, envOctA, lfoCutMod, kResA, drvA, sr);

        // --- Part B (Osc B -> Independent Filter B -> Amp Env B) ---
        float partBL = 0.0f;
        float partBR = 0.0f;
        if (uniGain2 > 0.0005f) {
          const float baseF2 = instFreq * ratio2;
          foreach (u; 0 .. uni2) {
            const float df = 1.0f + UNISON_DETUNE_OFFSETS[u] * spread2;
            const double dt = clampVal(cast(double)(baseF2 * df) / sample_rate, 0.00001,
                0.45);
            const float smp = renderOscSample(wf2, v.osc2_phase[u], dt) * uniGain2;
            const float panU = UNISON_PAN_OFFSETS[u] * st2;
            partBL += smp * (0.5f * (1.0f - panU));
            partBR += smp * (0.5f * (1.0f + panU));
            v.osc2_phase[u] += dt;
            if (v.osc2_phase[u] >= 1.0)
              v.osc2_phase[u] -= 1.0;
          }
        }
        applyFilterPair(v.filtb1_l, v.filtb2_l, v.filtb1_r, v.filtb2_r, partBL,
            partBR, ftB, cutBHz, envFiltB, envOctB, lfoCutMod, kResB, drvB, sr);

        const float gPanL = 1.0f - clampVal(v.pan, 0.0f, 1.0f);
        const float gPanR = 1.0f + clampVal(v.pan, -1.0f, 0.0f);
        const float vVel = v.velocity * 0.42f;
        mixL += (partAL * gainA + partBL * gainB) * vVel * gPanL;
        mixR += (partAR * gainA + partBR * gainB) * vVel * gPanR;
      }

      // --- Serial FX Block 1: DISTORTION ---
      if (distOn && distMix > 0.005f) {
        const float satL = tanhf(mixL * distGain) * distInv;
        const float satR = tanhf(mixR * distGain) * distInv;
        state.fx.dist_lp_l += (satL - state.fx.dist_lp_l) * distToneCoef;
        state.fx.dist_lp_r += (satR - state.fx.dist_lp_r) * distToneCoef;
        mixL = mixL * (1.0f - distMix) + state.fx.dist_lp_l * distMix;
        mixR = mixR * (1.0f - distMix) + state.fx.dist_lp_r * distMix;
      }

      // --- Serial FX Block 2: CHORUS ---
      if (chorusOn && chorusMix > 0.005f) {
        state.fx.chorus_l[state.fx.chorus_write] = mixL;
        state.fx.chorus_r[state.fx.chorus_write] = mixR;
        const float cLfo = sinf(cast(float)(state.fx.chorus_lfo * (2.0 * pi)));
        state.fx.chorus_lfo += cast(double)chorusRateHz / sample_rate;
        if (state.fx.chorus_lfo >= 1.0)
          state.fx.chorus_lfo -= 1.0;

        const int tapL = cast(int)((0.011f + chorusDepthSec * cLfo) * sr);
        const int tapR = cast(int)((0.014f - chorusDepthSec * cLfo) * sr);
        const int idxL = (state.fx.chorus_write - tapL + kSylphChorusMax) & (kSylphChorusMax - 1);
        const int idxR = (state.fx.chorus_write - tapR + kSylphChorusMax) & (kSylphChorusMax - 1);
        const float wetL = state.fx.chorus_r[idxL];
        const float wetR = state.fx.chorus_l[idxR];
        mixL = mixL * (1.0f - 0.35f * chorusMix) + wetL * (0.65f * chorusMix);
        mixR = mixR * (1.0f - 0.35f * chorusMix) + wetR * (0.65f * chorusMix);
        state.fx.chorus_write = (state.fx.chorus_write + 1) & (kSylphChorusMax - 1);
      }

      // --- Serial FX Block 3: PING-PONG DELAY ---
      if (delayOn && delayMix > 0.005f) {
        const int rdL = (state.fx.delay_write - delaySamp + kSylphDelayMax) % kSylphDelayMax;
        const int rdR = (state.fx.delay_write - (delaySamp * 3) / 4 + kSylphDelayMax) % kSylphDelayMax;
        const float dOutL = state.fx.delay_l[rdL];
        const float dOutR = state.fx.delay_r[rdR];
        state.fx.delay_l[state.fx.delay_write] = mixL + dOutR * delayFb;
        state.fx.delay_r[state.fx.delay_write] = mixR + dOutL * delayFb;
        if (++state.fx.delay_write >= kSylphDelayMax)
          state.fx.delay_write = 0;
        mixL += dOutL * delayMix * 0.65f;
        mixR += dOutR * delayMix * 0.65f;
      }

      // --- Serial FX Block 4: STEREO REVERB ---
      if (reverbOn && reverbMix > 0.005f) {
        const float revInL = mixL * 0.25f;
        const float revInR = mixR * 0.25f;
        float rL = processComb(state.fx.c1_l, state.fx.comb_idx[0], revInL, revFb,
            revDamp) + processComb(state.fx.c2_l, state.fx.comb_idx[1], revInL,
            revFb * 0.99f, revDamp) + processComb(state.fx.c3_l,
            state.fx.comb_idx[2],
            revInL, revFb * 0.98f, revDamp) + processComb(state.fx.c4_l,
            state.fx.comb_idx[3], revInL, revFb * 0.97f, revDamp);
        float rR = processComb(state.fx.c1_r, state.fx.comb_idx_r[0], revInR, revFb,
            revDamp) + processComb(state.fx.c2_r, state.fx.comb_idx_r[1],
            revInR, revFb * 0.99f, revDamp) + processComb(state.fx.c3_r,
            state.fx.comb_idx_r[2],
            revInR, revFb * 0.98f, revDamp) + processComb(state.fx.c4_r,
            state.fx.comb_idx_r[3], revInR, revFb * 0.97f, revDamp);
        rL = processAllpass(state.fx.a1_l, state.fx.ap_idx[0], rL, 0.5f);
        rL = processAllpass(state.fx.a2_l, state.fx.ap_idx[1], rL, 0.5f);
        rR = processAllpass(state.fx.a1_r, state.fx.ap_idx_r[0], rR, 0.5f);
        rR = processAllpass(state.fx.a2_r, state.fx.ap_idx_r[1], rR, 0.5f);
        mixL += rL * reverbMix * 0.45f;
        mixR += rR * reverbMix * 0.45f;
      }

      out_l[i] = tanhf(mixL * masterVol * 1.4f);
      out_r[i] = tanhf(mixR * masterVol * 1.4f);
    }
  }

} // extern (C++, "hibiki")
