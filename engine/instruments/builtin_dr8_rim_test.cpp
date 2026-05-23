#include "engine/instruments/builtin_dr8_rim.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace hibiki {

class BuiltinDr8RimTest : public ::testing::Test {
 protected:
  BuiltinDr8Rim plugin;

  void SetUp() override { plugin.load(BuiltinDr8Rim::kPath, 0, 44100.0); }
};

TEST_F(BuiltinDr8RimTest, MetadataAndParams) {
  EXPECT_EQ(plugin.getName(), "DR8 Rimshot");
  EXPECT_EQ(plugin.getPath(), "builtin://dr8_rim");
  EXPECT_TRUE(plugin.isInstrument());
  EXPECT_EQ(plugin.getParameterCount(), 3);

  VstParamInfo info;
  EXPECT_TRUE(plugin.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Pitch");
  EXPECT_TRUE(plugin.getParameterInfo(1, info));
  EXPECT_EQ(info.name, "Decay");
}

TEST_F(BuiltinDr8RimTest, PlayAndDecay) {
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

  // Trigger Rimshot note
  plugin.setParameterValue(BuiltinDr8Rim::PARAM_DECAY,
                           0.0);  // fast decay (10ms)
  MidiNoteEvent note_on{};
  note_on.isNoteOn = true;
  note_on.pitch = 37;  // Rimshot C#1
  note_on.velocity = 0.9f;
  events.push_back(note_on);

  plugin.process(nullptr, outputs, 500, context, events);

  // Output should have non-zero rimshot sound
  bool has_sound = false;
  for (int i = 5; i < 500; ++i) {
    if (std::abs(output_l[i]) > 0.001f) {
      has_sound = true;
      break;
    }
  }
  EXPECT_TRUE(has_sound) << "Rimshot should produce sound when triggered";

  // Let it play until it decays completely to silence
  events.clear();
  for (int block = 0; block < 100; ++block) {
    plugin.process(nullptr, outputs, 500, context, events);
  }

  // Now it must be silent
  for (int i = 0; i < 500; ++i) {
    EXPECT_NEAR(output_l[i], 0.0f, 0.0001f)
        << "Rimshot should decay to silence after playing out, sample " << i;
  }
}

}  // namespace hibiki
