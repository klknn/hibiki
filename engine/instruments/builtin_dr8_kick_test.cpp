#include "engine/instruments/builtin_dr8_kick.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace hibiki {

class BuiltinDr8KickTest : public ::testing::Test {
 protected:
  BuiltinDr8Kick plugin;

  void SetUp() override { plugin.load(BuiltinDr8Kick::kPath, 0, 44100.0); }
};

TEST_F(BuiltinDr8KickTest, MetadataAndParams) {
  EXPECT_EQ(plugin.getName(), "DR8 Kick");
  EXPECT_EQ(plugin.getPath(), "builtin://dr8_kick");
  EXPECT_TRUE(plugin.isInstrument());
  EXPECT_EQ(plugin.getParameterCount(), 7);

  VstParamInfo info;
  EXPECT_TRUE(plugin.getParameterInfo(0, info));
  EXPECT_EQ(info.name, "Pitch");
}

TEST_F(BuiltinDr8KickTest, PlayAndDecay) {
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

  // Trigger Kick note
  plugin.setParameterValue(BuiltinDr8Kick::PARAM_DECAY, 0.0);  // fast decay
  MidiNoteEvent note_on{};
  note_on.isNoteOn = true;
  note_on.pitch = 36;
  note_on.velocity = 0.9f;
  events.push_back(note_on);

  plugin.process(nullptr, outputs, 500, context, events);

  // Output should have non-zero kick sound
  bool has_sound = false;
  for (int i = 5; i < 500; ++i) {
    if (std::abs(output_l[i]) > 0.001f) {
      has_sound = true;
      break;
    }
  }
  EXPECT_TRUE(has_sound) << "Kick drum should produce sound when triggered";

  // Let it play until it decays completely to silence
  events.clear();
  // With 0.4 decay parameter, decay time is ~0.4 seconds (approx 17640
  // samples). 17640 / 500 = 36 blocks. We process 50 blocks.
  for (int block = 0; block < 50; ++block) {
    plugin.process(nullptr, outputs, 500, context, events);
  }

  // Now it must be silent
  for (int i = 0; i < 500; ++i) {
    EXPECT_NEAR(output_l[i], 0.0f, 0.0001f)
        << "Kick drum should decay to silence after playing out, sample " << i;
  }
}

}  // namespace hibiki
