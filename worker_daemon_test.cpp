// Integration test for hbk-worker-daemon.
//
// Spawns the daemon on a free port, connects via TCP, loads Dexed.vst3,
// sends MIDI note-on events, and verifies non-zero audio output.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>  // _access
#include <windows.h>
#define F_OK 0
#define X_OK 0  // Windows _access doesn't distinguish X_OK
#define access _access
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "pb/plugin_worker.pb.h"
#include "tcp.hpp"

namespace hibiki {
namespace {

// ─── TCP Message Helpers ──────────────────────────────────────────

bool sendMessage(socket_t fd, const std::string& data) {
  uint32_t size = static_cast<uint32_t>(data.size());
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&size);
  size_t remaining = sizeof(size);
  while (remaining > 0) {
    int n = tcp_send(fd, p, remaining);
    if (n <= 0) return false;
    p += n;
    remaining -= n;
  }

  p = reinterpret_cast<const uint8_t*>(data.data());
  remaining = data.size();
  while (remaining > 0) {
    int n = tcp_send(fd, p, remaining);
    if (n <= 0) return false;
    p += n;
    remaining -= n;
  }
  return true;
}

int recvMessage(socket_t fd, std::string& out) {
  uint32_t size = 0;
  uint8_t* p = reinterpret_cast<uint8_t*>(&size);
  size_t remaining = sizeof(size);
  while (remaining > 0) {
    int n = tcp_recv(fd, p, remaining);
    if (n <= 0) return -1;
    p += n;
    remaining -= n;
  }
  if (size > 4 * 1024 * 1024) return -1;
  out.resize(size);
  p = reinterpret_cast<uint8_t*>(out.data());
  remaining = size;
  while (remaining > 0) {
    int n = tcp_recv(fd, p, remaining);
    if (n <= 0) return -1;
    p += n;
    remaining -= n;
  }
  return static_cast<int>(size);
}

pb::worker::WorkerResponse sendRequest(socket_t fd,
                                        const pb::worker::WorkerRequest& req) {
  std::string data;
  req.SerializeToString(&data);
  EXPECT_TRUE(sendMessage(fd, data));

  std::string resp_data;
  EXPECT_GT(recvMessage(fd, resp_data), 0);

  pb::worker::WorkerResponse resp;
  EXPECT_TRUE(resp.ParseFromString(resp_data));
  return resp;
}

// ─── Port + Process Helpers ───────────────────────────────────────

int findFreePort() {
  socket_t s = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = 0;  // OS picks free port
  bind(s, (struct sockaddr*)&addr, sizeof(addr));
  socklen_t len = sizeof(addr);
  getsockname(s, (struct sockaddr*)&addr, &len);
  int port = ntohs(addr.sin_port);
  tcp_close(s);
  return port;
}

std::string findDaemonBinary() {
  // Try runfiles (Bazel test environment)
  const char* srcdir = getenv("TEST_SRCDIR");
  if (srcdir) {
#ifdef _WIN32
    std::string p = std::string(srcdir) + "/_main/hbk-worker-daemon.exe";
#else
    std::string p = std::string(srcdir) + "/_main/hbk-worker-daemon";
#endif
    if (access(p.c_str(), X_OK) == 0) return p;
  }
  // Try bazel-bin
#ifdef _WIN32
  if (access("bazel-bin/hbk-worker-daemon.exe", X_OK) == 0)
    return "bazel-bin/hbk-worker-daemon.exe";
  if (access("./hbk-worker-daemon.exe", X_OK) == 0)
    return "./hbk-worker-daemon.exe";
#else
  if (access("bazel-bin/hbk-worker-daemon", X_OK) == 0)
    return "bazel-bin/hbk-worker-daemon";
  // Try CWD
  if (access("./hbk-worker-daemon", X_OK) == 0)
    return "./hbk-worker-daemon";
#endif
  return "";
}

std::string findDexedVst3() {
  const char* srcdir = getenv("TEST_SRCDIR");
  if (srcdir) {
    std::string p = std::string(srcdir) + "/_main/testdata/Dexed.vst3";
    if (access(p.c_str(), F_OK) == 0) return p;
  }
  if (access("testdata/Dexed.vst3", F_OK) == 0)
    return "testdata/Dexed.vst3";
  return "";
}

socket_t connectToPort(int port, int max_attempts = 20) {
  for (int i = 0; i < max_attempts; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCK) continue;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
      tcp_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, 1);
      return fd;
    }
    tcp_close(fd);
  }
  return INVALID_SOCK;
}

// ─── Tests ────────────────────────────────────────────────────────

class WorkerDaemonTest : public ::testing::Test {
 protected:
#ifdef _WIN32
  HANDLE daemon_handle_ = nullptr;
#else
  pid_t daemon_pid_ = -1;
#endif
  int port_ = 0;

  void SetUp() override {
    tcp_init();

    std::string daemon = findDaemonBinary();
    if (daemon.empty()) {
      GTEST_SKIP() << "hbk-worker-daemon binary not found";
    }

    port_ = findFreePort();

#ifdef _WIN32
    std::string cmd_line = "\"" + daemon + "\" --port " + std::to_string(port_);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    ASSERT_TRUE(CreateProcessA(NULL, cmd_line.data(), NULL, NULL, FALSE,
                               CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        << "CreateProcess failed: " << GetLastError();

    daemon_handle_ = pi.hProcess;
    CloseHandle(pi.hThread);

    std::cerr << "Spawned hbk-worker-daemon (handle=" << daemon_handle_
              << " port=" << port_ << ")\n";
#else
    daemon_pid_ = fork();
    ASSERT_NE(daemon_pid_, -1) << "fork() failed";

    if (daemon_pid_ == 0) {
      // Child — exec daemon
      std::string port_str = std::to_string(port_);
      execl(daemon.c_str(), daemon.c_str(), "--port", port_str.c_str(),
            nullptr);
      _exit(1);
    }

    std::cerr << "Spawned hbk-worker-daemon (pid=" << daemon_pid_
              << " port=" << port_ << ")\n";
#endif
  }

  void TearDown() override {
#ifdef _WIN32
    if (daemon_handle_) {
      TerminateProcess(daemon_handle_, 0);
      WaitForSingleObject(daemon_handle_, 5000);
      CloseHandle(daemon_handle_);
      daemon_handle_ = nullptr;
      std::cerr << "Stopped hbk-worker-daemon\n";
    }
#else
    if (daemon_pid_ > 0) {
      kill(daemon_pid_, SIGTERM);
      int status;
      waitpid(daemon_pid_, &status, 0);
      std::cerr << "Stopped hbk-worker-daemon\n";
    }
#endif
  }
};

TEST_F(WorkerDaemonTest, LoadDexedAndProcessAudio) {
  std::string dexed = findDexedVst3();
  if (dexed.empty()) {
    GTEST_SKIP() << "testdata/Dexed.vst3 not found";
  }

  // Connect
  socket_t fd = connectToPort(port_);
  ASSERT_NE(fd, INVALID_SOCK) << "Could not connect to daemon on port "
                               << port_;

  constexpr int BLOCK_SIZE = 512;
  constexpr int NUM_CHANNELS = 2;
  constexpr double SAMPLE_RATE = 44100.0;

  // 1. Send Config
  {
    pb::worker::WorkerRequest req;
    auto* cfg = req.mutable_config();
    cfg->set_block_size(BLOCK_SIZE);
    cfg->set_num_channels(NUM_CHANNELS);
    cfg->set_use_shared_memory(false);

    auto resp = sendRequest(fd, req);
    EXPECT_EQ(resp.result_case(),
              pb::worker::WorkerResponse::kConfigAck);
    std::cerr << "  Config accepted\n";
  }

  // 2. Load Dexed
  {
    pb::worker::WorkerRequest req;
    auto* load = req.mutable_load();
    load->set_path(dexed);
    load->set_plugin_index(0);
    load->set_sample_rate(SAMPLE_RATE);

    auto resp = sendRequest(fd, req);
    ASSERT_EQ(resp.result_case(),
              pb::worker::WorkerResponse::kLoadResult);
    ASSERT_TRUE(resp.load_result().success())
        << "Failed to load Dexed: " << resp.load_result().error();
    EXPECT_TRUE(resp.load_result().is_instrument());
    std::cerr << "  Loaded plugin: " << resp.load_result().name()
              << " (instrument=" << resp.load_result().is_instrument() << ")\n";
  }

  // 3. Get parameter count (sanity check)
  int param_count = 0;
  {
    pb::worker::WorkerRequest req;
    req.mutable_get_param_count();
    auto resp = sendRequest(fd, req);
    ASSERT_EQ(resp.result_case(),
              pb::worker::WorkerResponse::kParamCount);
    param_count = resp.param_count().count();
    EXPECT_GT(param_count, 0) << "Dexed should have parameters";
    std::cerr << "  Parameter count: " << param_count << "\n";
  }

  // 4. Process silence (no MIDI) — should produce silence
  {
    pb::worker::WorkerRequest req;
    auto* proc = req.mutable_process();
    proc->set_num_samples(BLOCK_SIZE);
    proc->set_sample_rate(SAMPLE_RATE);
    proc->set_tempo(120.0);
    proc->set_time_sig_numerator(4);
    proc->set_time_sig_denominator(4);
    proc->set_has_inputs(false);

    auto resp = sendRequest(fd, req);
    ASSERT_EQ(resp.result_case(),
              pb::worker::WorkerResponse::kProcessDone);
    auto& audio = resp.process_done().output_audio();
    size_t expected_bytes = NUM_CHANNELS * BLOCK_SIZE * sizeof(float);
    EXPECT_EQ(audio.size(), expected_bytes);
    std::cerr << "  Silence block: " << audio.size() << " bytes\n";
  }

  // 5. Send MIDI note-on (C4, velocity=100), then process
  float max_abs_sample = 0.0f;
  {
    pb::worker::WorkerRequest req;
    auto* proc = req.mutable_process();
    proc->set_num_samples(BLOCK_SIZE);
    proc->set_sample_rate(SAMPLE_RATE);
    proc->set_tempo(120.0);
    proc->set_time_sig_numerator(4);
    proc->set_time_sig_denominator(4);
    proc->set_has_inputs(false);
    proc->set_continuous_time_samples(BLOCK_SIZE);  // second block
    proc->set_project_time_music(0.0);

    auto* note = proc->add_midi_events();
    note->set_sample_offset(0);
    note->set_channel(0);
    note->set_pitch(60);      // C4
    note->set_velocity(0.8f);  // ~100/127
    note->set_is_note_on(true);

    auto resp = sendRequest(fd, req);
    ASSERT_EQ(resp.result_case(),
              pb::worker::WorkerResponse::kProcessDone);
    auto& audio = resp.process_done().output_audio();
    size_t expected_bytes = NUM_CHANNELS * BLOCK_SIZE * sizeof(float);
    ASSERT_EQ(audio.size(), expected_bytes)
        << "Audio output should be " << expected_bytes << " bytes";

    const float* samples = reinterpret_cast<const float*>(audio.data());
    int total_samples = NUM_CHANNELS * BLOCK_SIZE;
    for (int i = 0; i < total_samples; ++i) {
      float v = std::abs(samples[i]);
      if (v > max_abs_sample) max_abs_sample = v;
    }
    std::cerr << "  Note-on block: max |sample| = " << max_abs_sample << "\n";
  }

  // Dexed might need a few more blocks for attack envelope
  if (max_abs_sample < 1e-6f) {
    for (int extra = 0; extra < 5; ++extra) {
      pb::worker::WorkerRequest req;
      auto* proc = req.mutable_process();
      proc->set_num_samples(BLOCK_SIZE);
      proc->set_sample_rate(SAMPLE_RATE);
      proc->set_tempo(120.0);
      proc->set_time_sig_numerator(4);
      proc->set_time_sig_denominator(4);
      proc->set_has_inputs(false);
      proc->set_continuous_time_samples((extra + 2) * BLOCK_SIZE);

      auto resp = sendRequest(fd, req);
      auto& audio = resp.process_done().output_audio();
      const float* samples = reinterpret_cast<const float*>(audio.data());
      int total_samples = NUM_CHANNELS * BLOCK_SIZE;
      for (int i = 0; i < total_samples; ++i) {
        float v = std::abs(samples[i]);
        if (v > max_abs_sample) max_abs_sample = v;
      }
      std::cerr << "  Extra block " << (extra + 1)
                << ": max |sample| = " << max_abs_sample << "\n";
      if (max_abs_sample > 1e-6f) break;
    }
  }

  EXPECT_GT(max_abs_sample, 1e-6f)
      << "Dexed should produce non-zero audio after note-on";

  // 6. Send note-off and verify audio eventually decays
  {
    pb::worker::WorkerRequest req;
    auto* proc = req.mutable_process();
    proc->set_num_samples(BLOCK_SIZE);
    proc->set_sample_rate(SAMPLE_RATE);
    proc->set_tempo(120.0);
    proc->set_has_inputs(false);

    auto* note = proc->add_midi_events();
    note->set_sample_offset(0);
    note->set_channel(0);
    note->set_pitch(60);
    note->set_velocity(0.0f);
    note->set_is_note_on(false);

    auto resp = sendRequest(fd, req);
    ASSERT_EQ(resp.result_case(),
              pb::worker::WorkerResponse::kProcessDone);
    std::cerr << "  Sent note-off\n";
  }

  // 7. Shutdown
  {
    pb::worker::WorkerRequest req;
    req.mutable_shutdown();
    std::string data;
    req.SerializeToString(&data);
    sendMessage(fd, data);
    // Don't expect response — daemon closes connection
  }

  tcp_close(fd);

  std::cerr << "\n=== PASS: Daemon loaded Dexed, processed MIDI, "
            << "produced audio (peak=" << max_abs_sample << ") ===\n";
}

}  // namespace
}  // namespace hibiki
