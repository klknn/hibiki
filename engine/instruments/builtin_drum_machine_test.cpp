#include "engine/instruments/builtin_drum_machine.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "engine/instruments/builtin_3xosc.hpp"
#include "engine/instruments/builtin_sampler.hpp"
#include "engine/ipc/ipc.hpp"
#include "engine/test_utils.hpp"
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
  EXPECT_TRUE(dm.loadPadPlugin(0, "builtin://sampler").ok());
  // Pad 1: Load 3xOsc
  EXPECT_TRUE(dm.loadPadPlugin(1, "builtin://3xosc").ok());

  // Check state serialization (implicitly verifies load)
  pb::core::DrumMachineState state;
  dm.serializeState(&state);
  ASSERT_EQ(state.pads_size(), 2);

  EXPECT_EQ(state.pads(0).pad_index(), 0u);
  EXPECT_EQ(state.pads(0).plugin_path(), "builtin://sampler");

  EXPECT_EQ(state.pads(1).pad_index(), 1u);
  EXPECT_EQ(state.pads(1).plugin_path(), "builtin://3xosc");

  // Remove plugin from Pad 0
  EXPECT_TRUE(dm.removePadPlugin(0).ok());
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
  ASSERT_TRUE(dm.loadPadPlugin(0, "builtin://3xosc").ok());

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

TEST(BuiltinDrumMachineTest, PadSamplerProducesSound) {
  g_ipc_enabled = false;
  BuiltinDrumMachine dm;
  dm.load("", 0, 44100.0);

  ASSERT_TRUE(dm.loadPadPlugin(0, "builtin://sampler").ok());
  std::string wav_path = hibiki::find_test_file("testdata/loop140.wav");
  ASSERT_TRUE(dm.loadPadSample(0, wav_path).ok());

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

  float peak = 0;
  for (int i = 0; i < N; ++i) {
    peak = std::max(peak, std::abs(outL[i]));
  }
  EXPECT_GT(peak, 0.001f) << "Sampler on Pad 0 should generate audio";
}

TEST(BuiltinDrumMachineTest, PadSamplerMultipleTriggers) {
  g_ipc_enabled = false;
  BuiltinDrumMachine dm;
  dm.load("", 0, 44100.0);

  ASSERT_TRUE(dm.loadPadPlugin(0, "builtin://sampler").ok());
  std::string wav_path = hibiki::find_test_file("testdata/loop140.wav");
  ASSERT_TRUE(dm.loadPadSample(0, wav_path).ok());

  constexpr int N = 256;
  float outL[N] = {0};
  float outR[N] = {0};
  float* outs[2] = {outL, outR};
  auto ctx = MakeContext();

  // Trigger it 16 times with note-on/off
  for (int t = 0; t < 16; ++t) {
    // Note on
    std::vector<MidiNoteEvent> events1;
    MidiNoteEvent ev1;
    ev1.pitch = 36;
    ev1.velocity = 1.0f;
    ev1.isNoteOn = true;
    ev1.sampleOffset = 0;
    ev1.channel = 0;
    events1.push_back(ev1);

    std::fill(outL, outL + N, 0.0f);
    std::fill(outR, outR + N, 0.0f);
    dm.process(nullptr, outs, N, ctx, events1);

    float peak1 = 0;
    for (int i = 0; i < N; ++i) {
      peak1 = std::max(peak1, std::abs(outL[i]));
    }
    EXPECT_GT(peak1, 0.001f)
        << "Trigger " << t << " note-on should generate audio";

    // Play a few blocks to let the sample progress or finish
    for (int b = 0; b < 20; ++b) {
      std::fill(outL, outL + N, 0.0f);
      std::fill(outR, outR + N, 0.0f);
      dm.process(nullptr, outs, N, ctx, {});
    }

    // Note off
    std::vector<MidiNoteEvent> events2;
    MidiNoteEvent ev2;
    ev2.pitch = 36;
    ev2.velocity = 0.0f;
    ev2.isNoteOn = false;
    ev2.sampleOffset = 0;
    ev2.channel = 0;
    events2.push_back(ev2);

    std::fill(outL, outL + N, 0.0f);
    std::fill(outR, outR + N, 0.0f);
    dm.process(nullptr, outs, N, ctx, events2);
  }
}

TEST(BuiltinDrumMachineTest, PadSamplerNoteOnNoteOffSameBlock) {
  g_ipc_enabled = false;
  BuiltinDrumMachine dm;
  dm.load("", 0, 44100.0);

  ASSERT_TRUE(dm.loadPadPlugin(0, "builtin://sampler").ok());
  std::string wav_path = hibiki::find_test_file("testdata/loop140.wav");
  ASSERT_TRUE(dm.loadPadSample(0, wav_path).ok());

  constexpr int N = 256;
  float outL[N] = {0};
  float outR[N] = {0};
  float* outs[2] = {outL, outR};
  auto ctx = MakeContext();

  // Create note-on and note-off in the same block
  std::vector<MidiNoteEvent> events;
  MidiNoteEvent ev1;
  ev1.pitch = 36;
  ev1.velocity = 1.0f;
  ev1.isNoteOn = true;
  ev1.sampleOffset = 0;
  ev1.channel = 0;
  events.push_back(ev1);

  MidiNoteEvent ev2;
  ev2.pitch = 36;
  ev2.velocity = 0.0f;
  ev2.isNoteOn = false;
  ev2.sampleOffset = 128;  // later in the same block
  ev2.channel = 0;
  events.push_back(ev2);

  dm.process(nullptr, outs, N, ctx, events);

  float peak = 0;
  for (int i = 0; i < N; ++i) {
    peak = std::max(peak, std::abs(outL[i]));
  }
  EXPECT_GT(peak, 0.001f)
      << "Note-on and note-off in the same block should still generate audio";
}

TEST(BuiltinDrumMachineTest, MuteAndSolo) {
  g_ipc_enabled = false;
  BuiltinDrumMachine dm;
  dm.load("", 0, 44100.0);

  ASSERT_TRUE(dm.loadPadPlugin(0, "builtin://3xosc").ok());
  ASSERT_TRUE(dm.loadPadPlugin(1, "builtin://3xosc").ok());

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
  ASSERT_TRUE(dm1.loadPadPlugin(3, "builtin://3xosc").ok());
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

TEST(BuiltinDrumMachineTest, LoadOtherInstruments) {
  g_ipc_enabled = false;
  BuiltinDrumMachine dm;
  dm.load("", 0, 44100.0);

  // Try loading DR8 Kick
  EXPECT_TRUE(dm.loadPadPlugin(0, "builtin://dr8_kick").ok());

  constexpr int N = 256;
  float outL[N] = {0};
  float outR[N] = {0};
  float* outs[2] = {outL, outR};

  std::vector<MidiNoteEvent> events;
  MidiNoteEvent ev;
  ev.pitch = 36;
  ev.velocity = 1.0f;
  ev.isNoteOn = true;
  events.push_back(ev);

  auto ctx = MakeContext();
  dm.process(nullptr, outs, N, ctx, events);

  float peak = 0;
  for (int i = 0; i < N; ++i) {
    peak = std::max(peak, std::abs(outL[i]));
  }
  EXPECT_GT(peak, 0.001f) << "DR8 Kick on Pad 0 should generate audio";
}

TEST(BuiltinDrumMachineTest, LoadVst3Instrument) {
  g_ipc_enabled = false;
  BuiltinDrumMachine dm;
  dm.load("", 0, 44100.0);

  std::string dexed_path = hibiki::find_test_file("testdata/Dexed.vst3");

  // Load Dexed VST3 plugin on Pad 0
  EXPECT_TRUE(dm.loadPadPlugin(0, dexed_path).ok());

  // Verify that it is loaded in the pads_ state
  pb::core::DrumMachineState state;
  dm.serializeState(&state);
  ASSERT_EQ(state.pads_size(), 1);
  EXPECT_EQ(state.pads(0).pad_index(), 0u);
  EXPECT_EQ(state.pads(0).plugin_path(), dexed_path);

  // Trigger Pad 0 with MIDI note 36
  constexpr int N = 256;
  float outL[N] = {0};
  float outR[N] = {0};
  float* outs[2] = {outL, outR};

  std::vector<MidiNoteEvent> events;
  MidiNoteEvent ev;
  ev.pitch = 36;
  ev.velocity = 1.0f;
  ev.isNoteOn = true;
  events.push_back(ev);

  auto ctx = MakeContext();
  dm.process(nullptr, outs, N, ctx, events);

  // Assert that Dexed produced audio output
  float peak = 0;
  for (int i = 0; i < N; ++i) {
    peak = std::max(peak, std::abs(outL[i]));
  }
  EXPECT_GT(peak, 0.001f) << "Dexed VST3 on Pad 0 should generate audio";
}

}  // namespace
}  // namespace hibiki
