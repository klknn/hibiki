#pragma once

#include <memory>
#include <string>
#include <sys/types.h>

#include "iplugin.hpp"

class WorkerChannelUnix;

// Out-of-process IPlugin implementation.
// Spawns hbk-plugin-worker, communicates via WorkerChannel.
class PluginProxy : public IPlugin {
 public:
  PluginProxy();
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

 private:
  bool spawnWorker();
  bool isWorkerAlive() const;

  std::unique_ptr<WorkerChannelUnix> channel_;
  pid_t worker_pid_ = -1;
  std::string socket_path_;
  std::string shm_name_;

  // Cached plugin info from LoadResult
  std::string name_;
  std::string path_;
  int plugin_index_ = 0;
  double sample_rate_ = 44100.0;
  bool is_instrument_ = false;
};
