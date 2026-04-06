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

#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "pb/plugin_worker.pb.h"
#include "tcp.hpp"
#include "vst3_host.hpp"

namespace hibiki {

static constexpr int DEFAULT_PORT = 9100;

// Handle a single client connection — run a Vst3Plugin directly in-process
// (simpler than proxying to a separate hbk-plugin-worker).
void handleClient(socket_t conn_fd) {
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
        auto& cmd = req.list_plugins();
        auto* result = resp.mutable_list_plugins_result();

        // Determine which directories to scan
        std::vector<std::string> scan_dirs;
        std::string sp = cmd.search_path();
        if (sp.empty() || sp == "/") {
          // Use platform-appropriate defaults
          scan_dirs = Vst3Plugin::getDefaultVst3Dirs();
        } else {
          scan_dirs.push_back(sp);
        }

        // Iterate each directory, find .vst3 bundles, list their plugins
        for (const auto& dir : scan_dirs) {
          std::error_code ec;
          if (!std::filesystem::is_directory(dir, ec)) continue;
          for (const auto& entry :
               std::filesystem::directory_iterator(dir, ec)) {
            std::string name = entry.path().filename().string();
            if (name.size() > 5 && name.substr(name.size() - 5) == ".vst3") {
              std::string bundle_path = entry.path().string();
              auto plugins = Vst3Plugin::listPlugins(bundle_path);
              for (const auto& pd : plugins) {
                auto* pi = result->add_plugins();
                pi->set_name(pd.name);
                pi->set_path(bundle_path);
                pi->set_plugin_index(pd.index);
              }
            }
          }
        }
        break;
      }

      case hibiki::pb::worker::WorkerRequest::kShutdown: {
        std::string resp_data;
        resp.mutable_process_done();
        resp.SerializeToString(&resp_data);
        sendMsg(resp_data.data(), resp_data.size());
        tcp_close(conn_fd);
        return;
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

  // Ignore SIGPIPE
#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);
#endif

  while (true) {
    socket_t conn_fd = ::accept(listen_fd, nullptr, nullptr);
    if (conn_fd == INVALID_SOCK) {
      // Note: On Windows WSAEINTR corresponds to EINTR. For simplicity we can just log all failures.
      std::cerr << "accept() failed: " << tcp_strerror() << "\n";
      continue;
    }

    // Disable Nagle's algorithm
    tcp_setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY, 1);

    std::cerr << "Accepted connection (fd=" << conn_fd << ")\n";

    // Handle each client in a detached thread
    std::thread(handleClient, conn_fd).detach();
  }

  tcp_close(listen_fd);
  return 0;
}
