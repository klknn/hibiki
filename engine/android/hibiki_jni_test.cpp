#include "engine/android/hibiki_jni.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "pb/commands.pb.h"
#include "pb/notifications.pb.h"

namespace hibiki {
namespace {

class AndroidEngineContextTest : public ::testing::Test {
 protected:
  void SetUp() override { engine_ = std::make_unique<AndroidEngineContext>(); }

  void TearDown() override {
    if (engine_) {
      engine_->destroy();
      engine_.reset();
    }
  }

  std::unique_ptr<AndroidEngineContext> engine_;
};

TEST_F(AndroidEngineContextTest, InitializeAndDestroy) {
  auto status = engine_->init(44100, 50);
  EXPECT_TRUE(status.ok()) << "Engine initialization failed: "
                           << status.message();

  EXPECT_FALSE(engine_->isPlaying())
      << "Engine should not be playing upon startup";
  EXPECT_DOUBLE_EQ(engine_->getBpm(), 120.0) << "Default BPM should be 120.0";
  EXPECT_DOUBLE_EQ(engine_->getPlaybackPosition(), 0.0)
      << "Initial playback position should be 0.0s";

  engine_->destroy();
}

TEST_F(AndroidEngineContextTest, TransportAndBpmControl) {
  ASSERT_TRUE(engine_->init(44100, 50).ok());

  // Test BPM change
  engine_->setBpm(138.0);
  EXPECT_DOUBLE_EQ(engine_->getBpm(), 138.0) << "BPM was not updated correctly";

  // Test Playback control
  engine_->setPlayback(true);
  EXPECT_TRUE(engine_->isPlaying()) << "Engine should report isPlaying == true";

  // Let audio loop run briefly
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  engine_->setPlayback(false);
  EXPECT_FALSE(engine_->isPlaying()) << "Engine should stop playback";
}

TEST_F(AndroidEngineContextTest, SendProtobufRequests) {
  ASSERT_TRUE(engine_->init(44100, 50).ok());

  // Send Transport Play Request
  pb::commands::Request play_req;
  auto* transport = play_req.mutable_transport();
  transport->set_action(pb::commands::TransportCmd::ACTION_PLAY);

  std::string play_payload;
  ASSERT_TRUE(play_req.SerializeToString(&play_payload));

  auto status = engine_->sendRequest(
      reinterpret_cast<const uint8_t*>(play_payload.data()),
      play_payload.size());
  EXPECT_TRUE(status.ok()) << "Transport request failed: " << status.message();

  // Send Track Volume Request
  pb::commands::Request track_req;
  auto* track_cmd = track_req.mutable_track();
  track_cmd->set_action(pb::commands::TrackCmd::ACTION_SET_VOLUME);
  track_cmd->mutable_target()->set_track_index(0);
  track_cmd->set_value(0.85f);

  std::string track_payload;
  ASSERT_TRUE(track_req.SerializeToString(&track_payload));

  status = engine_->sendRequest(
      reinterpret_cast<const uint8_t*>(track_payload.data()),
      track_payload.size());
  EXPECT_TRUE(status.ok()) << "Track request failed: " << status.message();

  // Poll notifications
  auto notif_bytes = engine_->pollNotification();
  if (!notif_bytes.empty()) {
    pb::notifications::Notification notif;
    EXPECT_TRUE(notif.ParseFromArray(notif_bytes.data(), notif_bytes.size()))
        << "Notification parsing failed";
  }
}

TEST_F(AndroidEngineContextTest, RejectInvalidProtobufPayload) {
  ASSERT_TRUE(engine_->init(44100, 50).ok());

  // Garbage bytes
  std::vector<uint8_t> garbage = {0xFF, 0xFE, 0xFD, 0xFC, 0x00, 0x11};
  auto status = engine_->sendRequest(garbage.data(), garbage.size());
  EXPECT_FALSE(status.ok()) << "Malformed protobuf should return error status";
}

}  // namespace
}  // namespace hibiki
