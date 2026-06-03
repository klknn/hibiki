#include "engine/instruments/builtin_epiano.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "engine/core/math.hpp"
#include "engine/instruments/builtin_epiano_data.h"

namespace hibiki {

static const std::string kEPianoName = "Electric Piano";
static const std::string kEPianoPath = "builtin://epiano";

struct BuiltinEPiano::Impl {
  static constexpr int kMaxVoices = 32;

  struct Voice {
    int32_t delta = 0;
    int32_t frac = 0;
    int32_t pos = 0;
    int32_t end = 0;
    int32_t loop = 0;

    float env = 0.0f;
    float dec = 0.99f;

    float f0 = 0.0f;
    float f1 = 0.0f;
    float ff = 0.0f;

    float outl = 0.0f;
    float outr = 0.0f;
    int note = -1;
  };

  struct KeyGroup {
    int root = 0;
    int high = 0;
    int pos = 0;
    int end = 0;
    int loop = 0;
  };

  double params[kTotalParams] = {};
  double sample_rate = 44100.0;
  double iFs = 1.0 / 44100.0;

  Voice voices[kMaxVoices];
  KeyGroup kgrp[34];
  std::vector<int16_t> waves;

  int activevoices = 0;
  int poly = 16;
  float width = 0.0f;
  int size = 0;
  float lfo0 = 0.0f;
  float lfo1 = 1.0f;
  float dlfo = 0.0f;
  float lmod = 0.0f;
  float rmod = 0.0f;
  float treb = 0.0f;
  float tfrq = 0.5f;
  float tl = 0.0f;
  float tr = 0.0f;
  float fine = 0.0f;
  float random = 0.0f;
  float stretch = 0.0f;
  float overdrive = 0.0f;
  float muff = 160.0f;
  float muffvel = 0.0f;
  float velsens = 1.0f;
  float volume = 0.2f;
  float modulation = 0.0f;
  float decay = 0.0f;
  float release = 0.0f;

  void reset() {
    for (int i = 0; i < kMaxVoices; ++i) {
      voices[i] = Voice();
    }
    activevoices = 0;
    volume = 0.2f;
    muff = 160.0f;
    sustain = 0;
    activevoices = 0;
    tl = tr = lfo0 = dlfo = 0.0f;
    lfo1 = 1.0f;
  }

  void processParams() {
    size = (int)(12.0f * params[P_HARDNESS] - 6.0f);

    float trebNormalized = (float)params[P_TREBLE_BOOST];
    treb = 4.0f * trebNormalized * trebNormalized - 1.0f;
    tfrq = (trebNormalized > 0.5f) ? 14000.0f : 5000.0f;
    tfrq = 1.0f - (float)std::exp(-iFs * tfrq);

    float modNormalized = (float)params[P_MODULATION];
    rmod = lmod = 2.0f * modNormalized - 1.0f;
    if (modNormalized < 0.5f) rmod = -rmod;

    dlfo = 6.283f * (float)iFs *
           (float)std::exp(6.22f * params[P_LFO_RATE] - 2.61f);

    float velsensNormalized = (float)params[P_VEL_SENSE];
    velsens = 2.0f * velsensNormalized + 1.0f;
    if (velsensNormalized < 0.25f) velsens -= 0.75f - 3.0f * velsensNormalized;

    width = 0.03f * (float)params[P_STEREO_WIDTH];
    poly = 1 + (int)(31.9f * params[P_POLYPHONY]);
    if (poly > kMaxVoices) poly = kMaxVoices;
    fine = (float)params[P_FINE_TUNING] - 0.5f;
    float randomNormalized = (float)params[P_RANDOM_TUNING];
    random = 0.077f * randomNormalized * randomNormalized;
    stretch = 0.0f;
    overdrive = 1.8f * (float)params[P_OVERDRIVE];

    release = (float)params[P_ENV_RELEASE];
    decay = (float)params[P_ENV_DECAY];
    modulation = (float)params[P_MODULATION];
  }

  void noteOn(int note, int velocity) {
    int vl = -1;
    if (activevoices < poly) {
      vl = activevoices;
      activevoices++;
      voices[vl].f0 = voices[vl].f1 = 0.0f;
    } else {
      float min_env = std::numeric_limits<float>::infinity();
      for (int v = 0; v < poly; ++v) {
        if (voices[v].env < min_env) {
          min_env = voices[v].env;
          vl = v;
        }
      }
    }

    if (vl == -1) return;

    int k = (note - 60) * (note - 60);
    float l = fine + random * ((float)(k % 13) - 6.5f);
    if (note > 60) l += stretch * (float)k;

    k = 0;
    while (note > (kgrp[k].high + size)) k += 3;

    l += (float)(note - kgrp[k].root);
    l = 32000.0f * (float)iFs * (float)std::exp(0.05776226505 * l);
    voices[vl].delta = (int32_t)(65536.0f * l);
    voices[vl].frac = 0;

    if (velocity > 48) k++;
    if (velocity > 80) k++;
    voices[vl].pos = kgrp[k].pos;
    voices[vl].end = kgrp[k].end - 1;
    voices[vl].loop = kgrp[k].loop;

    voices[vl].env =
        (3.0f + 2.0f * velsens) * (float)std::pow(0.0078f * velocity, velsens);

    if (note > 60)
      voices[vl].env *= (float)std::exp(0.01f * (float)(60 - note));

    l = 50.0f + modulation * modulation * muff +
        muffvel * (float)(velocity - 64);
    if (l < (55.0f + 0.4f * (float)note)) l = 55.0f + 0.4f * (float)note;
    if (l > 210.0f) l = 210.0f;
    voices[vl].ff = l * l * (float)iFs;

    voices[vl].note = note;
    int pan_note = note;
    if (pan_note < 12) pan_note = 12;
    if (pan_note > 108) pan_note = 108;
    l = volume;
    voices[vl].outr = l + l * width * (float)(pan_note - 60);
    voices[vl].outl = l + l - voices[vl].outr;

    int decay_note = note;
    if (decay_note < 44) decay_note = 44;
    voices[vl].dec = (float)std::exp(
        -iFs * std::exp(-1.0 + 0.03 * (double)decay_note - 2.0f * decay));
  }

  void noteOff(int note) {
    float release_dec = (float)std::exp(
        -iFs * std::exp(6.0 + 0.01 * (double)note - 5.0 * release));
    for (int v = 0; v < kMaxVoices; ++v) {
      if (voices[v].note == note) {
        if (sustain == 0) {
          voices[v].dec = release_dec;
        } else {
          voices[v].note = kSustainMarker;
        }
      }
    }
  }

  int sustain = 0;
  static constexpr int kSustainMarker = 128;
};

BuiltinEPiano::BuiltinEPiano() : impl_(std::make_unique<Impl>()) {
  impl_->params[P_ENV_DECAY] = 0.500;
  impl_->params[P_ENV_RELEASE] = 0.500;
  impl_->params[P_HARDNESS] = 0.500;
  impl_->params[P_TREBLE_BOOST] = 0.500;
  impl_->params[P_MODULATION] = 0.500;
  impl_->params[P_LFO_RATE] = 0.650;
  impl_->params[P_VEL_SENSE] = 0.250;
  impl_->params[P_STEREO_WIDTH] = 0.500;
  impl_->params[P_POLYPHONY] = 0.500;
  impl_->params[P_FINE_TUNING] = 0.500;
  impl_->params[P_RANDOM_TUNING] = 0.146;
  impl_->params[P_OVERDRIVE] = 0.000;
  impl_->params[P_VOLUME] = 0.700;
  impl_->params[P_ENABLE] = 1.000;

  impl_->waves.assign(
      epianoData, epianoData + (sizeof(epianoData) / sizeof(epianoData[0])));
  impl_->waves.push_back(0);  // Safety element for interpolation

  // Initialize KeyGroups
  for (int i = 0; i < 34; ++i) {
    impl_->kgrp[i] = Impl::KeyGroup();
  }

  impl_->kgrp[0].root = 36;
  impl_->kgrp[0].high = 39;
  impl_->kgrp[3].root = 43;
  impl_->kgrp[3].high = 45;
  impl_->kgrp[6].root = 48;
  impl_->kgrp[6].high = 51;
  impl_->kgrp[9].root = 55;
  impl_->kgrp[9].high = 57;
  impl_->kgrp[12].root = 60;
  impl_->kgrp[12].high = 63;
  impl_->kgrp[15].root = 67;
  impl_->kgrp[15].high = 69;
  impl_->kgrp[18].root = 72;
  impl_->kgrp[18].high = 75;
  impl_->kgrp[21].root = 79;
  impl_->kgrp[21].high = 81;
  impl_->kgrp[24].root = 84;
  impl_->kgrp[24].high = 87;
  impl_->kgrp[27].root = 91;
  impl_->kgrp[27].high = 93;
  impl_->kgrp[30].root = 96;
  impl_->kgrp[30].high = 999;

  impl_->kgrp[0].pos = 0;
  impl_->kgrp[0].end = 8476;
  impl_->kgrp[0].loop = 4400;
  impl_->kgrp[1].pos = 8477;
  impl_->kgrp[1].end = 16248;
  impl_->kgrp[1].loop = 4903;
  impl_->kgrp[2].pos = 16249;
  impl_->kgrp[2].end = 34565;
  impl_->kgrp[2].loop = 6398;
  impl_->kgrp[3].pos = 34566;
  impl_->kgrp[3].end = 41384;
  impl_->kgrp[3].loop = 3938;
  impl_->kgrp[4].pos = 41385;
  impl_->kgrp[4].end = 45760;
  impl_->kgrp[4].loop = 1633;
  impl_->kgrp[5].pos = 45761;
  impl_->kgrp[5].end = 65211;
  impl_->kgrp[5].loop = 5245;
  impl_->kgrp[6].pos = 65212;
  impl_->kgrp[6].end = 72897;
  impl_->kgrp[6].loop = 2937;
  impl_->kgrp[7].pos = 72898;
  impl_->kgrp[7].end = 78626;
  impl_->kgrp[7].loop = 2203;
  impl_->kgrp[8].pos = 78627;
  impl_->kgrp[8].end = 100387;
  impl_->kgrp[8].loop = 6368;
  impl_->kgrp[9].pos = 100388;
  impl_->kgrp[9].end = 116297;
  impl_->kgrp[9].loop = 10452;
  impl_->kgrp[10].pos = 116298;
  impl_->kgrp[10].end = 127661;
  impl_->kgrp[10].loop = 5217;
  impl_->kgrp[11].pos = 127662;
  impl_->kgrp[11].end = 144113;
  impl_->kgrp[11].loop = 3099;
  impl_->kgrp[12].pos = 144114;
  impl_->kgrp[12].end = 152863;
  impl_->kgrp[12].loop = 4284;
  impl_->kgrp[13].pos = 152864;
  impl_->kgrp[13].end = 173107;
  impl_->kgrp[13].loop = 3916;
  impl_->kgrp[14].pos = 173108;
  impl_->kgrp[14].end = 192734;
  impl_->kgrp[14].loop = 2937;
  impl_->kgrp[15].pos = 192735;
  impl_->kgrp[15].end = 204598;
  impl_->kgrp[15].loop = 4732;
  impl_->kgrp[16].pos = 204599;
  impl_->kgrp[16].end = 218995;
  impl_->kgrp[16].loop = 4733;
  impl_->kgrp[17].pos = 218996;
  impl_->kgrp[17].end = 233801;
  impl_->kgrp[17].loop = 2285;
  impl_->kgrp[18].pos = 233802;
  impl_->kgrp[18].end = 248011;
  impl_->kgrp[18].loop = 4098;
  impl_->kgrp[19].pos = 248012;
  impl_->kgrp[19].end = 265287;
  impl_->kgrp[19].loop = 4099;
  impl_->kgrp[20].pos = 265288;
  impl_->kgrp[20].end = 282255;
  impl_->kgrp[20].loop = 3609;
  impl_->kgrp[21].pos = 282256;
  impl_->kgrp[21].end = 293776;
  impl_->kgrp[21].loop = 2446;
  impl_->kgrp[22].pos = 293777;
  impl_->kgrp[22].end = 312566;
  impl_->kgrp[22].loop = 6278;
  impl_->kgrp[23].pos = 312567;
  impl_->kgrp[23].end = 330200;
  impl_->kgrp[23].loop = 2283;
  impl_->kgrp[24].pos = 330201;
  impl_->kgrp[24].end = 348889;
  impl_->kgrp[24].loop = 2689;
  impl_->kgrp[25].pos = 348890;
  impl_->kgrp[25].end = 365675;
  impl_->kgrp[25].loop = 4370;
  impl_->kgrp[26].pos = 365676;
  impl_->kgrp[26].end = 383661;
  impl_->kgrp[26].loop = 5225;
  impl_->kgrp[27].pos = 383662;
  impl_->kgrp[27].end = 393372;
  impl_->kgrp[27].loop = 2811;
  impl_->kgrp[28].pos = 383662;
  impl_->kgrp[28].end = 393372;
  impl_->kgrp[28].loop = 2811;
  impl_->kgrp[29].pos = 393373;
  impl_->kgrp[29].end = 406045;
  impl_->kgrp[29].loop = 4522;
  impl_->kgrp[30].pos = 406046;
  impl_->kgrp[30].end = 414486;
  impl_->kgrp[30].loop = 2306;
  impl_->kgrp[31].pos = 406046;
  impl_->kgrp[31].end = 414486;
  impl_->kgrp[31].loop = 2306;
  impl_->kgrp[32].pos = 414487;
  impl_->kgrp[32].end = 422408;
  impl_->kgrp[32].loop = 2169;

  // Extra crossfade looping
  for (int k = 0; k < 28; k++) {
    int p0 = impl_->kgrp[k].end;
    int p1 = impl_->kgrp[k].end - impl_->kgrp[k].loop;

    float xf = 1.0f;
    float dxf = -0.02f;

    while (xf > 0.0f) {
      impl_->waves[p0] = (short)((1.0f - xf) * (float)impl_->waves[p0] +
                                 xf * (float)impl_->waves[p1]);
      p0--;
      p1--;
      xf += dxf;
    }
  }

  impl_->reset();
}

BuiltinEPiano::~BuiltinEPiano() = default;

bool BuiltinEPiano::load(const std::string& /*path*/, int /*plugin_index*/,
                         double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->iFs = 1.0 / sample_rate;
  impl_->reset();
  return true;
}

void BuiltinEPiano::process(float** /*inputs*/, float** outputs,
                            int num_samples, const HostProcessContext& context,
                            const std::vector<MidiNoteEvent>& events,
                            float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;
  impl_->iFs = 1.0 / context.sampleRate;

  impl_->processParams();

  float* outL = outputs[0];
  float* outR = outputs[1];

  for (int i = 0; i < num_samples; ++i) {
    outL[i] = 0.0f;
    outR[i] = 0.0f;
  }

  if (!impl_->params[P_ENABLE]) {
    return;
  }

  float master_vol = (float)impl_->params[P_VOLUME];

  size_t event_idx = 0;
  for (int i = 0; i < num_samples; ++i) {
    while (event_idx < events.size() && events[event_idx].sampleOffset <= i) {
      const auto& ev = events[event_idx];
      if (ev.isNoteOn && ev.velocity > 0.0f) {
        if (impl_->activevoices == 0 && impl_->modulation > 0.5f) {
          impl_->lfo0 = -0.7071f;
          impl_->lfo1 = 0.7071f;
        }
        impl_->noteOn(ev.pitch, (int)(ev.velocity * 127.0f));
      } else {
        impl_->noteOff(ev.pitch);
      }
      event_idx++;
    }

    float l = 0.0f;
    float r = 0.0f;

    for (int v = 0; v < impl_->activevoices; ++v) {
      auto& V = impl_->voices[v];
      V.frac += V.delta;
      V.pos += V.frac >> 16;
      V.frac &= 0xFFFF;
      if (V.pos > V.end) {
        V.pos -= V.loop;
      }

      int32_t val0 = impl_->waves[V.pos];
      int32_t val1 = impl_->waves[V.pos + 1];
      int32_t interp = val0 + ((V.frac * (val1 - val0)) >> 16);
      float x = V.env * (float)interp / 32768.0f;

      V.env *= V.dec;

      if (x > 0.0f) {
        x -= impl_->overdrive * x * x;
        if (x < -V.env) {
          x = -V.env;
        }
      }

      l += V.outl * x;
      r += V.outr * x;
    }

    impl_->tl += impl_->tfrq * (l - impl_->tl);
    impl_->tr += impl_->tfrq * (r - impl_->tr);
    r += impl_->treb * (r - impl_->tr);
    l += impl_->treb * (l - impl_->tl);

    impl_->lfo0 += impl_->dlfo * impl_->lfo1;
    impl_->lfo1 -= impl_->dlfo * impl_->lfo0;
    l += l * impl_->lmod * impl_->lfo1;
    r += r * impl_->rmod * impl_->lfo1;

    outL[i] = l * master_vol;
    outR[i] = r * master_vol;
  }

  if (std::abs(impl_->tl) < 1.0e-10f) impl_->tl = 0.0f;
  if (std::abs(impl_->tr) < 1.0e-10f) impl_->tr = 0.0f;

  constexpr float SILENCE = 0.0001f;
  for (int v = 0; v < impl_->activevoices;) {
    if (impl_->voices[v].env < SILENCE) {
      impl_->voices[v] = impl_->voices[--impl_->activevoices];
    } else {
      v++;
    }
  }
}

int BuiltinEPiano::getParameterCount() const { return kTotalParams; }

bool BuiltinEPiano::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  info.id = index;
  info.defaultValue = getParameterValue(index);
  static const char* names[] = {"Envelope Decay", "Envelope Release",
                                "Hardness",       "Treble Boost",
                                "Modulation",     "LFO Rate",
                                "Velocity Sense", "Stereo Width",
                                "Polyphony",      "Fine Tuning",
                                "Random Tuning",  "Overdrive",
                                "Volume",         "Enable"};
  info.name = names[index];
  return true;
}

void BuiltinEPiano::setParameterValue(uint32_t id, double value) {
  if (id >= kTotalParams) return;
  impl_->params[id] = value;
}

double BuiltinEPiano::getParameterValue(uint32_t id) const {
  return (id < kTotalParams) ? impl_->params[id] : 0.0;
}

const std::string& BuiltinEPiano::getName() const { return kEPianoName; }

const std::string& BuiltinEPiano::getPath() const { return kEPianoPath; }

int BuiltinEPiano::getPluginIndex() const { return 0; }

bool BuiltinEPiano::isInstrument() const { return true; }

}  // namespace hibiki
