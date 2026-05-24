#include "engine/effects/builtin_vocodey.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace hibiki {

class BuiltinVocodeyTest : public ::testing::Test {
 protected:
  BuiltinVocodey vocodey;

  void SetUp() override { vocodey.load(BuiltinVocodey::kPath, 0, 44100.0); }
};

TEST_F(BuiltinVocodeyTest, MetadataAndParams) {
  EXPECT_EQ(vocodey.getName(), "Vocodey");
  EXPECT_EQ(vocodey.getPath(), "builtin://vocodey");
  EXPECT_FALSE(vocodey.isInstrument());
  EXPECT_EQ(vocodey.getParameterCount(), 8);

  VstParamInfo info;
  EXPECT_TRUE(vocodey.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Attack");
  EXPECT_TRUE(vocodey.getParameterInfo(1, info));
  EXPECT_EQ(info.name, "Decay");
  EXPECT_TRUE(vocodey.getParameterInfo(2, info));
  EXPECT_EQ(info.name, "Bandwidth");
}

TEST_F(BuiltinVocodeyTest, DryMixPassesThrough) {
  vocodey.setParameterValue(BuiltinVocodey::PARAM_DRY, 1.0);
  vocodey.setParameterValue(BuiltinVocodey::PARAM_WET, 0.0);
  vocodey.setParameterValue(BuiltinVocodey::PARAM_VOLUME, 1.0);

  std::vector<float> input_l = {0.1f, -0.2f, 0.3f, -0.4f};
  std::vector<float> input_r = {-0.1f, 0.2f, -0.3f, 0.4f};
  std::vector<float> output_l(4, 0.0f);
  std::vector<float> output_r(4, 0.0f);

  float* inputs[2] = {input_l.data(), input_r.data()};
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  context.sampleRate = 44100.0;
  std::vector<MidiNoteEvent> events;

  vocodey.process(inputs, outputs, 4, context, events);

  for (size_t i = 0; i < input_l.size(); ++i) {
    EXPECT_NEAR(output_l[i], input_l[i], 0.001f);
    EXPECT_NEAR(output_r[i], input_r[i], 0.001f);
  }
}

TEST_F(BuiltinVocodeyTest, WetVocodedWithSidechain) {
  vocodey.setParameterValue(BuiltinVocodey::PARAM_DRY, 0.0);
  vocodey.setParameterValue(BuiltinVocodey::PARAM_WET, 1.0);
  vocodey.setParameterValue(BuiltinVocodey::PARAM_VOLUME, 1.0);

  // Modulator: simple 1 kHz sine
  std::vector<float> mod_l(500, 0.0f);
  std::vector<float> mod_r(500, 0.0f);
  for (int i = 0; i < 500; ++i) {
    float val = std::sin(2.0f * (float)M_PI * 1000.0f * i / 44100.0f);
    mod_l[i] = val;
    mod_r[i] = val;
  }

  // Carrier: rich 200 Hz sawtooth in sidechain
  std::vector<float> car_l(500, 0.0f);
  std::vector<float> car_r(500, 0.0f);
  for (int i = 0; i < 500; ++i) {
    float phase = std::fmod(200.0f * i / 44100.0f, 1.0f);
    float val = 2.0f * phase - 1.0f;
    car_l[i] = val;
    car_r[i] = val;
  }

  std::vector<float> output_l(500, 0.0f);
  std::vector<float> output_r(500, 0.0f);

  float* inputs[2] = {mod_l.data(), mod_r.data()};
  float* outputs[2] = {output_l.data(), output_r.data()};
  float* sidechain[2] = {car_l.data(), car_r.data()};

  HostProcessContext context;
  context.sampleRate = 44100.0;
  std::vector<MidiNoteEvent> events;

  // Process through vocoder
  vocodey.process(inputs, outputs, 500, context, events, sidechain);

  // There should be output energy at 1000 Hz filter bank bands
  bool has_output = false;
  for (int i = 50; i < 500; ++i) {
    if (std::abs(output_l[i]) > 0.0001f) {
      has_output = true;
      break;
    }
  }
  EXPECT_TRUE(has_output)
      << "Vocoder should produce audio using sidechain carrier";
}

TEST_F(BuiltinVocodeyTest, WetVocodedWithInternalSynth) {
  vocodey.setParameterValue(BuiltinVocodey::PARAM_DRY, 0.0);
  vocodey.setParameterValue(BuiltinVocodey::PARAM_WET, 1.0);
  vocodey.setParameterValue(BuiltinVocodey::PARAM_VOLUME, 1.0);

  // Modulator: sine wave at 400 Hz
  std::vector<float> mod_l(500, 0.0f);
  std::vector<float> mod_r(500, 0.0f);
  for (int i = 0; i < 500; ++i) {
    float val = std::sin(2.0f * (float)M_PI * 400.0f * i / 44100.0f);
    mod_l[i] = val;
    mod_r[i] = val;
  }

  std::vector<float> output_l(500, 0.0f);
  std::vector<float> output_r(500, 0.0f);

  float* inputs[2] = {mod_l.data(), mod_r.data()};
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  context.sampleRate = 44100.0;
  std::vector<MidiNoteEvent> events;

  // 1. Process without MIDI notes - should produce virtually zero output
  vocodey.process(inputs, outputs, 500, context, events);
  for (int i = 0; i < 500; ++i) {
    EXPECT_NEAR(output_l[i], 0.0f, 1e-4f);
  }

  // 2. Trigger MIDI Note C3 (pitch 60)
  MidiNoteEvent note_on{};
  note_on.isNoteOn = true;
  note_on.pitch = 60;
  note_on.velocity = 0.8f;
  events.push_back(note_on);

  vocodey.process(inputs, outputs, 500, context, events);

  // Output should now contain the vocoded internal synth carrier
  bool has_output = false;
  for (int i = 50; i < 500; ++i) {
    if (std::abs(output_l[i]) > 0.0001f) {
      has_output = true;
      break;
    }
  }
  EXPECT_TRUE(has_output) << "Vocoder should produce audio when internal synth "
                             "is triggered via MIDI";
}

}  // namespace hibiki
