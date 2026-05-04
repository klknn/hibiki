#include "engine/effects/builtin_convolver.hpp"

#include <cmath>
#include <vector>

#include "gtest/gtest.h"

namespace hibiki {
namespace {

class BuiltinConvolverTest : public ::testing::Test {
 protected:
  BuiltinConvolver conv;

  void SetUp() override { conv.load("builtin://convolver", 0, 44100.0); }
};

TEST_F(BuiltinConvolverTest, LoadDefaults) {
  EXPECT_EQ(conv.getParameterCount(), 4);
  EXPECT_DOUBLE_EQ(conv.getParameterValue(BuiltinConvolver::PARAM_DRY), 0.0);
  EXPECT_DOUBLE_EQ(conv.getParameterValue(BuiltinConvolver::PARAM_WET), 1.0);
  EXPECT_DOUBLE_EQ(conv.getParameterValue(BuiltinConvolver::PARAM_ENABLE), 1.0);
}

TEST_F(BuiltinConvolverTest, NoIR_Passthrough) {
  // Without an IR loaded, process should pass through
  constexpr int N = 512;
  std::vector<float> in_l(N), in_r(N), out_l(N), out_r(N);
  for (int i = 0; i < N; ++i) {
    in_l[i] = (float)std::sin(2.0 * M_PI * 440.0 * i / 44100.0);
    in_r[i] = in_l[i];
  }
  float* ins[] = {in_l.data(), in_r.data()};
  float* outs[] = {out_l.data(), out_r.data()};
  HostProcessContext ctx{44100.0, 120.0, 4, 4, 0, 0};
  std::vector<MidiNoteEvent> events;

  conv.process(ins, outs, N, ctx, events);

  // Output should equal input (passthrough when no IR loaded)
  for (int i = 0; i < N; ++i) {
    EXPECT_FLOAT_EQ(out_l[i], in_l[i]);
    EXPECT_FLOAT_EQ(out_r[i], in_r[i]);
  }
}

TEST_F(BuiltinConvolverTest, Bypass) {
  conv.setParameterValue(BuiltinConvolver::PARAM_ENABLE, 0.0);

  constexpr int N = 512;
  std::vector<float> in_l(N, 1.0f), in_r(N, 1.0f);
  std::vector<float> out_l(N), out_r(N);
  float* ins[] = {in_l.data(), in_r.data()};
  float* outs[] = {out_l.data(), out_r.data()};
  HostProcessContext ctx{44100.0, 120.0, 4, 4, 0, 0};
  std::vector<MidiNoteEvent> events;

  conv.process(ins, outs, N, ctx, events);

  for (int i = 0; i < N; ++i) {
    EXPECT_FLOAT_EQ(out_l[i], in_l[i]);
  }
}

TEST_F(BuiltinConvolverTest, PluginInfo) {
  EXPECT_EQ(conv.getName(), "Convolver");
  EXPECT_EQ(conv.getPath(), "builtin://convolver");
  EXPECT_FALSE(conv.isInstrument());
  EXPECT_EQ(conv.getPluginIndex(), 0);
}

TEST_F(BuiltinConvolverTest, ParameterInfo) {
  VstParamInfo info;
  EXPECT_TRUE(conv.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Dry");
  EXPECT_TRUE(conv.getParameterInfo(1, info));
  EXPECT_EQ(info.name, "Wet");
  EXPECT_TRUE(conv.getParameterInfo(2, info));
  EXPECT_EQ(info.name, "Pre-Delay");
  EXPECT_TRUE(conv.getParameterInfo(3, info));
  EXPECT_EQ(info.name, "Enable");
  EXPECT_FALSE(conv.getParameterInfo(4, info));
}

TEST_F(BuiltinConvolverTest, SetParameterValue) {
  conv.setParameterValue(BuiltinConvolver::PARAM_DRY, 0.5);
  EXPECT_DOUBLE_EQ(conv.getParameterValue(BuiltinConvolver::PARAM_DRY), 0.5);

  conv.setParameterValue(BuiltinConvolver::PARAM_WET, 0.3);
  EXPECT_DOUBLE_EQ(conv.getParameterValue(BuiltinConvolver::PARAM_WET), 0.3);
}

}  // namespace
}  // namespace hibiki
