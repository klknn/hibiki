#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "iplugin.hpp"

namespace hibiki {

// Collect all .vst3 bundle paths from a list of directories.
std::vector<std::string> collectVst3Bundles(
    const std::vector<std::string>& dirs);

// Scan bundles in parallel using std::async.
// `scan_fn` is called for each bundle (e.g. Vst3Plugin::listPlugins or
// listPluginsIsolated). `on_bundle` is called with results as each completes.
void scanBundlesParallel(
    const std::vector<std::string>& bundles,
    std::function<std::vector<PluginDescription>(const std::string&)> scan_fn,
    std::function<void(const std::string& path,
                       const std::vector<PluginDescription>&)> on_bundle);

// Async plugin cache — populated by a background scan thread.
// Clients can poll for new entries via getNewEntries().
struct AsyncPluginCache {
  struct Entry {
    std::string path;
    std::string name;
    int index;
  };

  std::mutex mu;
  std::vector<Entry> entries;  // grows as bundles are scanned
  std::atomic<bool> complete{false};

  // Returns entries starting from `from_index`.
  std::vector<Entry> getNewEntries(size_t from_index);
};

// Start scanning all default VST3 dirs in background.
// Each bundle is scanned in parallel; results are appended to
// cache.entries as they complete so clients can stream them.
void startPluginScan(AsyncPluginCache& cache);

}  // namespace hibiki
