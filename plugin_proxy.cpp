#include "plugin_proxy.hpp"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <random>

#include "pb/plugin_worker.pb.h"
#include "vst3_host.hpp"
#include "worker_channel_unix.hpp"

namespace {

// Generate a unique ID for socket/shm names
std::string generateUniqueId() {
  static std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<uint32_t> dist;
  char buf[32];
  snprintf(buf, sizeof(buf), "%08x", dist(rng));
  return buf;
}

// Find the hbk-plugin-worker binary next to the current executable
std::string findWorkerBinary() {
  char exe_path[1024];
  ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len <= 0) return "hbk-plugin-worker";
  exe_path[len] = '\0';
  std::string path(exe_path);
  size_t slash = path.find_last_of('/');
  if (slash != std::string::npos) {
    path = path.substr(0, slash + 1) + "hbk-plugin-worker";
  }
  return path;
}

// Helper: send a WorkerRequest and receive a WorkerResponse
bool sendRequest(WorkerChannelUnix* channel,
                 const hibiki::pb::worker::WorkerRequest& req,
                 hibiki::pb::worker::WorkerResponse& resp) {
  std::string data;
  req.SerializeToString(&data);
  if (!channel->sendMessage(data.data(), data.size())) return false;

  std::string resp_data;
  if (channel->recvMessage(resp_data) < 0) return false;
  return resp.ParseFromString(resp_data);
}

}  // namespace

PluginProxy::PluginProxy() = default;

PluginProxy::~PluginProxy() {
  if (channel_ && isWorkerAlive()) {
    // Send shutdown command
    hibiki::pb::worker::WorkerRequest req;
    req.mutable_shutdown();
    std::string data;
    req.SerializeToString(&data);
    channel_->sendMessage(data.data(), data.size());
  }

  // Wait for worker to exit
  if (worker_pid_ > 0) {
    int status;
    waitpid(worker_pid_, &status, WNOHANG);
    if (isWorkerAlive()) {
      kill(worker_pid_, SIGTERM);
      waitpid(worker_pid_, &status, 0);
    }
  }

  channel_.reset();
}

bool PluginProxy::spawnWorker() {
  std::string uid = generateUniqueId();
  socket_path_ = "/tmp/hbk-plugin-" + uid + ".sock";
  shm_name_ = "/hbk-plugin-" + uid;

  // Create server-side channel (creates socket + shm)
  auto* ch = WorkerChannelUnix::createServer(socket_path_, shm_name_, 512, 2);
  if (!ch) return false;
  channel_.reset(ch);

  // Fork worker process
  std::string worker_bin = findWorkerBinary();
  worker_pid_ = fork();
  if (worker_pid_ < 0) {
    std::cerr << "PluginProxy: fork() failed: " << strerror(errno) << "\n";
    channel_.reset();
    return false;
  }

  if (worker_pid_ == 0) {
    // Child: exec the worker binary
    execl(worker_bin.c_str(), "hbk-plugin-worker", socket_path_.c_str(),
          shm_name_.c_str(), nullptr);
    // exec failed
    std::cerr << "PluginProxy: execl() failed: " << strerror(errno) << "\n";
    _exit(1);
  }

  // Parent: accept the connection
  if (!channel_->accept()) {
    std::cerr << "PluginProxy: accept() failed\n";
    kill(worker_pid_, SIGTERM);
    waitpid(worker_pid_, nullptr, 0);
    worker_pid_ = -1;
    channel_.reset();
    return false;
  }

  return true;
}

bool PluginProxy::isWorkerAlive() const {
  if (worker_pid_ <= 0) return false;
  int status;
  pid_t result = waitpid(worker_pid_, &status, WNOHANG);
  return result == 0;  // 0 = still running
}

bool PluginProxy::load(const std::string& path, int plugin_index,
                       double sample_rate) {
  path_ = path;
  plugin_index_ = plugin_index;
  sample_rate_ = sample_rate;

  if (!spawnWorker()) return false;

  // Send LoadPlugin command
  hibiki::pb::worker::WorkerRequest req;
  auto* cmd = req.mutable_load();
  cmd->set_path(path);
  cmd->set_plugin_index(plugin_index);
  cmd->set_sample_rate(sample_rate);

  hibiki::pb::worker::WorkerResponse resp;
  if (!sendRequest(channel_.get(), req, resp)) return false;

  if (!resp.has_load_result() || !resp.load_result().success()) {
    std::cerr << "PluginProxy: load failed: "
              << (resp.has_load_result() ? resp.load_result().error()
                                        : "no response")
              << "\n";
    return false;
  }

  name_ = resp.load_result().name();
  is_instrument_ = resp.load_result().is_instrument();
  return true;
}

void PluginProxy::process(float** inputs, float** outputs, int num_samples,
                          const HostProcessContext& context,
                          const std::vector<MidiNoteEvent>& events) {
  if (!channel_ || !isWorkerAlive()) return;

  // Write input audio to shared memory
  if (inputs) {
    for (int ch = 0; ch < 2; ++ch) {
      float* shm_in = channel_->inputBuffer(ch);
      if (shm_in && inputs[ch]) {
        memcpy(shm_in, inputs[ch], num_samples * sizeof(float));
      }
    }
  }

  // Build process command
  hibiki::pb::worker::WorkerRequest req;
  auto* cmd = req.mutable_process();
  cmd->set_num_samples(num_samples);
  cmd->set_has_inputs(inputs != nullptr);
  cmd->set_sample_rate(context.sampleRate);
  cmd->set_tempo(context.tempo);
  cmd->set_time_sig_numerator(context.timeSigNumerator);
  cmd->set_time_sig_denominator(context.timeSigDenominator);
  cmd->set_continuous_time_samples(context.continuousTimeSamples);
  cmd->set_project_time_music(context.projectTimeMusic);

  for (const auto& e : events) {
    auto* me = cmd->add_midi_events();
    me->set_sample_offset(e.sampleOffset);
    me->set_channel(e.channel);
    me->set_pitch(e.pitch);
    me->set_velocity(e.velocity);
    me->set_is_note_on(e.isNoteOn);
  }

  hibiki::pb::worker::WorkerResponse resp;
  if (!sendRequest(channel_.get(), req, resp)) return;

  // Read output audio from shared memory
  if (outputs) {
    for (int ch = 0; ch < 2; ++ch) {
      float* shm_out = channel_->outputBuffer(ch);
      if (shm_out && outputs[ch]) {
        memcpy(outputs[ch], shm_out, num_samples * sizeof(float));
      }
    }
  }
}

void PluginProxy::setParameterValue(uint32_t id, double valueNormalized) {
  if (!channel_ || !isWorkerAlive()) return;
  hibiki::pb::worker::WorkerRequest req;
  auto* cmd = req.mutable_set_param();
  cmd->set_param_id(id);
  cmd->set_value(valueNormalized);

  hibiki::pb::worker::WorkerResponse resp;
  sendRequest(channel_.get(), req, resp);
}

double PluginProxy::getParameterValue(uint32_t id) const {
  if (!channel_ || !isWorkerAlive()) return 0.0;
  hibiki::pb::worker::WorkerRequest req;
  auto* cmd = req.mutable_get_param();
  cmd->set_param_id(id);

  hibiki::pb::worker::WorkerResponse resp;
  if (!sendRequest(const_cast<WorkerChannelUnix*>(channel_.get()), req, resp))
    return 0.0;
  if (resp.has_param_value()) return resp.param_value().value();
  return 0.0;
}

int PluginProxy::getParameterCount() const {
  if (!channel_ || !isWorkerAlive()) return 0;
  hibiki::pb::worker::WorkerRequest req;
  req.mutable_get_param_count();

  hibiki::pb::worker::WorkerResponse resp;
  if (!sendRequest(const_cast<WorkerChannelUnix*>(channel_.get()), req, resp))
    return 0;
  if (resp.has_param_count()) return resp.param_count().count();
  return 0;
}

bool PluginProxy::getParameterInfo(int index, VstParamInfo& info) const {
  if (!channel_ || !isWorkerAlive()) return false;
  hibiki::pb::worker::WorkerRequest req;
  req.mutable_get_param_info()->set_index(index);

  hibiki::pb::worker::WorkerResponse resp;
  if (!sendRequest(const_cast<WorkerChannelUnix*>(channel_.get()), req, resp))
    return false;
  if (resp.has_param_info() && resp.param_info().found()) {
    info.id = resp.param_info().id();
    info.name = resp.param_info().name();
    info.defaultValue = resp.param_info().default_value();
    return true;
  }
  return false;
}

const std::string& PluginProxy::getName() const { return name_; }
const std::string& PluginProxy::getPath() const { return path_; }
int PluginProxy::getPluginIndex() const { return plugin_index_; }
bool PluginProxy::isInstrument() const { return is_instrument_; }

void PluginProxy::showEditor() {
  // Editor display is not supported in sandboxed mode.
  // The plugin GUI requires in-process hosting.
}

void PluginProxy::stopEditor() {
  // No-op for sandboxed plugins.
}
