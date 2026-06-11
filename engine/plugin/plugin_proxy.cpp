// Platform-neutral implementation of PluginProxy.
// Platform-specific methods (spawnLocalWorker, isWorkerAlive, killWorker)
// are in plugin_proxy_posix.cpp / plugin_proxy_win32.cpp.

#include "engine/plugin/plugin_proxy.hpp"

#include <cstring>
#include <iostream>
#include <random>

#include "absl/log/log.h"
#include "engine/ipc/worker_channel_tcp.hpp"
#include "pb/plugin_worker.pb.h"

namespace hibiki {

namespace {

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

// Shared helper used by platform-specific files.
std::string generateUniqueId() {
  static std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<uint32_t> dist;
  char buf[32];
  snprintf(buf, sizeof(buf), "%08x", dist(rng));
  return buf;
}

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

  killWorker();  // Platform-specific cleanup (posix or win32)
  channel_.reset();
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
    LOG(ERROR) << "PluginProxy: load failed: "
               << (resp.has_load_result() ? resp.load_result().error()
                                          : "no response");
    return false;
  }

  name_ = resp.load_result().name();
  is_instrument_ = resp.load_result().is_instrument();
  return true;
}

void PluginProxy::process(float** inputs, float** outputs, int num_samples,
                          const HostProcessContext& context,
                          const std::vector<MidiNoteEvent>& events,
                          float** /*sidechain*/) {
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
void PluginProxy::showEditor() {
  if (!channel_ || !isWorkerAlive()) return;
  hibiki::pb::worker::WorkerRequest req;
  req.mutable_show_editor();
  hibiki::pb::worker::WorkerResponse resp;
  sendRequest(channel_.get(), req, resp);
}

void PluginProxy::stopEditor() {
  if (!channel_ || !isWorkerAlive()) return;
  hibiki::pb::worker::WorkerRequest req;
  req.mutable_stop_editor();
  hibiki::pb::worker::WorkerResponse resp;
  sendRequest(channel_.get(), req, resp);
}

bool PluginProxy::captureEditorFrame(std::vector<uint8_t>& rgba, int& w,
                                     int& h) {
  if (!channel_ || !isWorkerAlive()) return false;
  hibiki::pb::worker::WorkerRequest req;
  req.mutable_get_editor_frame();
  hibiki::pb::worker::WorkerResponse resp;
  if (!sendRequest(channel_.get(), req, resp)) return false;
  if (!resp.has_editor_frame()) return false;
  const auto& frame = resp.editor_frame();
  w = frame.width();
  h = frame.height();
  const auto& data = frame.image_data();
  rgba.assign(data.begin(), data.end());
  return !rgba.empty();
}

void PluginProxy::sendEditorInput(int type, int x, int y, int button,
                                  int key_code, int delta) {
  if (!channel_ || !isWorkerAlive()) return;
  hibiki::pb::worker::WorkerRequest req;
  auto* input = req.mutable_editor_input();
  input->set_type(static_cast<hibiki::pb::worker::EditorInput::Type>(type));
  input->set_x(x);
  input->set_y(y);
  input->set_button(button);
  input->set_key_code(key_code);
  input->set_delta(delta);
  hibiki::pb::worker::WorkerResponse resp;
  sendRequest(channel_.get(), req, resp);
}

bool PluginProxy::getState(std::vector<uint8_t>& state) const {
  if (!channel_ || !isWorkerAlive()) return false;

  hibiki::pb::worker::WorkerRequest req;
  req.mutable_get_state();

  hibiki::pb::worker::WorkerResponse resp;
  // const_cast is safe since sendRequest doesn't modify the channel state
  if (!sendRequest(const_cast<WorkerChannel*>(channel_.get()), req, resp))
    return false;

  if (!resp.has_state_result() || !resp.state_result().success()) {
    return false;
  }

  const auto& state_bytes = resp.state_result().state();
  state.assign(state_bytes.begin(), state_bytes.end());
  return true;
}

bool PluginProxy::setState(const std::vector<uint8_t>& state) {
  if (!channel_ || !isWorkerAlive()) return false;

  hibiki::pb::worker::WorkerRequest req;
  auto* cmd = req.mutable_set_state();
  cmd->set_state(state.data(), state.size());

  hibiki::pb::worker::WorkerResponse resp;
  if (!sendRequest(channel_.get(), req, resp)) return false;

  return resp.has_state_result() && resp.state_result().success();
}

}  // namespace hibiki
