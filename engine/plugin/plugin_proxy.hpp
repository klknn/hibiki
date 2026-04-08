#pragma once

#include <memory>
#include <string>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

class WorkerChannel;

// Plugin hosting mode — determines how local plugins are hosted.
enum class PluginHostMode {
  IN_PROCESS,     // Use Vst3Plugin directly (no worker)
  LOCAL_SANDBOX,  // Fork hbk-plugin-worker, Unix socket + shared memory
};

// Out-of-process IPlugin implementation.
// Supports local (Unix socket + shm) and remote (TCP) worker modes.
class PluginProxy : public IPlugin {
 public:
  // Create a proxy for local sandbox mode (default).
  PluginProxy();

  // Create a proxy for remote mode (TCP to host:port).
  PluginProxy(const std::string& remote_host, int remote_port);

  ~PluginProxy() override;

  bool load(const std::string& path, int plugin_index = 0,
            double sample_rate = 44100.0) override;
  void showEditor() override;
  void stopEditor() override;
  void process(float** inputs, float** outputs, int num_samples,
               const HostProcessContext& context,
               const std::vector<MidiNoteEvent>& events) override;

  int getParameterCount() const override;
  bool getParameterInfo(int index, VstParamInfo& info) const override;
  void setParameterValue(uint32_t id, double valueNormalized) override;
  double getParameterValue(uint32_t id) const override;
  const std::string& getName() const override;
  const std::string& getPath() const override;
  int getPluginIndex() const override;
  bool isInstrument() const override;
  bool captureEditorFrame(std::vector<uint8_t>& rgba, int& w, int& h) override;
  void sendEditorInput(int type, int x, int y, int button, int key_code,
                       int delta) override;

  bool isRemote() const { return is_remote_; }

 private:
  bool spawnLocalWorker();
  bool connectRemote();
  bool isWorkerAlive() const;
  void killWorker();  // Platform-specific worker cleanup

  std::unique_ptr<WorkerChannel> channel_;
  int worker_pid_ = -1;            // POSIX: pid_t (fork result)
  void* worker_handle_ = nullptr;  // Windows: HANDLE (CreateProcess result)
  bool is_remote_ = false;

  // Local mode
  std::string socket_path_;
  std::string shm_name_;

  // Remote mode
  std::string remote_host_;
  int remote_port_ = 9100;

  // Cached plugin info
  std::string name_;
  std::string path_;
  int plugin_index_ = 0;
  double sample_rate_ = 44100.0;
  bool is_instrument_ = false;
};

}  // namespace hibiki
