#include "engine/instruments/builtin_dr8_conga.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace hibiki {

class BuiltinDr8CongaTest : public ::testing::Test {
 protected:
  BuiltinDr8Conga plugin;

  void SetUp() override { plugin.load(BuiltinDr8Conga::kPath, 0, 44100.0); }
};

TEST_F(BuiltinDr8CongaTest, MetadataAndParams) {
  EXPECT_EQ(plugin.getName(), "DR8 Conga");
  EXPECT_EQ(plugin.getPath(), "builtin://dr8_conga");
  EXPECT_TRUE(plugin.isInstrument());
  EXPECT_EQ(plugin.getParameterCount(), 5);

  VstParamInfo info;
  EXPECT_TRUE(plugin.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Pitch");
  EXPECT_TRUE(plugin.getParameterInfo(1, info));
  EXPECT_EQ(info.name, "Decay");
  EXPECT_TRUE(plugin.getParameterInfo(2, info));
  EXPECT_EQ(info.name, "Pitch Env Decay");
}

TEST_F(BuiltinDr8CongaTest, PlayAndDecay) {
  std::vector<float> output_l(500, 0.0f);
  std::vector<float> output_r(500, 0.0f);
  float* outputs[2] = {output_l.data(), output_r.data()};

  HostProcessContext context;
  context.sampleRate = 44100.0;
  std::vector<MidiNoteEvent> events;

  // Verify silent when idle
  plugin.process(nullptr, outputs, 500, context, events);
  for (int i = 0; i < 500; ++i) {
    EXPECT_FLOAT_EQ(output_l[i], 0.0f);
    EXPECT_FLOAT_EQ(output_r[i], 0.0f);
  }

  // Trigger Conga note
  plugin.setParameterValue(BuiltinDr8Conga::PARAM_DECAY,
                           0.0);  // fast decay (0.05s)
  MidiNoteEvent note_on{};
  note_on.isNoteOn = true;
  note_on.pitch = 64;  // Conga high E4
  note_on.velocity = 0.9f;
  events.push_back(note_on);

  plugin.process(nullptr, outputs, 500, context, events);

  // Output should have non-zero conga sound
  bool has_sound = false;
  for (int i = 5; i < 500; ++i) {
    if (std::abs(output_l[i]) > 0.001f) {
      has_sound = true;
      break;
    }
  }
  EXPECT_TRUE(has_sound) << "Conga should produce sound when triggered";

  // Let it play until it decays completely to silence
  events.clear();
  for (int block = 0; block < 100; ++block) {
    plugin.process(nullptr, outputs, 500, context, events);
  }

  // Now it must be silent
  for (int i = 0; i < 500; ++i) {
    EXPECT_NEAR(output_l[i], 0.0f, 0.0001f)
        << "Conga should decay to silence after playing out, sample " << i;
  }
}

}  // namespace hibiki
