#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "pb/commands.pb.h"
#include "pb/core.pb.h"
#include "pb/notifications.pb.h"
#include "pb/plugin_worker.pb.h"

extern int hibiki_d_rt_init();
extern int hibiki_d_rt_term();
extern int d_roundtrip_project(const uint8_t* inData, int inLen,
                               uint8_t* outBuf, int outCapacity);
extern int d_roundtrip_request(const uint8_t* inData, int inLen,
                               uint8_t* outBuf, int outCapacity);
extern int d_roundtrip_notification(const uint8_t* inData, int inLen,
                                    uint8_t* outBuf, int outCapacity);
extern int d_encode_realtime_process_audio(int numSamples, double sampleRate,
                                           double tempo, const float* samples,
                                           int sampleCount, uint8_t* outBuf,
                                           int outCapacity);

class PbDInteropTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { ASSERT_NE(hibiki_d_rt_init(), 0); }
  static void TearDownTestSuite() { hibiki_d_rt_term(); }
};

TEST_F(PbDInteropTest, ProjectWireCompatibilityBetweenCppAndD) {
  hibiki::pb::core::Project orig;
  orig.set_bpm(128.0f);
  orig.set_playhead_sec(16.5f);
  orig.set_transport_state(hibiki::pb::core::TRANSPORT_STATE_PLAYING);

  auto* trk = orig.add_tracks();
  trk->set_track_index(0);
  trk->set_name("Synth Bass");
  trk->set_output_track_index(-1);
  trk->set_group_parent_index(-1);

  auto* plug = trk->add_plugins();
  plug->set_plugin_index(0);
  plug->set_path("builtin://acid_bass");
  plug->set_name("Acid Bass");
  plug->set_is_instrument(true);
  plug->set_sidechain_track_index(0);  // explicit 0 optional presence
  plug->set_state("\x01\x02\x03\x04", 4);

  auto* tclip = trk->add_timeline_clips();
  tclip->set_clip_index(0);
  tclip->set_start_time_sec(4.0f);
  tclip->set_alias_source(-1);
  auto* clip = tclip->mutable_clip();
  clip->set_type(hibiki::pb::core::CLIP_TYPE_MIDI);
  clip->set_name("Bassline");
  clip->set_resolution(480);
  clip->add_waveform_summary(-0.75f);
  clip->add_waveform_summary(0.25f);
  clip->add_waveform_summary(0.95f);

  auto* ev = clip->add_midi_events();
  ev->set_tick(960);
  ev->set_pitch(48);
  ev->set_duration_ticks(240);
  ev->set_velocity(110);
  ev->set_cc_number(128);
  ev->set_cc_value(-8192);  // negative int32 10-byte varint test

  std::string cppBytes;
  ASSERT_TRUE(orig.SerializeToString(&cppBytes));

  std::vector<uint8_t> outBuf(4096, 0);
  int dLen =
      d_roundtrip_project(reinterpret_cast<const uint8_t*>(cppBytes.data()),
                          static_cast<int>(cppBytes.size()), outBuf.data(),
                          static_cast<int>(outBuf.size()));
  ASSERT_GT(dLen, 0);

  hibiki::pb::core::Project restored;
  ASSERT_TRUE(restored.ParseFromArray(outBuf.data(), dLen));
  EXPECT_FLOAT_EQ(restored.bpm(), 128.0f);
  EXPECT_FLOAT_EQ(restored.playhead_sec(), 16.5f);
  EXPECT_EQ(restored.transport_state(),
            hibiki::pb::core::TRANSPORT_STATE_PLAYING);
  ASSERT_EQ(restored.tracks_size(), 1);
  EXPECT_EQ(restored.tracks(0).name(), "Synth Bass");
  EXPECT_EQ(restored.tracks(0).output_track_index(), -1);
  ASSERT_EQ(restored.tracks(0).plugins_size(), 1);
  EXPECT_TRUE(restored.tracks(0).plugins(0).has_sidechain_track_index());
  EXPECT_EQ(restored.tracks(0).plugins(0).sidechain_track_index(), 0);
  EXPECT_EQ(restored.tracks(0).plugins(0).state(),
            std::string("\x01\x02\x03\x04", 4));
  ASSERT_EQ(restored.tracks(0).timeline_clips_size(), 1);
  const auto& rclip = restored.tracks(0).timeline_clips(0).clip();
  ASSERT_EQ(rclip.waveform_summary_size(), 3);
  EXPECT_FLOAT_EQ(rclip.waveform_summary(0), -0.75f);
  EXPECT_FLOAT_EQ(rclip.waveform_summary(1), 0.25f);
  EXPECT_FLOAT_EQ(rclip.waveform_summary(2), 0.95f);
  ASSERT_EQ(rclip.midi_events_size(), 1);
  EXPECT_EQ(rclip.midi_events(0).cc_value(), -8192);
}

TEST_F(PbDInteropTest, RequestAndNotificationOneofRoundtrip) {
  hibiki::pb::commands::Request req;
  auto* trkCmd = req.mutable_track();
  trkCmd->set_action(hibiki::pb::commands::TrackCmd::ACTION_PLAY_SLOT);
  trkCmd->mutable_target()->set_track_index(3);
  trkCmd->mutable_target()->set_session_slot(0);  // 0-valued oneof variant
  trkCmd->set_target_track_index(-1);

  std::string reqBytes;
  ASSERT_TRUE(req.SerializeToString(&reqBytes));

  std::vector<uint8_t> outBuf(4096, 0);
  int dLen =
      d_roundtrip_request(reinterpret_cast<const uint8_t*>(reqBytes.data()),
                          static_cast<int>(reqBytes.size()), outBuf.data(),
                          static_cast<int>(outBuf.size()));
  ASSERT_GT(dLen, 0);

  hibiki::pb::commands::Request restoredReq;
  ASSERT_TRUE(restoredReq.ParseFromArray(outBuf.data(), dLen));
  ASSERT_TRUE(restoredReq.has_track());
  EXPECT_EQ(restoredReq.track().action(),
            hibiki::pb::commands::TrackCmd::ACTION_PLAY_SLOT);
  EXPECT_EQ(restoredReq.track().target().track_index(), 3u);
  EXPECT_TRUE(restoredReq.track().target().has_session_slot());
  EXPECT_EQ(restoredReq.track().target().session_slot(), 0u);
  EXPECT_EQ(restoredReq.track().target_track_index(), -1);

  // Notification with empty ClearProject message inside oneof
  hibiki::pb::notifications::Notification notif;
  notif.mutable_clear_project();
  std::string notifBytes;
  ASSERT_TRUE(notif.SerializeToString(&notifBytes));

  int nLen = d_roundtrip_notification(
      reinterpret_cast<const uint8_t*>(notifBytes.data()),
      static_cast<int>(notifBytes.size()), outBuf.data(),
      static_cast<int>(outBuf.size()));
  ASSERT_GT(nLen, 0);

  hibiki::pb::notifications::Notification restoredNotif;
  ASSERT_TRUE(restoredNotif.ParseFromArray(outBuf.data(), nLen));
  EXPECT_TRUE(restoredNotif.has_clear_project());
}

TEST_F(PbDInteropTest, RealtimeNogcProcessAudioDecodedByCppProtobuf) {
  const float samples[4] = {0.125f, -0.25f, 0.5f, -0.875f};
  std::vector<uint8_t> outBuf(512, 0);

  int written = d_encode_realtime_process_audio(
      2, 48000.0, 125.0, samples, 4, outBuf.data(),
      static_cast<int>(outBuf.size()));
  ASSERT_GT(written, 0);

  hibiki::pb::worker::ProcessAudio msg;
  ASSERT_TRUE(msg.ParseFromArray(outBuf.data(), written));
  EXPECT_EQ(msg.num_samples(), 2);
  EXPECT_DOUBLE_EQ(msg.sample_rate(), 48000.0);
  EXPECT_DOUBLE_EQ(msg.tempo(), 125.0);
  EXPECT_EQ(msg.time_sig_numerator(), 4);
  EXPECT_EQ(msg.time_sig_denominator(), 4);
  EXPECT_EQ(msg.continuous_time_samples(), 2048);
  EXPECT_DOUBLE_EQ(msg.project_time_music(), 4.0);
  EXPECT_TRUE(msg.has_inputs());
  ASSERT_EQ(msg.input_audio().size(), sizeof(samples));
  float decodedSamples[4] = {};
  std::memcpy(decodedSamples, msg.input_audio().data(), sizeof(samples));
  for (int i = 0; i < 4; ++i) {
    EXPECT_FLOAT_EQ(decodedSamples[i], samples[i]);
  }
}
