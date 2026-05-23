#include "engine/effects/builtin_chorus.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace hibiki {

class BuiltinChorusTest : public ::testing::Test {
 protected:
  BuiltinChorus chorus;

  void SetUp() override { chorus.load(BuiltinChorus::kPath, 0, 44100.0); }
};

TEST_F(BuiltinChorusTest, MetadataAndParams) {
  EXPECT_EQ(chorus.getName(), "Chorus");
  EXPECT_EQ(chorus.getPath(), "builtin://chorus");
  EXPECT_FALSE(chorus.isInstrument());
  EXPECT_EQ(chorus.getParameterCount(), 6);

  VstParamInfo info;
  EXPECT_TRUE(chorus.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Rate");
}

TEST_F(BuiltinChorusTest, BypassWhenDisabled) {
  chorus.setParameterValue(BuiltinChorus::PARAM_ENABLE, 0.0);

  std::vector<float> input_l = {0.5f, -0.2f, 0.1f};
  std::vector<float> input_r = {-0.4f, 0.3f, -0.6f};
  std::vector<float> output_l(3, 0.0f);
  std::vector<float> output_r(3, 0.0f);

  float* inputs[2] = {input_l.data(), input_r.data()};
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  std::vector<MidiNoteEvent> events;
  chorus.process(inputs, outputs, 3, context, events);

  for (size_t i = 0; i < input_l.size(); ++i) {
    EXPECT_FLOAT_EQ(output_l[i], input_l[i]);
    EXPECT_FLOAT_EQ(output_r[i], input_r[i]);
  }
}

TEST_F(BuiltinChorusTest, DryMixPassesThrough) {
  chorus.setParameterValue(BuiltinChorus::PARAM_WET_DRY, 0.0);

  std::vector<float> input_l = {0.9f, -0.8f, 0.7f};
  std::vector<float> input_r = {-0.6f, 0.5f, -0.4f};
  std::vector<float> output_l(3, 0.0f);
  std::vector<float> output_r(3, 0.0f);

  float* inputs[2] = {input_l.data(), input_r.data()};
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  std::vector<MidiNoteEvent> events;
  chorus.process(inputs, outputs, 3, context, events);

  for (size_t i = 0; i < input_l.size(); ++i) {
    EXPECT_FLOAT_EQ(output_l[i], input_l[i]);
    EXPECT_FLOAT_EQ(output_r[i], input_r[i]);
  }
}

TEST_F(BuiltinChorusTest, ChorusModulatesSignal) {
  chorus.setParameterValue(BuiltinChorus::PARAM_WET_DRY, 1.0);  // 100% wet
  chorus.setParameterValue(BuiltinChorus::PARAM_DEPTH, 0.8);    // high depth
  chorus.setParameterValue(BuiltinChorus::PARAM_RATE, 0.5);     // high rate

  std::vector<float> input_l(2000);
  std::vector<float> input_r(2000);
  for (int i = 0; i < 2000; ++i) {
    float val = std::sin(2.0f * 3.14159265f * 440.0f * i / 44100.0f);
    input_l[i] = val;
    input_r[i] = val;
  }
  std::vector<float> output_l(2000, 0.0f);
  std::vector<float> output_r(2000, 0.0f);

  float* inputs[2] = {input_l.data(), input_r.data()};
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  std::vector<MidiNoteEvent> events;
  chorus.process(inputs, outputs, 2000, context, events);

  // Since L and R LFOs are 90 degrees out of phase, their delay times will
  // differ, causing output L and R to drift apart even with identical mono
  // input.
  bool different = false;
  for (int i = 1000; i < 2000; ++i) {
    if (std::abs(output_l[i] - output_r[i]) > 0.01f) {
      different = true;
      break;
    }
  }
  EXPECT_TRUE(different)
      << "Left and right channels should modulate differently for stereo width";
}

}  // namespace hibiki
