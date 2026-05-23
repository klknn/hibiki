#include "engine/effects/builtin_stereo_width.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace hibiki {

class BuiltinStereoWidthTest : public ::testing::Test {
 protected:
  BuiltinStereoWidth plugin;

  void SetUp() override { plugin.load(BuiltinStereoWidth::kPath, 0, 44100.0); }
};

TEST_F(BuiltinStereoWidthTest, MetadataAndParams) {
  EXPECT_EQ(plugin.getName(), "Stereo Width");
  EXPECT_EQ(plugin.getPath(), "builtin://stereo_width");
  EXPECT_FALSE(plugin.isInstrument());
  EXPECT_EQ(plugin.getParameterCount(), 5);

  VstParamInfo info;
  EXPECT_TRUE(plugin.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Delay");
}

TEST_F(BuiltinStereoWidthTest, BypassWhenDisabled) {
  plugin.setParameterValue(BuiltinStereoWidth::PARAM_ENABLE, 0.0);

  std::vector<float> input_l = {0.5f, -0.2f, 0.1f};
  std::vector<float> input_r = {-0.4f, 0.3f, -0.6f};
  std::vector<float> output_l(3, 0.0f);
  std::vector<float> output_r(3, 0.0f);

  float* inputs[2] = {input_l.data(), input_r.data()};
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  std::vector<MidiNoteEvent> events;
  plugin.process(inputs, outputs, 3, context, events);

  for (size_t i = 0; i < input_l.size(); ++i) {
    EXPECT_FLOAT_EQ(output_l[i], input_l[i]);
    EXPECT_FLOAT_EQ(output_r[i], input_r[i]);
  }
}

TEST_F(BuiltinStereoWidthTest, StereoWideningHaas) {
  plugin.setParameterValue(BuiltinStereoWidth::PARAM_DELAY,
                           0.25);  // 10ms delay
  plugin.setParameterValue(BuiltinStereoWidth::PARAM_CHANNEL,
                           0.0);  // delay Left
  plugin.setParameterValue(BuiltinStereoWidth::PARAM_MONO_FREQ,
                           0.0);  // low crossover (50Hz)
  plugin.setParameterValue(BuiltinStereoWidth::PARAM_WIDTH, 0.5);  // 1.0x width

  std::vector<float> input_l(1000);
  std::vector<float> input_r(1000);
  for (int i = 0; i < 1000; ++i) {
    float val = std::sin(2.0f * 3.14159265f * 950.0f * i /
                         44100.0f);  // 950Hz sine wave
    input_l[i] = val;
    input_r[i] = val;
  }
  std::vector<float> output_l(1000, 0.0f);
  std::vector<float> output_r(1000, 0.0f);

  float* inputs[2] = {input_l.data(), input_r.data()};
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  std::vector<MidiNoteEvent> events;
  plugin.process(inputs, outputs, 1000, context, events);

  // Since we delay Left channel by 10ms (approx 441 samples), the output of
  // Left and Right should differ after the delay line starts filling up.
  bool different = false;
  for (int i = 500; i < 1000; ++i) {
    if (std::abs(output_l[i] - output_r[i]) > 0.05f) {
      different = true;
      break;
    }
  }
  EXPECT_TRUE(different)
      << "Haas delay should make Left and Right channels different";
}

TEST_F(BuiltinStereoWidthTest, MonoLowEnd) {
  plugin.setParameterValue(BuiltinStereoWidth::PARAM_DELAY, 0.25);  // 10ms
  plugin.setParameterValue(BuiltinStereoWidth::PARAM_CHANNEL,
                           0.0);  // delay Left
  plugin.setParameterValue(BuiltinStereoWidth::PARAM_MONO_FREQ,
                           1.0);  // maximum crossover (500Hz)
  plugin.setParameterValue(BuiltinStereoWidth::PARAM_WIDTH, 0.5);  // 1.0x width

  // Generate a low-frequency mono-inversive signal (60Hz, L and R out of phase)
  std::vector<float> input_l(2000);
  std::vector<float> input_r(2000);
  for (int i = 0; i < 2000; ++i) {
    float val = std::sin(2.0f * 3.14159265f * 20.0f * i / 44100.0f);
    input_l[i] = val;
    input_r[i] = -val;  // Completely out of phase
  }
  std::vector<float> output_l(2000, 0.0f);
  std::vector<float> output_r(2000, 0.0f);

  float* inputs[2] = {input_l.data(), input_r.data()};
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  std::vector<MidiNoteEvent> events;
  plugin.process(inputs, outputs, 2000, context, events);

  // Since low frequencies (60Hz is below 500Hz) are summed to mono, they should
  // cancel out completely (or nearly completely, due to the filter phase
  // delay/transients). Let's assert that after the transient response settles
  // (around 1000 samples), the output values are very close to 0.0.
  for (int i = 1500; i < 2000; ++i) {
    EXPECT_NEAR(output_l[i], 0.0f, 0.05f)
        << "Low frequency out-of-phase should cancel out in mono at sample "
        << i;
    EXPECT_NEAR(output_r[i], 0.0f, 0.05f)
        << "Low frequency out-of-phase should cancel out in mono at sample "
        << i;
  }
}

}  // namespace hibiki
