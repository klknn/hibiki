#include "engine/instruments/builtin_drum_machine.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "engine/instruments/builtin_3xosc.hpp"
#include "engine/instruments/builtin_sampler.hpp"
#include "engine/ipc/ipc.hpp"
#include "engine/test_utils_state.hpp"

namespace hibiki {
namespace {

HostProcessContext MakeContext(double sr = 44100.0) {
  HostProcessContext ctx;
  ctx.sampleRate = sr;
  return ctx;
}

TEST(BuiltinDrumMachineTest, InitialState) {
  g_ipc_enabled = false;
  BuiltinDrumMachine dm;
  EXPECT_EQ(dm.getName(), "Drum Machine");
  EXPECT_EQ(dm.getPath(), "builtin://drum_machine");
  EXPECT_TRUE(dm.isInstrument());
  EXPECT_EQ(dm.getParameterCount(), 3);
}

TEST(BuiltinDrumMachineTest, LoadAndRemovePadPlugins) {
  g_ipc_enabled = false;
  BuiltinDrumMachine dm;
  dm.load("", 0, 44100.0);

  // Pad 0: Load Sampler
  EXPECT_TRUE(dm.loadPadPlugin(0, "builtin://sampler"));
  // Pad 1: Load 3xOsc
  EXPECT_TRUE(dm.loadPadPlugin(1, "builtin://3xosc"));

  // Check state serialization (implicitly verifies load)
  pb::core::DrumMachineState state;
  dm.serializeState(&state);
  ASSERT_EQ(state.pads_size(), 2);

  EXPECT_EQ(state.pads(0).pad_index(), 0u);
  EXPECT_EQ(state.pads(0).plugin_path(), "builtin://sampler");

  EXPECT_EQ(state.pads(1).pad_index(), 1u);
  EXPECT_EQ(state.pads(1).plugin_path(), "builtin://3xosc");

  // Remove plugin from Pad 0
  EXPECT_TRUE(dm.removePadPlugin(0));
  pb::core::DrumMachineState state2;
  dm.serializeState(&state2);
  ASSERT_EQ(state2.pads_size(), 1);
  EXPECT_EQ(state2.pads(0).pad_index(), 1u);
}

TEST(BuiltinDrumMachineTest, MixAndRouting) {
  g_ipc_enabled = false;
  BuiltinDrumMachine dm;
  dm.load("", 0, 44100.0);

  // Load 3xOsc on Pad 0 (triggers on note 36)
  ASSERT_TRUE(dm.loadPadPlugin(0, "builtin://3xosc"));

  constexpr int N = 256;
  float outL[N] = {0};
  float outR[N] = {0};
  float* outs[2] = {outL, outR};

  // Trigger Pad 0 (pitch 36)
  std::vector<MidiNoteEvent> events;
  MidiNoteEvent ev;
  ev.pitch = 36;
  ev.velocity = 1.0f;
  ev.isNoteOn = true;
  events.push_back(ev);

  auto ctx = MakeContext();
  dm.process(nullptr, outs, N, ctx, events);

  // Check that 3xOsc generated audio
  float peak = 0;
  for (int i = 0; i < N; ++i) {
    peak = std::max(peak, std::abs(outL[i]));
  }
  EXPECT_GT(peak, 0.001f) << "Synthesizer on Pad 0 should generate audio";
}

TEST(BuiltinDrumMachineTest, MuteAndSolo) {
  g_ipc_enabled = false;
  BuiltinDrumMachine dm;
  dm.load("", 0, 44100.0);

  ASSERT_TRUE(dm.loadPadPlugin(0, "builtin://3xosc"));
  ASSERT_TRUE(dm.loadPadPlugin(1, "builtin://3xosc"));

  // Solo Pad 1, meaning Pad 0 should not play
  dm.setPadSolo(1, true);

  constexpr int N = 256;
  float outL[N] = {0};
  float outR[N] = {0};
  float* outs[2] = {outL, outR};

  // Trigger Pad 0 only
  std::vector<MidiNoteEvent> events;
  MidiNoteEvent ev;
  ev.pitch = 36;
  ev.velocity = 1.0f;
  ev.isNoteOn = true;
  events.push_back(ev);

  auto ctx = MakeContext();
  dm.process(nullptr, outs, N, ctx, events);

  // Output should be silent since Pad 0 is not soloed and Pad 1 is soloed
  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(outL[i], 0.0f, 1e-7f);
    EXPECT_NEAR(outR[i], 0.0f, 1e-7f);
  }

  // Now mute Pad 0, unsolo Pad 1, and trigger Pad 0
  dm.setPadSolo(1, false);
  dm.setPadMute(0, true);

  std::fill(outL, outL + N, 0.0f);
  std::fill(outR, outR + N, 0.0f);
  dm.process(nullptr, outs, N, ctx, events);

  // Output should be silent since Pad 0 is muted
  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(outL[i], 0.0f, 1e-7f);
    EXPECT_NEAR(outR[i], 0.0f, 1e-7f);
  }
}

TEST(BuiltinDrumMachineTest, SerializationRoundTrip) {
  g_ipc_enabled = false;
  BuiltinDrumMachine dm1;
  dm1.load("", 0, 44100.0);
  ASSERT_TRUE(dm1.loadPadPlugin(3, "builtin://3xosc"));
  dm1.setPadVolume(3, 0.75f);
  dm1.setPadPan(3, -0.5f);
  dm1.setPadMute(3, true);
  dm1.setPadTriggerNote(3, 72);

  // Serialize to protobuf
  pb::core::DrumMachineState state;
  dm1.serializeState(&state);

  // Deserialize to dm2
  BuiltinDrumMachine dm2;
  dm2.load("", 0, 44100.0);
  dm2.deserializeState(state);

  // Check state of dm2
  pb::core::DrumMachineState state2;
  dm2.serializeState(&state2);

  ASSERT_EQ(state2.pads_size(), 1);
  EXPECT_EQ(state2.pads(0).pad_index(), 3u);
  EXPECT_EQ(state2.pads(0).plugin_path(), "builtin://3xosc");
  EXPECT_NEAR(state2.pads(0).volume(), 0.75f, 1e-5f);
  EXPECT_NEAR(state2.pads(0).pan(), -0.5f, 1e-5f);
  EXPECT_TRUE(state2.pads(0).mute());
  EXPECT_FALSE(state2.pads(0).solo());
  EXPECT_EQ(state2.pads(0).trigger_note(), 72u);
}

}  // namespace
}  // namespace hibiki
