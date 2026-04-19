#include "engine/plugin/plugin_scanner.hpp"

#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>

#include "absl/log/log.h"
#include "engine/vst3/vst3_host.hpp"

namespace hibiki {

std::vector<std::string> collectVst3Bundles(
    const std::vector<std::string>& dirs) {
  LOG(INFO) << "Collecting VST3 bundles from " << dirs.size() << " directories";
  std::vector<std::string> bundles;
  for (const auto& dir : dirs) {
    LOG(INFO) << "Scanning directory: " << dir;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
      LOG(WARNING) << "Not a directory: " << dir << " (error: " << ec.message()
                   << ")";
      continue;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
      std::string path = entry.path().string();
      std::string name = entry.path().filename().string();
      if (name.size() > 5 && name.substr(name.size() - 5) == ".vst3") {
        LOG(INFO) << "Found bundle: " << path;
        bundles.push_back(path);
      } else {
        VLOG(1) << "Skipping non-vst3 file/directory: " << path;
      }
    }
  }
  LOG(INFO) << "Total bundles found: " << bundles.size();
  return bundles;
}

void scanBundlesParallel(
    const std::vector<std::string>& bundles,
    std::function<std::vector<PluginDescription>(const std::string&)> scan_fn,
    std::function<void(const std::string& path,
                       const std::vector<PluginDescription>&)>
        on_bundle) {
  struct BundleResult {
    std::string path;
    std::vector<PluginDescription> plugins;
  };

  std::vector<std::future<BundleResult>> futures;
  for (const auto& bp : bundles) {
    futures.push_back(std::async(
        std::launch::async,
        [bp, &scan_fn]() -> BundleResult { return {bp, scan_fn(bp)}; }));
  }

  for (auto& f : futures) {
    auto br = f.get();
    on_bundle(br.path, br.plugins);
  }
}

std::vector<AsyncPluginCache::Entry> AsyncPluginCache::getNewEntries(
    size_t from_index) {
  std::lock_guard<std::mutex> lock(mu);
  if (from_index >= entries.size()) return {};
  return {entries.begin() + from_index, entries.end()};
}

void startPluginScan(AsyncPluginCache& cache) {
  std::thread([&cache]() {
    auto t0 = std::chrono::steady_clock::now();

    auto dirs = Vst3Plugin::getDefaultVst3Dirs();
    auto bundles = collectVst3Bundles(dirs);

    scanBundlesParallel(
        bundles,
        [](const std::string& bp) { return Vst3Plugin::listPlugins(bp); },
        [&cache](const std::string& path,
                 const std::vector<PluginDescription>& plugins) {
          std::lock_guard<std::mutex> lock(cache.mu);
          for (const auto& pd : plugins) {
            cache.entries.push_back({path, pd.name, pd.index});
          }
        });

    cache.complete.store(true);

    auto t1 = std::chrono::steady_clock::now();
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::lock_guard<std::mutex> lock(cache.mu);
    LOG(INFO) << "Plugin cache: " << cache.entries.size() << " plugins in "
              << ms << " ms (" << bundles.size() << " bundles)";
  }).detach();
}

}  // namespace hibiki
