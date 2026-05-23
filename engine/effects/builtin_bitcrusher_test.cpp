#include "engine/effects/builtin_bitcrusher.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace hibiki {

class BuiltinBitcrusherTest : public ::testing::Test {
 protected:
  BuiltinBitcrusher bitcrusher;

  void SetUp() override {
    bitcrusher.load(BuiltinBitcrusher::kPath, 0, 44100.0);
  }
};

TEST_F(BuiltinBitcrusherTest, MetadataAndParams) {
  EXPECT_EQ(bitcrusher.getName(), "Bitcrusher");
  EXPECT_EQ(bitcrusher.getPath(), "builtin://bitcrusher");
  EXPECT_FALSE(bitcrusher.isInstrument());
  EXPECT_EQ(bitcrusher.getParameterCount(), 5);

  VstParamInfo info;
  EXPECT_TRUE(bitcrusher.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Bit Depth");
  EXPECT_NEAR(info.defaultValue, 0.652174, 0.001);
}

TEST_F(BuiltinBitcrusherTest, BypassWhenDisabled) {
  bitcrusher.setParameterValue(BuiltinBitcrusher::PARAM_ENABLE, 0.0);

  std::vector<float> input_l = {0.1f, -0.5f, 0.8f, -0.2f};
  std::vector<float> input_r = {-0.3f, 0.6f, -0.9f, 0.4f};
  std::vector<float> output_l(4, 0.0f);
  std::vector<float> output_r(4, 0.0f);

  float* inputs[2] = {input_l.data(), input_r.data()};
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  std::vector<MidiNoteEvent> events;
  bitcrusher.process(inputs, outputs, 4, context, events);

  for (size_t i = 0; i < input_l.size(); ++i) {
    EXPECT_FLOAT_EQ(output_l[i], input_l[i]);
    EXPECT_FLOAT_EQ(output_r[i], input_r[i]);
  }
}

TEST_F(BuiltinBitcrusherTest, DryMixPassesThrough) {
  bitcrusher.setParameterValue(BuiltinBitcrusher::PARAM_WET_DRY, 0.0);

  std::vector<float> input_l = {0.15f, -0.45f, 0.75f};
  std::vector<float> input_r = {-0.25f, 0.55f, -0.85f};
  std::vector<float> output_l(3, 0.0f);
  std::vector<float> output_r(3, 0.0f);

  float* inputs[2] = {input_l.data(), input_r.data()};
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  std::vector<MidiNoteEvent> events;
  bitcrusher.process(inputs, outputs, 3, context, events);

  for (size_t i = 0; i < input_l.size(); ++i) {
    EXPECT_FLOAT_EQ(output_l[i], input_l[i]);
    EXPECT_FLOAT_EQ(output_r[i], input_r[i]);
  }
}

TEST_F(BuiltinBitcrusherTest, BitReductionQuantization) {
  // Set to 2 bits (steps = 2^(2-1) = 2. Bounded to -1.0, -0.5, 0.0, 0.5, 1.0)
  // Norm for 2 bits: (2 - 1) / 23 = 1.0 / 23.0
  bitcrusher.setParameterValue(BuiltinBitcrusher::PARAM_BIT_DEPTH, 1.0 / 23.0);
  bitcrusher.setParameterValue(BuiltinBitcrusher::PARAM_SAMPLE_RATE,
                               1.0);  // full rate
  bitcrusher.setParameterValue(BuiltinBitcrusher::PARAM_DRIVE,
                               0.0);  // no drive
  bitcrusher.setParameterValue(BuiltinBitcrusher::PARAM_WET_DRY,
                               1.0);  // 100% wet

  std::vector<float> input_l = {0.1f, 0.35f, 0.7f, -0.1f, -0.35f, -0.8f};
  std::vector<float> output_l(input_l.size(), 0.0f);
  std::vector<float> output_r(input_l.size(), 0.0f);

  float* inputs[2] = {input_l.data(), input_l.data()};  // mono-in
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  std::vector<MidiNoteEvent> events;
  bitcrusher.process(inputs, outputs, (int)input_l.size(), context, events);

  // Expected quantized values with step size = 0.5:
  // 0.1  -> round(0.1*2)/2  = 0.0
  // 0.35 -> round(0.35*2)/2 = 0.5
  // 0.7  -> round(0.7*2)/2  = 0.5 (or 1.0 depending on rounding,
  // std::round(1.4) = 1.0) -0.1 -> 0.0 -0.35 -> -0.5 -0.8 -> -1.0
  EXPECT_NEAR(output_l[0], 0.0f, 1e-5);
  EXPECT_NEAR(output_l[1], 0.5f, 1e-5);
  EXPECT_NEAR(output_l[2], 0.5f, 1e-5);
  EXPECT_NEAR(output_l[3], 0.0f, 1e-5);
  EXPECT_NEAR(output_l[4], -0.5f, 1e-5);
  EXPECT_NEAR(output_l[5], -1.0f, 1e-5);
}

TEST_F(BuiltinBitcrusherTest, SampleRateReduction) {
  // Let's set sample rate downsampling factor to trigger holds.
  // 20 * (44100 / 20)^norm.
  // Let's target exactly 11025 Hz (decimate by 4).
  // 11025 = 20 * (2205)^norm => 551.25 = 2205^norm => norm =
  // log(551.25)/log(2205) ~= 0.82 We can just set standard norm to something
  // small to get heavy downsampling, e.g. target 4410 Hz (decimate by 10) =>
  // norm ~= 0.69 Or we can just set norm directly and check target frequency
  // via mapping function.
  double norm_sr = 0.5;
  double target_hz = BuiltinBitcrusher::normToSampleRate(norm_sr, 44100.0);
  bitcrusher.setParameterValue(BuiltinBitcrusher::PARAM_SAMPLE_RATE, norm_sr);
  bitcrusher.setParameterValue(BuiltinBitcrusher::PARAM_BIT_DEPTH,
                               1.0);  // 24 bits (no crushing)
  bitcrusher.setParameterValue(BuiltinBitcrusher::PARAM_DRIVE, 0.0);

  int decimation = (int)std::round(44100.0 / target_hz);
  ASSERT_GT(decimation, 1);

  std::vector<float> input_l(20);
  for (int i = 0; i < 20; ++i) {
    input_l[i] = (float)i / 20.0f;  // ramp
  }
  std::vector<float> output_l(20, 0.0f);
  std::vector<float> output_r(20, 0.0f);

  float* inputs[2] = {input_l.data(), input_l.data()};
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  std::vector<MidiNoteEvent> events;
  bitcrusher.process(inputs, outputs, 20, context, events);

  // With downsampling, some adjacent samples should be held completely
  // identical
  int hold_count = 0;
  for (int i = 1; i < 20; ++i) {
    if (output_l[i] == output_l[i - 1]) {
      hold_count++;
    }
  }
  EXPECT_GT(hold_count, 0) << "Should have held samples due to downsampling";
}

}  // namespace hibiki
