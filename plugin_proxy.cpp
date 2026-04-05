#include "plugin_proxy.hpp"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <random>

#include "pb/plugin_worker.pb.h"
#include "vst3_host.hpp"
#include "worker_channel_local.hpp"
#include "worker_channel_tcp.hpp"


namespace hibiki {

namespace {

std::string generateUniqueId() {
  static std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<uint32_t> dist;
  char buf[32];
  snprintf(buf, sizeof(buf), "%08x", dist(rng));
  return buf;
}

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

bool sendRequest(WorkerChannel* channel,
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

// Local sandbox mode
PluginProxy::PluginProxy() : is_remote_(false) {}

// Remote mode
PluginProxy::PluginProxy(const std::string& remote_host, int remote_port)
    : is_remote_(true), remote_host_(remote_host), remote_port_(remote_port) {}

PluginProxy::~PluginProxy() {
  if (channel_) {
    // Send shutdown command
    hibiki::pb::worker::WorkerRequest req;
    req.mutable_shutdown();
    std::string data;
    req.SerializeToString(&data);
    channel_->sendMessage(data.data(), data.size());
  }

  if (!is_remote_ && worker_pid_ > 0) {
    int status;
    waitpid(worker_pid_, &status, WNOHANG);
    if (isWorkerAlive()) {
      kill(worker_pid_, SIGTERM);
      waitpid(worker_pid_, &status, 0);
    }
  }

  channel_.reset();
}

bool PluginProxy::spawnLocalWorker() {
  std::string uid = generateUniqueId();
  socket_path_ = "/tmp/hbk-plugin-" + uid + ".sock";
  shm_name_ = "/hbk-plugin-" + uid;

  auto* ch = WorkerChannelLocal::createServer(socket_path_, shm_name_, 512, 2);
  if (!ch) return false;
  channel_.reset(ch);

  std::string worker_bin = findWorkerBinary();
  worker_pid_ = fork();
  if (worker_pid_ < 0) {
    std::cerr << "PluginProxy: fork() failed: " << strerror(errno) << "\n";
    channel_.reset();
    return false;
  }

  if (worker_pid_ == 0) {
    execl(worker_bin.c_str(), "hbk-plugin-worker", socket_path_.c_str(),
          shm_name_.c_str(), nullptr);
    std::cerr << "PluginProxy: execl() failed: " << strerror(errno) << "\n";
    _exit(1);
  }

  if (!static_cast<WorkerChannelLocal*>(channel_.get())->accept()) {
    std::cerr << "PluginProxy: accept() failed\n";
    kill(worker_pid_, SIGTERM);
    waitpid(worker_pid_, nullptr, 0);
    worker_pid_ = -1;
    channel_.reset();
    return false;
  }

  return true;
}

bool PluginProxy::connectRemote() {
  auto* ch = WorkerChannelTcp::createClient(remote_host_, remote_port_, 512, 2);
  if (!ch) return false;
  channel_.reset(ch);

  // Send config handshake
  hibiki::pb::worker::WorkerRequest req;
  auto* cfg = req.mutable_config();
  cfg->set_block_size(512);
  cfg->set_num_channels(2);
  cfg->set_use_shared_memory(false);

  hibiki::pb::worker::WorkerResponse resp;
  if (!sendRequest(channel_.get(), req, resp)) {
    channel_.reset();
    return false;
  }

  return true;
}

bool PluginProxy::isWorkerAlive() const {
  if (is_remote_) return channel_ != nullptr;
  if (worker_pid_ <= 0) return false;
  int status;
  pid_t result = waitpid(worker_pid_, &status, WNOHANG);
  return result == 0;
}

bool PluginProxy::load(const std::string& path, int plugin_index,
                       double sample_rate) {
  path_ = path;
  plugin_index_ = plugin_index;
  sample_rate_ = sample_rate;

  if (is_remote_) {
    if (!connectRemote()) return false;
  } else {
    if (!spawnLocalWorker()) return false;
  }

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

  if (is_remote_) {
    // Remote mode: serialize input audio into proto
    if (inputs) {
      std::string audio_data;
      audio_data.resize(2 * num_samples * sizeof(float));
      float* dst = reinterpret_cast<float*>(audio_data.data());
      for (int ch = 0; ch < 2; ++ch) {
        if (inputs[ch]) {
          memcpy(dst + ch * num_samples, inputs[ch],
                 num_samples * sizeof(float));
        }
      }
      cmd->set_input_audio(std::move(audio_data));
    }
  } else {
    // Local mode: write inputs to shared memory
    if (inputs) {
      for (int ch = 0; ch < 2; ++ch) {
        float* shm_in = channel_->inputBuffer(ch);
        if (shm_in && inputs[ch]) {
          memcpy(shm_in, inputs[ch], num_samples * sizeof(float));
        }
      }
    }
  }

  hibiki::pb::worker::WorkerResponse resp;
  if (!sendRequest(channel_.get(), req, resp)) return;

  if (is_remote_) {
    // Remote mode: deserialize output audio from proto
    if (outputs && resp.has_process_done() &&
        !resp.process_done().output_audio().empty()) {
      const float* src = reinterpret_cast<const float*>(
          resp.process_done().output_audio().data());
      int total =
          (int)(resp.process_done().output_audio().size() / sizeof(float));
      for (int ch = 0; ch < 2 && ch * num_samples < total; ++ch) {
        if (outputs[ch]) {
          memcpy(outputs[ch], src + ch * num_samples,
                 num_samples * sizeof(float));
        }
      }
    }
  } else {
    // Local mode: read outputs from shared memory
    if (outputs) {
      for (int ch = 0; ch < 2; ++ch) {
        float* shm_out = channel_->outputBuffer(ch);
        if (shm_out && outputs[ch]) {
          memcpy(outputs[ch], shm_out, num_samples * sizeof(float));
        }
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
  req.mutable_get_param()->set_param_id(id);
  hibiki::pb::worker::WorkerResponse resp;
  if (!sendRequest(const_cast<WorkerChannel*>(channel_.get()), req, resp))
    return 0.0;
  if (resp.has_param_value()) return resp.param_value().value();
  return 0.0;
}

int PluginProxy::getParameterCount() const {
  if (!channel_ || !isWorkerAlive()) return 0;
  hibiki::pb::worker::WorkerRequest req;
  req.mutable_get_param_count();
  hibiki::pb::worker::WorkerResponse resp;
  if (!sendRequest(const_cast<WorkerChannel*>(channel_.get()), req, resp))
    return 0;
  if (resp.has_param_count()) return resp.param_count().count();
  return 0;
}

bool PluginProxy::getParameterInfo(int index, VstParamInfo& info) const {
  if (!channel_ || !isWorkerAlive()) return false;
  hibiki::pb::worker::WorkerRequest req;
  req.mutable_get_param_info()->set_index(index);
  hibiki::pb::worker::WorkerResponse resp;
  if (!sendRequest(const_cast<WorkerChannel*>(channel_.get()), req, resp))
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
void PluginProxy::showEditor() {}
void PluginProxy::stopEditor() {}

}  // namespace hibiki
