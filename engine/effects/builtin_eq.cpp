#include "engine/effects/builtin_eq.hpp"

#include <cmath>
#include <string>

namespace hibiki {

static const std::string kEqName = BuiltinEq::kName;
static const std::string kEqPath = BuiltinEq::kPath;

static constexpr float defaultFreqs_[BuiltinEq::kNumBands] = {
    30.0f, 80.0f, 250.0f, 700.0f, 2000.0f, 5000.0f, 10000.0f, 16000.0f};

struct BuiltinEq::Impl {
  EqDspState* state = nullptr;
  double sample_rate = 44100.0;

  Impl() : state(d_eq_create()) {}
  ~Impl() { d_eq_destroy(state); }
  void reset() { d_eq_reset(state); }
};

BuiltinEq::BuiltinEq() : impl_(std::make_unique<Impl>()) { reset(); }

BuiltinEq::~BuiltinEq() = default;

bool BuiltinEq::load(const std::string& /*path*/, int /*plugin_index*/,
                     double sample_rate) {
  impl_->sample_rate = sample_rate;
  reset();
  return true;
}

void BuiltinEq::process(float** inputs, float** outputs, int num_samples,
                        const HostProcessContext& context,
                        const std::vector<MidiNoteEvent>& /*events*/,
                        float** /*sidechain*/) {
  if (impl_->sample_rate != context.sampleRate) {
    impl_->sample_rate = context.sampleRate;
    initSpectrumBins();
  }

  float* outL = outputs[0];
  float* outR = outputs[1];
  if (inputs && inputs != outputs) {
    for (int i = 0; i < num_samples; ++i) {
      outL[i] = inputs[0][i];
      outR[i] = inputs[1][i];
    }
  }

  // Accumulate input spectrum (before EQ processing)
  spectrum_analyzer_.accumulateToRing(outL, outR, num_samples, input_ring_);

  d_eq_process(impl_->state, outL, outR, num_samples, context.sampleRate);

  // Accumulate output spectrum (after EQ processing)
  spectrum_analyzer_.accumulateToRing(outL, outR, num_samples, output_ring_);
  spectrum_analyzer_.advanceRingPos(num_samples);
  spectrum_sample_count_ += num_samples;
  maybeUpdateSpectrum();
}

int BuiltinEq::getParameterCount() const { return kTotalParams; }

bool BuiltinEq::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  info.id = index;
  if (index < kNumBands) {
    info.name = "Band " + std::to_string(index + 1) + " Type";
    info.defaultValue = 0.0;  // OFF
  } else if (index < kNumBands * 2) {
    int b = index - kNumBands;
    info.name = "Band " + std::to_string(b + 1) + " Freq";
    info.defaultValue = freqToNorm(defaultFreqs_[b]);
  } else if (index < kNumBands * 3) {
    int b = index - kNumBands * 2;
    info.name = "Band " + std::to_string(b + 1) + " Gain";
    info.defaultValue = 0.5;  // 0 dB
  } else if (index < kNumBands * 4) {
    int b = index - kNumBands * 3;
    info.name = "Band " + std::to_string(b + 1) + " Q";
    info.defaultValue = qToNorm(0.707);
  } else {
    info.name = "Enable";
    info.defaultValue = 1.0;
  }
  return true;
}

void BuiltinEq::setParameterValue(uint32_t id, double value) {
  d_eq_set_parameter(impl_->state, id, value);
}

double BuiltinEq::getParameterValue(uint32_t id) const {
  return d_eq_get_parameter(impl_->state, id);
}

const std::string& BuiltinEq::getName() const { return kEqName; }

const std::string& BuiltinEq::getPath() const { return kEqPath; }

int BuiltinEq::getPluginIndex() const { return 0; }
bool BuiltinEq::isInstrument() const { return false; }

float BuiltinEq::getMagnitudeDb(float freq) const {
  return d_eq_get_magnitude_db(impl_->state, freq);
}

BuiltinEq::SpectrumData BuiltinEq::getSpectrumData() const {
  SpectrumData d;
  for (int i = 0; i < kSpectrumBins; ++i) {
    d.input_db[i] = spectrum_input_db_[i].load(std::memory_order_relaxed);
    d.output_db[i] = spectrum_output_db_[i].load(std::memory_order_relaxed);
  }
  return d;
}

void BuiltinEq::reset() {
  impl_->reset();
  initSpectrumBins();
}

float BuiltinEq::normToFreq(double norm) { return d_eq_norm_to_freq(norm); }

double BuiltinEq::freqToNorm(float freq) { return d_eq_freq_to_norm(freq); }

float BuiltinEq::normToQ(double norm) { return d_eq_norm_to_q(norm); }

double BuiltinEq::qToNorm(float q) { return d_eq_q_to_norm(q); }

void BuiltinEq::initSpectrumBins() {
  spectrum_analyzer_.init(kSpectrumBins, (float)impl_->sample_rate);
  spectrum_sample_count_ = 0;
  for (int i = 0; i < kSpectrumBins; ++i) {
    spectrum_input_db_[i].store(-100.0f, std::memory_order_relaxed);
    spectrum_output_db_[i].store(-100.0f, std::memory_order_relaxed);
  }
}

void BuiltinEq::maybeUpdateSpectrum() {
  if (spectrum_sample_count_ < kFftSize) return;
  spectrum_analyzer_.computeSpectrum(input_ring_, spectrum_input_db_);
  spectrum_analyzer_.computeSpectrum(output_ring_, spectrum_output_db_);
  spectrum_sample_count_ = 0;
}

}  // namespace hibiki
