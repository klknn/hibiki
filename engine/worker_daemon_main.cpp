// hbk-worker-daemon: Remote plugin worker service.
//
// Usage: hbk-worker-daemon [--port PORT]
//
// Listens on a TCP port (default 9100) and accepts connections from
// remote PluginProxy instances. For each connection, spawns a local
// hbk-plugin-worker process and relays messages between the TCP
// connection and the worker's Unix socket.
//
// This enables cross-OS plugin hosting: run this daemon on a Mac
// to serve macOS-only plugins to a Linux host, or vice versa.

#ifndef _WIN32
#include <signal.h>
#endif

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "engine/ipc/tcp.hpp"
#include "engine/plugin/plugin_scanner.hpp"
#include "engine/vst3/vst3_host.hpp"
#include "pb/plugin_worker.pb.h"

namespace hibiki {

static constexpr int DEFAULT_PORT = 9100;

// Handle a single client connection — run a Vst3Plugin directly in-process
// (simpler than proxying to a separate hbk-plugin-worker).
void handleClient(socket_t conn_fd, AsyncPluginCache& plugin_cache) {
  // Wrap in a WorkerChannelTcp for message framing
  // We'll use raw send/recv on conn_fd since we already accepted.
  auto sendMsg = [&](const void* data, size_t len) -> bool {
    uint32_t size = static_cast<uint32_t>(len);
    const uint8_t* p;
    size_t remaining;

    p = reinterpret_cast<const uint8_t*>(&size);
    remaining = sizeof(size);
    while (remaining > 0) {
      int n = tcp_send(conn_fd, p, remaining);
      if (n <= 0) return false;
      p += n;
      remaining -= n;
    }

    p = reinterpret_cast<const uint8_t*>(data);
    remaining = len;
    while (remaining > 0) {
      int n = tcp_send(conn_fd, p, remaining);
      if (n <= 0) return false;
      p += n;
      remaining -= n;
    }
    return true;
  };

  auto recvMsg = [&](std::string& out) -> int {
    uint32_t size = 0;
    uint8_t* p = reinterpret_cast<uint8_t*>(&size);
    size_t remaining = sizeof(size);
    while (remaining > 0) {
      int n = tcp_recv(conn_fd, p, remaining);
      if (n <= 0) return -1;
      p += n;
      remaining -= n;
    }
    if (size > 4 * 1024 * 1024) return -1;
    out.resize(size);
    p = reinterpret_cast<uint8_t*>(out.data());
    remaining = size;
    while (remaining > 0) {
      int n = tcp_recv(conn_fd, p, remaining);
      if (n <= 0) return -1;
      p += n;
      remaining -= n;
    }
    return (int)size;
  };

  std::unique_ptr<Vst3Plugin> plugin;
  int block_size = 512;
  int num_channels = 2;
  std::vector<std::vector<float>> input_bufs;
  std::vector<std::vector<float>> output_bufs;

  while (true) {
    std::string msg_data;
    if (recvMsg(msg_data) < 0) break;

    hibiki::pb::worker::WorkerRequest req;
    if (!req.ParseFromString(msg_data)) continue;

    hibiki::pb::worker::WorkerResponse resp;

    switch (req.command_case()) {
      case hibiki::pb::worker::WorkerRequest::kConfig: {
        block_size = req.config().block_size();
        num_channels = req.config().num_channels();
        input_bufs.resize(num_channels, std::vector<float>(block_size, 0.0f));
        output_bufs.resize(num_channels, std::vector<float>(block_size, 0.0f));
        resp.mutable_config_ack();
        break;
      }

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
        int num_samples = std::min(cmd.num_samples(), block_size);

        // Deserialize input audio from proto
        if (cmd.has_inputs() && !cmd.input_audio().empty()) {
          const float* src =
              reinterpret_cast<const float*>(cmd.input_audio().data());
          int total = (int)(cmd.input_audio().size() / sizeof(float));
          for (int ch = 0; ch < num_channels && ch * num_samples < total;
               ++ch) {
            memcpy(input_bufs[ch].data(), src + ch * num_samples,
                   num_samples * sizeof(float));
          }
        }

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

        float* inputs[2] = {input_bufs[0].data(), input_bufs[1].data()};
        float* outputs[2] = {output_bufs[0].data(), output_bufs[1].data()};

        // Clear output
        for (int ch = 0; ch < num_channels; ++ch) {
          memset(output_bufs[ch].data(), 0, num_samples * sizeof(float));
        }

        plugin->process(cmd.has_inputs() ? inputs : nullptr, outputs,
                        num_samples, ctx, events);

        // Serialize output audio into response
        auto* done = resp.mutable_process_done();
        std::string out_audio;
        out_audio.resize(num_channels * num_samples * sizeof(float));
        float* dst = reinterpret_cast<float*>(out_audio.data());
        for (int ch = 0; ch < num_channels; ++ch) {
          memcpy(dst + ch * num_samples, output_bufs[ch].data(),
                 num_samples * sizeof(float));
        }
        done->set_output_audio(std::move(out_audio));
        break;
      }

      case hibiki::pb::worker::WorkerRequest::kSetParam: {
        if (plugin) {
          plugin->setParameterValue(req.set_param().param_id(),
                                    req.set_param().value());
        }
        resp.mutable_process_done();
        break;
      }

      case hibiki::pb::worker::WorkerRequest::kGetParam: {
        auto* val = resp.mutable_param_value();
        if (plugin)
          val->set_value(plugin->getParameterValue(req.get_param().param_id()));
        break;
      }

      case hibiki::pb::worker::WorkerRequest::kGetParamCount: {
        resp.mutable_param_count()->set_count(
            plugin ? plugin->getParameterCount() : 0);
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

      case hibiki::pb::worker::WorkerRequest::kListPlugins: {
        // Stream results live from the async cache.
        // Poll for new entries and send them as they become available.
        size_t sent_index = 0;
        bool send_failed = false;

        while (!send_failed) {
          auto new_entries = plugin_cache.getNewEntries(sent_index);

          if (!new_entries.empty()) {
            pb::worker::WorkerResponse chunk;
            auto* result = chunk.mutable_list_plugins_result();
            for (const auto& e : new_entries) {
              auto* pi = result->add_plugins();
              pi->set_name(e.name);
              pi->set_path(e.path);
              pi->set_plugin_index(e.index);
            }
            sent_index += new_entries.size();
            std::string data;
            chunk.SerializeToString(&data);
            if (!sendMsg(data.data(), data.size())) {
              send_failed = true;
              break;
            }
          }

          if (plugin_cache.complete.load()) {
            // Drain any remaining entries
            auto final_entries = plugin_cache.getNewEntries(sent_index);
            if (!final_entries.empty()) {
              pb::worker::WorkerResponse chunk;
              auto* result = chunk.mutable_list_plugins_result();
              for (const auto& e : final_entries) {
                auto* pi = result->add_plugins();
                pi->set_name(e.name);
                pi->set_path(e.path);
                pi->set_plugin_index(e.index);
              }
              std::string data;
              chunk.SerializeToString(&data);
              if (!sendMsg(data.data(), data.size())) {
                send_failed = true;
                break;
              }
            }
            // Send final is_complete=true
            pb::worker::WorkerResponse final_resp;
            final_resp.mutable_list_plugins_result()->set_is_complete(true);
            std::string data;
            final_resp.SerializeToString(&data);
            if (!sendMsg(data.data(), data.size())) send_failed = true;
            break;
          }

          // Wait briefly for more results
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (send_failed) break;
        continue;
      }

      case hibiki::pb::worker::WorkerRequest::kShutdown: {
        std::string resp_data;
        resp.mutable_process_done();
        resp.SerializeToString(&resp_data);
        sendMsg(resp_data.data(), resp_data.size());
        tcp_close(conn_fd);
        return;
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

    std::string resp_data;
    resp.SerializeToString(&resp_data);
    if (!sendMsg(resp_data.data(), resp_data.size())) break;
  }

  tcp_close(conn_fd);
}

}  // namespace hibiki

int main(int argc, char** argv) {
  using namespace hibiki;
  int port = DEFAULT_PORT;

  // Parse --port flag
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--port" && i + 1 < argc) {
      port = std::atoi(argv[++i]);
    }
  }

  tcp_init();

  // Create listening socket
  socket_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd == INVALID_SOCK) {
    std::cerr << "Failed to create socket: " << tcp_strerror() << "\n";
    return 1;
  }

  tcp_setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, 1);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    std::cerr << "Failed to bind port " << port << ": " << tcp_strerror()
              << "\n";
    tcp_close(listen_fd);
    return 1;
  }

  if (listen(listen_fd, 8) < 0) {
    std::cerr << "Failed to listen: " << tcp_strerror() << "\n";
    tcp_close(listen_fd);
    return 1;
  }

  std::cerr << "hbk-worker-daemon listening on port " << port << "\n";

  // Start async plugin scan (non-blocking — results stream as they arrive)
  AsyncPluginCache plugin_cache;
  startPluginScan(plugin_cache);

  // Ignore SIGPIPE
#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);
#endif

  while (true) {
    socket_t conn_fd = ::accept(listen_fd, nullptr, nullptr);
    if (conn_fd == INVALID_SOCK) {
      // Note: On Windows WSAEINTR corresponds to EINTR. For simplicity we can
      // just log all failures.
      std::cerr << "accept() failed: " << tcp_strerror() << "\n";
      continue;
    }

    // Disable Nagle's algorithm
    tcp_setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY, 1);

    std::cerr << "Accepted connection (fd=" << conn_fd << ")\n";

    // Handle each client in a detached thread
    std::thread(handleClient, conn_fd, std::ref(plugin_cache)).detach();
  }

  tcp_close(listen_fd);
  return 0;
}
