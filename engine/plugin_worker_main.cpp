// hbk-plugin-worker: Out-of-process VST3 plugin host.
//
// Usage: hbk-plugin-worker <socket_path> <shm_name>
//
// This binary is spawned by PluginProxy in the host process.
// It connects to the Unix domain socket, opens the shared memory,
// and enters a command loop receiving WorkerRequests and sending
// WorkerResponses.

#ifndef _WIN32
#include <signal.h>
#include <sys/prctl.h>
#endif

#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "pb/plugin_worker.pb.h"
#include "engine/vst3/vst3_host.hpp"
#include "engine/ipc/worker_channel_local.hpp"

namespace hibiki {}  // namespace hibiki

int main(int argc, char** argv) {
  using namespace hibiki;
  if (argc < 3) {
    std::cerr << "Usage: hbk-plugin-worker <socket_path> <shm_name>\n";
    return 1;
  }

  // Die when parent dies (Linux only)
#ifdef __linux__
  prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif

  std::string socket_path = argv[1];
  std::string shm_name = argv[2];

  // Connect to host
  auto* channel = WorkerChannelLocal::createClient(socket_path, shm_name);
  if (!channel) {
    std::cerr << "Worker: failed to connect to host\n";
    return 1;
  }

  std::unique_ptr<Vst3Plugin> plugin;

  // Command loop
  while (true) {
    std::string msg_data;
    if (channel->recvMessage(msg_data) < 0) {
      std::cerr << "Worker: recv failed, exiting\n";
      break;
    }

    hibiki::pb::worker::WorkerRequest req;
    if (!req.ParseFromString(msg_data)) {
      std::cerr << "Worker: failed to parse request\n";
      continue;
    }

    hibiki::pb::worker::WorkerResponse resp;

    switch (req.command_case()) {
      case hibiki::pb::worker::WorkerRequest::kLoad: {
        auto& cmd = req.load();
        plugin = std::make_unique<Vst3Plugin>();
        auto* result = resp.mutable_load_result();
        if (plugin->load(cmd.path(), cmd.plugin_index(), cmd.sample_rate())) {
          result->set_success(true);
          result->set_name(plugin->getName());
          result->set_is_instrument(plugin->isInstrument());
        } else {
          result->set_success(false);
          result->set_error("Failed to load plugin");
          plugin.reset();
        }
        break;
      }

      case hibiki::pb::worker::WorkerRequest::kProcess: {
        if (!plugin) break;
        auto& cmd = req.process();
        int num_samples = cmd.num_samples();
        int block_size = channel->blockSize();
        if (num_samples > block_size) num_samples = block_size;

        // Build process context
        HostProcessContext ctx;
        ctx.sampleRate = cmd.sample_rate();
        ctx.tempo = cmd.tempo();
        ctx.timeSigNumerator = cmd.time_sig_numerator();
        ctx.timeSigDenominator = cmd.time_sig_denominator();
        ctx.continuousTimeSamples = cmd.continuous_time_samples();
        ctx.projectTimeMusic = cmd.project_time_music();

        // Build MIDI events
        std::vector<MidiNoteEvent> events;
        events.reserve(cmd.midi_events_size());
        for (const auto& me : cmd.midi_events()) {
          MidiNoteEvent e;
          e.sampleOffset = me.sample_offset();
          e.channel = me.channel();
          e.pitch = me.pitch();
          e.velocity = me.velocity();
          e.isNoteOn = me.is_note_on();
          events.push_back(e);
        }

        // Set up audio buffer pointers from shared memory
        float* inL = channel->inputBuffer(0);
        float* inR = channel->inputBuffer(1);
        float* outL = channel->outputBuffer(0);
        float* outR = channel->outputBuffer(1);

        float* inputs[] = {inL, inR};
        float* outputs[] = {outL, outR};

        plugin->process(cmd.has_inputs() ? inputs : nullptr, outputs,
                        num_samples, ctx, events);

        resp.mutable_process_done();
        break;
      }

      case hibiki::pb::worker::WorkerRequest::kSetParam: {
        if (!plugin) break;
        plugin->setParameterValue(req.set_param().param_id(),
                                  req.set_param().value());
        // SetParam is fire-and-forget, but we still send a response
        // to keep the protocol synchronous.
        resp.mutable_process_done();
        break;
      }

      case hibiki::pb::worker::WorkerRequest::kGetParam: {
        auto* val = resp.mutable_param_value();
        if (plugin) {
          val->set_value(plugin->getParameterValue(req.get_param().param_id()));
        }
        break;
      }

      case hibiki::pb::worker::WorkerRequest::kGetParamCount: {
        auto* count = resp.mutable_param_count();
        count->set_count(plugin ? plugin->getParameterCount() : 0);
        break;
      }

      case hibiki::pb::worker::WorkerRequest::kGetParamInfo: {
        auto* info = resp.mutable_param_info();
        if (plugin) {
          VstParamInfo pi;
          if (plugin->getParameterInfo(req.get_param_info().index(), pi)) {
            info->set_found(true);
            info->set_id(pi.id);
            info->set_name(pi.name);
            info->set_default_value(pi.defaultValue);
          } else {
            info->set_found(false);
          }
        } else {
          info->set_found(false);
        }
        break;
      }

      case hibiki::pb::worker::WorkerRequest::kGetPluginInfo: {
        auto* pi = resp.mutable_plugin_info();
        if (plugin) {
          pi->set_name(plugin->getName());
          pi->set_path(plugin->getPath());
          pi->set_plugin_index(plugin->getPluginIndex());
          pi->set_is_instrument(plugin->isInstrument());
        }
        break;
      }

      case hibiki::pb::worker::WorkerRequest::kShutdown: {
        // Send response then exit
        std::string resp_data;
        resp.mutable_process_done();
        resp.SerializeToString(&resp_data);
        channel->sendMessage(resp_data.data(), resp_data.size());
        delete channel;
        return 0;
      }

      case hibiki::pb::worker::WorkerRequest::kShowEditor: {
        if (plugin) plugin->showEditor();
        resp.mutable_editor_result()->set_success(plugin != nullptr);
        break;
      }

      case hibiki::pb::worker::WorkerRequest::kStopEditor: {
        if (plugin) plugin->stopEditor();
        resp.mutable_editor_result()->set_success(true);
        break;
      }

      case hibiki::pb::worker::WorkerRequest::kGetEditorFrame: {
        auto* frame = resp.mutable_editor_frame();
        if (plugin) {
          std::vector<uint8_t> rgba;
          int w = 0, h = 0;
          if (plugin->captureEditorFrame(rgba, w, h)) {
            frame->set_width(w);
            frame->set_height(h);
            frame->set_image_data(rgba.data(), rgba.size());
          }
        }
        break;
      }

      case hibiki::pb::worker::WorkerRequest::kEditorInput: {
        if (plugin) {
          auto& inp = req.editor_input();
          plugin->sendEditorInput(static_cast<int>(inp.type()), inp.x(),
                                  inp.y(), inp.button(), inp.key_code(),
                                  inp.delta());
        }
        resp.mutable_editor_result()->set_success(true);
        break;
      }

      default:
        break;
    }

    // Send response
    std::string resp_data;
    resp.SerializeToString(&resp_data);
    if (!channel->sendMessage(resp_data.data(), resp_data.size())) {
      std::cerr << "Worker: send failed, exiting\n";
      break;
    }
  }

  delete channel;
  return 0;
}
