#include "engine/ipc/ipc.hpp"

#include <gtest/gtest.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>

#include <cstdint>
#define pipe(pfds) _pipe(pfds, 4096, _O_BINARY)
#define dup _dup
#define dup2 _dup2
#define read _read
#define STDOUT_FILENO 1
#define close _close
typedef intptr_t ssize_t;
#else
#include <unistd.h>
#endif

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

// The detached IPC sender thread blocks on a condition variable at process
// exit. Use a custom main that calls _exit() to avoid hanging.
int main(int argc, char** argv) {
  using namespace hibiki;
  ::testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();
  _exit(result);
}

namespace {
using namespace hibiki;

// The IPC framing format: MAGIC(4) + SIZE(4) + PROTO(N)
static const uint32_t IPC_MAGIC = 0x48424B49;  // "HBKI"

// Redirect stdout to a pipe, call the IPC function, read back the framed
// proto, and return the deserialized Notification.
// This helper enables g_ipc_enabled, so each test is self-contained.
class IpcCapture {
 public:
  IpcCapture() {
    hibiki::g_ipc_enabled = true;
    // Create a pipe
    EXPECT_EQ(pipe(pipefd_), 0);
    // Save original stdout and redirect to our pipe
    saved_stdout_ = dup(STDOUT_FILENO);
    dup2(pipefd_[1], STDOUT_FILENO);
  }

  // Read one framed notification from the captured output.
  // Returns true if a valid notification was read.
  bool readNotification(hibiki::pb::notifications::Notification& out) {
    // Flush and give the sender thread a moment to write
    fflush(stdout);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Restore stdout so we can still output test results
    dup2(saved_stdout_, STDOUT_FILENO);
    close(pipefd_[1]);
    pipefd_[1] = -1;

    // Read the magic header
    uint32_t magic = 0;
    ssize_t n = read(pipefd_[0], &magic, 4);
    if (n != 4 || magic != IPC_MAGIC) return false;

    // Read the size
    uint32_t size = 0;
    n = read(pipefd_[0], &size, 4);
    if (n != 4) return false;

    // Read the proto bytes
    std::vector<uint8_t> buf(size);
    size_t total_read = 0;
    while (total_read < size) {
      n = read(pipefd_[0], buf.data() + total_read, size - total_read);
      if (n <= 0) return false;
      total_read += n;
    }

    return out.ParseFromArray(buf.data(), buf.size());
  }

  ~IpcCapture() {
    hibiki::g_ipc_enabled = false;
    if (pipefd_[1] >= 0) {
      dup2(saved_stdout_, STDOUT_FILENO);
      close(pipefd_[1]);
    }
    close(pipefd_[0]);
    close(saved_stdout_);
  }

 private:
  int pipefd_[2];
  int saved_stdout_;
};

}  // namespace

class IpcTest : public ::testing::Test {
 protected:
  void SetUp() override { hibiki::g_ipc_enabled = false; }
};

TEST_F(IpcTest, SendAck) {
  IpcCapture cap;
  hibiki::sendAck("TEST_CMD", true);

  hibiki::pb::notifications::Notification notif;
  ASSERT_TRUE(cap.readNotification(notif));
  ASSERT_TRUE(notif.has_acknowledge());
  EXPECT_EQ(notif.acknowledge().command_type(), "TEST_CMD");
  EXPECT_TRUE(notif.acknowledge().success());
}

TEST_F(IpcTest, SendAckFailure) {
  IpcCapture cap;
  hibiki::sendAck("FAIL_CMD", false);

  hibiki::pb::notifications::Notification notif;
  ASSERT_TRUE(cap.readNotification(notif));
  ASSERT_TRUE(notif.has_acknowledge());
  EXPECT_EQ(notif.acknowledge().command_type(), "FAIL_CMD");
  EXPECT_FALSE(notif.acknowledge().success());
}

TEST_F(IpcTest, SendLog) {
  IpcCapture cap;
  hibiki::sendLog("hello world");

  hibiki::pb::notifications::Notification notif;
  ASSERT_TRUE(cap.readNotification(notif));
  ASSERT_TRUE(notif.has_log());
  EXPECT_EQ(notif.log().message(), "hello world");
}

TEST_F(IpcTest, SendClipInfo) {
  IpcCapture cap;
  hibiki::sendClipInfo(2, 3, "drums.wav", "/path/to/drums.wav");

  hibiki::pb::notifications::Notification notif;
  ASSERT_TRUE(cap.readNotification(notif));
  ASSERT_TRUE(notif.has_clip_info());
  EXPECT_EQ(notif.clip_info().track_index(), 2);
  EXPECT_EQ(notif.clip_info().slot_index(), 3);
  EXPECT_EQ(notif.clip_info().name(), "drums.wav");
  EXPECT_EQ(notif.clip_info().path(), "/path/to/drums.wav");
}

TEST_F(IpcTest, SendTimelineClipInfo) {
  IpcCapture cap;
  std::vector<float> waveform = {0.1f, 0.5f, -0.3f};
  hibiki::sendTimelineClipInfo(1, 0, "bass.mid", "/path/bass.mid", 2.5f, 4.0f,
                               waveform);

  hibiki::pb::notifications::Notification notif;
  ASSERT_TRUE(cap.readNotification(notif));
  ASSERT_TRUE(notif.has_timeline_clip_info());
  auto& tci = notif.timeline_clip_info();
  EXPECT_EQ(tci.track_index(), 1);
  EXPECT_EQ(tci.clip_index(), 0);
  EXPECT_EQ(tci.name(), "bass.mid");
  EXPECT_FLOAT_EQ(tci.start_time(), 2.5f);
  EXPECT_FLOAT_EQ(tci.duration(), 4.0f);
  ASSERT_EQ(tci.waveform_size(), 3);
  EXPECT_FLOAT_EQ(tci.waveform(0), 0.1f);
  EXPECT_FLOAT_EQ(tci.waveform(1), 0.5f);
  EXPECT_FLOAT_EQ(tci.waveform(2), -0.3f);
}

TEST_F(IpcTest, SendPlayheadInfo) {
  IpcCapture cap;
  hibiki::sendPlayheadInfo(10.5f, 128.0f, true);

  hibiki::pb::notifications::Notification notif;
  ASSERT_TRUE(cap.readNotification(notif));
  ASSERT_TRUE(notif.has_playhead_info());
  EXPECT_FLOAT_EQ(notif.playhead_info().position_sec(), 10.5f);
  EXPECT_FLOAT_EQ(notif.playhead_info().bpm(), 128.0f);
  EXPECT_EQ(notif.playhead_info().transport_state(),
            hibiki::pb::core::TRANSPORT_STATE_PLAYING);
}

TEST_F(IpcTest, SendPlayheadInfoStopped) {
  IpcCapture cap;
  hibiki::sendPlayheadInfo(0.0f, 120.0f, false);

  hibiki::pb::notifications::Notification notif;
  ASSERT_TRUE(cap.readNotification(notif));
  ASSERT_TRUE(notif.has_playhead_info());
  EXPECT_EQ(notif.playhead_info().transport_state(),
            hibiki::pb::core::TRANSPORT_STATE_STOPPED);
}

TEST_F(IpcTest, SendBounceFinished) {
  IpcCapture cap;
  hibiki::sendBounceFinished("/out/mix.wav", true);

  hibiki::pb::notifications::Notification notif;
  ASSERT_TRUE(cap.readNotification(notif));
  ASSERT_TRUE(notif.has_bounce_finished());
  EXPECT_EQ(notif.bounce_finished().path(), "/out/mix.wav");
  EXPECT_TRUE(notif.bounce_finished().success());
}

TEST_F(IpcTest, SendTrackInfo) {
  IpcCapture cap;
  hibiki::sendTrackInfo(3, "Synth Lead");

  hibiki::pb::notifications::Notification notif;
  ASSERT_TRUE(cap.readNotification(notif));
  ASSERT_TRUE(notif.has_track_info());
  EXPECT_EQ(notif.track_info().track_index(), 3);
  EXPECT_EQ(notif.track_info().name(), "Synth Lead");
}

TEST_F(IpcTest, SendClearProject) {
  IpcCapture cap;
  hibiki::sendClearProject();

  hibiki::pb::notifications::Notification notif;
  ASSERT_TRUE(cap.readNotification(notif));
  EXPECT_TRUE(notif.has_clear_project());
}

TEST_F(IpcTest, SendParamValueChange) {
  IpcCapture cap;
  hibiki::sendParamValueChange(0, 1, 42, 0.75f);

  hibiki::pb::notifications::Notification notif;
  ASSERT_TRUE(cap.readNotification(notif));
  ASSERT_TRUE(notif.has_param_value_change());
  EXPECT_EQ(notif.param_value_change().track_index(), 0);
  EXPECT_EQ(notif.param_value_change().plugin_index(), 1);
  EXPECT_EQ(notif.param_value_change().param_id(), 42u);
  EXPECT_FLOAT_EQ(notif.param_value_change().value(), 0.75f);
}

TEST_F(IpcTest, SendClipMidiData) {
  IpcCapture cap;
  std::vector<hibiki::pb::core::MidiEvent> notes;
  hibiki::pb::core::MidiEvent evt;
  evt.set_tick(480);
  evt.set_pitch(60);
  evt.set_duration_ticks(240);
  evt.set_velocity(100);
  notes.push_back(evt);

  hibiki::sendClipMidiData(0, 1, -1, 480, notes);

  hibiki::pb::notifications::Notification notif;
  ASSERT_TRUE(cap.readNotification(notif));
  ASSERT_TRUE(notif.has_clip_midi_data());
  auto& cmd = notif.clip_midi_data();
  EXPECT_EQ(cmd.track_index(), 0);
  EXPECT_EQ(cmd.slot_index(), 1);
  EXPECT_EQ(cmd.clip_index(), -1);
  EXPECT_EQ(cmd.resolution(), 480);
  ASSERT_EQ(cmd.events_size(), 1);
  EXPECT_EQ(cmd.events(0).pitch(), 60);
  EXPECT_EQ(cmd.events(0).tick(), 480);
  EXPECT_EQ(cmd.events(0).duration_ticks(), 240);
  EXPECT_EQ(cmd.events(0).velocity(), 100);
}

TEST_F(IpcTest, SendParamList) {
  IpcCapture cap;
  std::vector<VstParamInfo> params;
  VstParamInfo p1;
  p1.id = 10;
  p1.name = "Cutoff";
  p1.defaultValue = 0.5;
  params.push_back(p1);

  VstParamInfo p2;
  p2.id = 11;
  p2.name = "Resonance";
  p2.defaultValue = 0.25;
  params.push_back(p2);

  hibiki::sendParamList(0, 0, "Dexed", true, params);

  hibiki::pb::notifications::Notification notif;
  ASSERT_TRUE(cap.readNotification(notif));
  ASSERT_TRUE(notif.has_param_list());
  auto& pl = notif.param_list();
  EXPECT_EQ(pl.track_index(), 0);
  EXPECT_EQ(pl.plugin_index(), 0);
  EXPECT_EQ(pl.plugin_name(), "Dexed");
  EXPECT_TRUE(pl.is_instrument());
  ASSERT_EQ(pl.params_size(), 2);
  EXPECT_EQ(pl.params(0).id(), 10u);
  EXPECT_EQ(pl.params(0).name(), "Cutoff");
  EXPECT_FLOAT_EQ(pl.params(1).default_value(), 0.25);
}

TEST_F(IpcTest, SendPluginList) {
  IpcCapture cap;
  std::vector<PluginDescription> plugins;
  PluginDescription pd;
  pd.index = 0;
  pd.name = "Dexed";
  pd.vendor = "Digital Suburban";
  plugins.push_back(pd);

  hibiki::sendPluginList("/path/to/plugins", plugins);

  hibiki::pb::notifications::Notification notif;
  ASSERT_TRUE(cap.readNotification(notif));
  ASSERT_TRUE(notif.has_plugin_list());
  auto& pl = notif.plugin_list();
  EXPECT_EQ(pl.path(), "/path/to/plugins");
  ASSERT_EQ(pl.plugins_size(), 1);
  EXPECT_EQ(pl.plugins(0).name(), "Dexed");
  EXPECT_EQ(pl.plugins(0).vendor(), "Digital Suburban");
}

// Verify IPC is disabled when g_ipc_enabled is false
TEST_F(IpcTest, DisabledIpcDoesNotWrite) {
  // g_ipc_enabled is already false from SetUp
  // These should be no-ops
  EXPECT_NO_FATAL_FAILURE(hibiki::sendAck("NOOP", true));
  EXPECT_NO_FATAL_FAILURE(hibiki::sendLog("should not appear"));
  EXPECT_NO_FATAL_FAILURE(hibiki::sendClearProject());
}
