// Platform-neutral implementation of PluginProxy.
// Platform-specific methods (spawnLocalWorker, isWorkerAlive, killWorker)
// are in plugin_proxy_posix.cpp / plugin_proxy_win32.cpp.

#include "engine/plugin/plugin_proxy.hpp"

#include <cstring>
#include <iostream>
#include <random>
#include <utility>

#include "absl/log/log.h"
#include "engine/ipc/worker_channel_tcp.hpp"

extern "C" {
struct HbkWorkerMidiEvent {
  int sampleOffset;
  unsigned int channel;
  unsigned int pitch;
  float velocity;
  bool isNoteOn;
};

int hbk_worker_encode_shutdown(uint8_t* outBuf, int cap);
int hbk_worker_encode_config(int blockSize, int numChannels,
                             bool useSharedMemory, uint8_t* outBuf, int cap);
int hbk_worker_encode_load(const char* path, int pathLen, int pluginIndex,
                           double sampleRate, uint8_t* outBuf, int cap);
int hbk_worker_encode_process(int numSamples, bool hasInputs, double sampleRate,
                              double tempo, int timeSigNum, int timeSigDenom,
                              long long continuousTimeSamples,
                              double projectTimeMusic,
                              const HbkWorkerMidiEvent* events, int numEvents,
                              const uint8_t* inputAudio, int inputAudioLen,
                              uint8_t* outBuf, int cap);
int hbk_worker_encode_set_param(uint32_t paramId, double value, uint8_t* outBuf,
                                int cap);
int hbk_worker_encode_get_param(uint32_t paramId, uint8_t* outBuf, int cap);
int hbk_worker_encode_get_param_count(uint8_t* outBuf, int cap);
int hbk_worker_encode_get_param_info(int index, uint8_t* outBuf, int cap);
int hbk_worker_encode_show_editor(bool headless, uint8_t* outBuf, int cap);
int hbk_worker_encode_stop_editor(uint8_t* outBuf, int cap);
int hbk_worker_encode_get_editor_frame(uint8_t* outBuf, int cap);
int hbk_worker_encode_editor_input(int type, int x, int y, int button,
                                   int keyCode, int delta, uint8_t* outBuf,
                                   int cap);
int hbk_worker_encode_get_state(uint8_t* outBuf, int cap);
int hbk_worker_encode_set_state(const uint8_t* state, int stateLen,
                                uint8_t* outBuf, int cap);

bool hbk_worker_decode_ack(const uint8_t* data, int len);
bool hbk_worker_decode_load_result(const uint8_t* data, int len,
                                   bool* outSuccess, const char** outName,
                                   int* outNameLen, bool* outIsInstrument,
                                   const char** outError, int* outErrorLen);
bool hbk_worker_decode_process_done(const uint8_t* data, int len,
                                    const uint8_t** outAudio, int* outAudioLen);
bool hbk_worker_decode_param_value(const uint8_t* data, int len,
                                   double* outValue);
bool hbk_worker_decode_param_count(const uint8_t* data, int len, int* outCount);
bool hbk_worker_decode_param_info(const uint8_t* data, int len, bool* outFound,
                                  uint32_t* outId, const char** outName,
                                  int* outNameLen, double* outDefaultValue);
bool hbk_worker_decode_editor_frame(const uint8_t* data, int len, int* outWidth,
                                    int* outHeight, const uint8_t** outImage,
                                    int* outImageLen);
bool hbk_worker_decode_state_result(const uint8_t* data, int len,
                                    bool* outSuccess, const uint8_t** outState,
                                    int* outStateLen);
}

namespace hibiki {

namespace {

template <typename EncodeFn>
bool encodeToBuffer(std::vector<uint8_t>& out, EncodeFn&& fn) {
  out.resize(1024);
  int n = fn(out.data(), static_cast<int>(out.size()));
  if (n < 0) {
    out.resize(static_cast<size_t>(-n));
    n = fn(out.data(), static_cast<int>(out.size()));
  }
  if (n <= 0) return false;
  out.resize(static_cast<size_t>(n));
  return true;
}

template <typename EncodeFn>
bool sendEncodedRequest(WorkerChannel* channel, EncodeFn&& fn,
                        std::string& resp_data) {
  std::vector<uint8_t> req_buf;
  if (!encodeToBuffer(req_buf, std::forward<EncodeFn>(fn))) return false;
  if (!channel->sendMessage(req_buf.data(), req_buf.size())) return false;
  return channel->recvMessage(resp_data) >= 0;
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
    uint8_t buf[64];
    int n = hbk_worker_encode_shutdown(buf, sizeof(buf));
    if (n > 0) {
      channel_->sendMessage(buf, static_cast<size_t>(n));
    }
  }

  killWorker();  // Platform-specific cleanup (posix or win32)
  channel_.reset();
}

bool PluginProxy::connectRemote() {
  auto* ch = WorkerChannelTcp::createClient(remote_host_, remote_port_, 512, 2);
  if (!ch) return false;
  channel_.reset(ch);

  std::string resp_data;
  if (!sendEncodedRequest(
          channel_.get(),
          [](uint8_t* b, int cap) {
            return hbk_worker_encode_config(512, 2, false, b, cap);
          },
          resp_data) ||
      !hbk_worker_decode_ack(reinterpret_cast<const uint8_t*>(resp_data.data()),
                             static_cast<int>(resp_data.size()))) {
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

  std::string resp_data;
  if (!sendEncodedRequest(
          channel_.get(),
          [&](uint8_t* b, int cap) {
            return hbk_worker_encode_load(path.data(),
                                          static_cast<int>(path.size()),
                                          plugin_index, sample_rate, b, cap);
          },
          resp_data)) {
    return false;
  }

  bool success = false;
  bool is_instrument = false;
  const char* name_ptr = nullptr;
  const char* err_ptr = nullptr;
  int name_len = 0;
  int err_len = 0;
  if (!hbk_worker_decode_load_result(
          reinterpret_cast<const uint8_t*>(resp_data.data()),
          static_cast<int>(resp_data.size()), &success, &name_ptr, &name_len,
          &is_instrument, &err_ptr, &err_len) ||
      !success) {
    std::string err = (err_ptr && err_len > 0)
                          ? std::string(err_ptr, static_cast<size_t>(err_len))
                          : "no response";
    LOG(ERROR) << "PluginProxy: load failed: " << err;
    return false;
  }

  name_ = (name_ptr && name_len > 0)
              ? std::string(name_ptr, static_cast<size_t>(name_len))
              : "";
  is_instrument_ = is_instrument;
  return true;
}

void PluginProxy::process(float** inputs, float** outputs, int num_samples,
                          const HostProcessContext& context,
                          const std::vector<MidiNoteEvent>& events,
                          float** /*sidechain*/) {
  if (!channel_ || !isWorkerAlive()) return;

  std::vector<HbkWorkerMidiEvent> midi_buf;
  midi_buf.reserve(events.size());
  for (const auto& e : events) {
    midi_buf.push_back(
        {e.sampleOffset, e.channel, e.pitch, e.velocity, e.isNoteOn});
  }

  std::vector<uint8_t> audio_bytes;
  if (is_remote_) {
    if (inputs) {
      audio_bytes.resize(static_cast<size_t>(2 * num_samples) * sizeof(float));
      float* dst = reinterpret_cast<float*>(audio_bytes.data());
      for (int ch = 0; ch < 2; ++ch) {
        if (inputs[ch]) {
          memcpy(dst + ch * num_samples, inputs[ch],
                 static_cast<size_t>(num_samples) * sizeof(float));
        }
      }
    }
  } else {
    if (inputs) {
      for (int ch = 0; ch < 2; ++ch) {
        float* shm_in = channel_->inputBuffer(ch);
        if (shm_in && inputs[ch]) {
          memcpy(shm_in, inputs[ch],
                 static_cast<size_t>(num_samples) * sizeof(float));
        }
      }
    }
  }

  std::string resp_data;
  if (!sendEncodedRequest(
          channel_.get(),
          [&](uint8_t* b, int cap) {
            return hbk_worker_encode_process(
                num_samples, inputs != nullptr, context.sampleRate,
                context.tempo, context.timeSigNumerator,
                context.timeSigDenominator, context.continuousTimeSamples,
                context.projectTimeMusic,
                midi_buf.empty() ? nullptr : midi_buf.data(),
                static_cast<int>(midi_buf.size()),
                audio_bytes.empty() ? nullptr : audio_bytes.data(),
                static_cast<int>(audio_bytes.size()), b, cap);
          },
          resp_data)) {
    return;
  }

  if (is_remote_) {
    const uint8_t* out_ptr = nullptr;
    int out_len = 0;
    if (outputs &&
        hbk_worker_decode_process_done(
            reinterpret_cast<const uint8_t*>(resp_data.data()),
            static_cast<int>(resp_data.size()), &out_ptr, &out_len) &&
        out_ptr && out_len > 0) {
      const float* src = reinterpret_cast<const float*>(out_ptr);
      int total =
          static_cast<int>(static_cast<size_t>(out_len) / sizeof(float));
      for (int ch = 0; ch < 2 && ch * num_samples < total; ++ch) {
        if (outputs[ch]) {
          memcpy(outputs[ch], src + ch * num_samples,
                 static_cast<size_t>(num_samples) * sizeof(float));
        }
      }
    }
  } else {
    if (outputs) {
      for (int ch = 0; ch < 2; ++ch) {
        float* shm_out = channel_->outputBuffer(ch);
        if (shm_out && outputs[ch]) {
          memcpy(outputs[ch], shm_out,
                 static_cast<size_t>(num_samples) * sizeof(float));
        }
      }
    }
  }
}

void PluginProxy::setParameterValue(uint32_t id, double valueNormalized) {
  if (!channel_ || !isWorkerAlive()) return;
  std::string resp_data;
  sendEncodedRequest(
      channel_.get(),
      [&](uint8_t* b, int cap) {
        return hbk_worker_encode_set_param(id, valueNormalized, b, cap);
      },
      resp_data);
}

double PluginProxy::getParameterValue(uint32_t id) const {
  if (!channel_ || !isWorkerAlive()) return 0.0;
  std::string resp_data;
  if (!sendEncodedRequest(
          const_cast<WorkerChannel*>(channel_.get()),
          [&](uint8_t* b, int cap) {
            return hbk_worker_encode_get_param(id, b, cap);
          },
          resp_data)) {
    return 0.0;
  }
  double val = 0.0;
  if (hbk_worker_decode_param_value(
          reinterpret_cast<const uint8_t*>(resp_data.data()),
          static_cast<int>(resp_data.size()), &val)) {
    return val;
  }
  return 0.0;
}

int PluginProxy::getParameterCount() const {
  if (!channel_ || !isWorkerAlive()) return 0;
  std::string resp_data;
  if (!sendEncodedRequest(
          const_cast<WorkerChannel*>(channel_.get()),
          [](uint8_t* b, int cap) {
            return hbk_worker_encode_get_param_count(b, cap);
          },
          resp_data)) {
    return 0;
  }
  int count = 0;
  if (hbk_worker_decode_param_count(
          reinterpret_cast<const uint8_t*>(resp_data.data()),
          static_cast<int>(resp_data.size()), &count)) {
    return count;
  }
  return 0;
}

bool PluginProxy::getParameterInfo(int index, VstParamInfo& info) const {
  if (!channel_ || !isWorkerAlive()) return false;
  std::string resp_data;
  if (!sendEncodedRequest(
          const_cast<WorkerChannel*>(channel_.get()),
          [&](uint8_t* b, int cap) {
            return hbk_worker_encode_get_param_info(index, b, cap);
          },
          resp_data)) {
    return false;
  }
  bool found = false;
  uint32_t id = 0;
  const char* name_ptr = nullptr;
  int name_len = 0;
  double default_val = 0.0;
  if (hbk_worker_decode_param_info(
          reinterpret_cast<const uint8_t*>(resp_data.data()),
          static_cast<int>(resp_data.size()), &found, &id, &name_ptr, &name_len,
          &default_val) &&
      found) {
    info.id = id;
    info.name = (name_ptr && name_len > 0)
                    ? std::string(name_ptr, static_cast<size_t>(name_len))
                    : "";
    info.defaultValue = default_val;
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
  std::string resp_data;
  sendEncodedRequest(
      channel_.get(),
      [](uint8_t* b, int cap) {
        return hbk_worker_encode_show_editor(false, b, cap);
      },
      resp_data);
}

void PluginProxy::stopEditor() {
  if (!channel_ || !isWorkerAlive()) return;
  std::string resp_data;
  sendEncodedRequest(
      channel_.get(),
      [](uint8_t* b, int cap) { return hbk_worker_encode_stop_editor(b, cap); },
      resp_data);
}

bool PluginProxy::captureEditorFrame(std::vector<uint8_t>& rgba, int& w,
                                     int& h) {
  if (!channel_ || !isWorkerAlive()) return false;
  std::string resp_data;
  if (!sendEncodedRequest(
          channel_.get(),
          [](uint8_t* b, int cap) {
            return hbk_worker_encode_get_editor_frame(b, cap);
          },
          resp_data)) {
    return false;
  }
  const uint8_t* img_ptr = nullptr;
  int img_len = 0;
  if (!hbk_worker_decode_editor_frame(
          reinterpret_cast<const uint8_t*>(resp_data.data()),
          static_cast<int>(resp_data.size()), &w, &h, &img_ptr, &img_len) ||
      !img_ptr || img_len <= 0) {
    return false;
  }
  rgba.assign(img_ptr, img_ptr + img_len);
  return !rgba.empty();
}

void PluginProxy::sendEditorInput(int type, int x, int y, int button,
                                  int key_code, int delta) {
  if (!channel_ || !isWorkerAlive()) return;
  std::string resp_data;
  sendEncodedRequest(
      channel_.get(),
      [&](uint8_t* b, int cap) {
        return hbk_worker_encode_editor_input(type, x, y, button, key_code,
                                              delta, b, cap);
      },
      resp_data);
}

bool PluginProxy::getState(std::vector<uint8_t>& state) const {
  if (!channel_ || !isWorkerAlive()) return false;
  std::string resp_data;
  if (!sendEncodedRequest(
          const_cast<WorkerChannel*>(channel_.get()),
          [](uint8_t* b, int cap) {
            return hbk_worker_encode_get_state(b, cap);
          },
          resp_data)) {
    return false;
  }
  bool success = false;
  const uint8_t* state_ptr = nullptr;
  int state_len = 0;
  if (!hbk_worker_decode_state_result(
          reinterpret_cast<const uint8_t*>(resp_data.data()),
          static_cast<int>(resp_data.size()), &success, &state_ptr,
          &state_len) ||
      !success) {
    return false;
  }
  if (state_ptr && state_len > 0) {
    state.assign(state_ptr, state_ptr + state_len);
  } else {
    state.clear();
  }
  return true;
}

bool PluginProxy::setState(const std::vector<uint8_t>& state) {
  if (!channel_ || !isWorkerAlive()) return false;
  std::string resp_data;
  if (!sendEncodedRequest(
          channel_.get(),
          [&](uint8_t* b, int cap) {
            return hbk_worker_encode_set_state(
                state.empty() ? nullptr : state.data(),
                static_cast<int>(state.size()), b, cap);
          },
          resp_data)) {
    return false;
  }
  bool success = false;
  const uint8_t* state_ptr = nullptr;
  int state_len = 0;
  return hbk_worker_decode_state_result(
             reinterpret_cast<const uint8_t*>(resp_data.data()),
             static_cast<int>(resp_data.size()), &success, &state_ptr,
             &state_len) &&
         success;
}

}  // namespace hibiki
