// Unit tests for plugin_scanner.hpp:
//   collectVst3Bundles, scanBundlesParallel, AsyncPluginCache

#include "engine/plugin/plugin_scanner.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace hibiki {
namespace {

// ─── Helpers ──────────────────────────────────────────────────────

std::string findTestdataDir() {
  const char* srcdir = getenv("TEST_SRCDIR");
  if (srcdir) {
    std::string p = std::string(srcdir) + "/_main/testdata";
    if (std::filesystem::exists(p)) return p;
  }
  if (std::filesystem::exists("testdata")) return "testdata";
  return "";
}

// Fake scan function for testing — returns a PluginDescription
// with the bundle path as the name.
std::vector<PluginDescription> fakeScanFn(const std::string& bundle_path) {
  // Extract filename from path for a deterministic name
  std::string name = bundle_path;
  auto pos = name.find_last_of('/');
  if (pos != std::string::npos) name = name.substr(pos + 1);
  return {{0, name, "TestVendor"}};
}

// ─── collectVst3Bundles ───────────────────────────────────────────

TEST(PluginScannerTest, CollectVst3BundlesFindsTestdata) {
  std::string dir = findTestdataDir();
  if (dir.empty()) GTEST_SKIP() << "testdata/ not found";

  auto bundles = collectVst3Bundles({dir});
  ASSERT_FALSE(bundles.empty()) << "Should find at least Dexed.vst3 in " << dir;

  // Verify Dexed.vst3 is among the results
  bool found_dexed = false;
  for (const auto& b : bundles) {
    if (b.find("Dexed.vst3") != std::string::npos) {
      found_dexed = true;
      break;
    }
  }
  EXPECT_TRUE(found_dexed) << "Dexed.vst3 should be found in testdata/";
}

TEST(PluginScannerTest, CollectVst3BundlesEmptyForMissingDir) {
  auto bundles = collectVst3Bundles({"/nonexistent/path/12345"});
  EXPECT_TRUE(bundles.empty());
}

TEST(PluginScannerTest, CollectVst3BundlesEmptyList) {
  auto bundles = collectVst3Bundles({});
  EXPECT_TRUE(bundles.empty());
}

TEST(PluginScannerTest, CollectVst3BundlesSkipsNonVst3) {
  std::string dir = findTestdataDir();
  if (dir.empty()) GTEST_SKIP() << "testdata/ not found";

  auto bundles = collectVst3Bundles({dir});
  for (const auto& b : bundles) {
    EXPECT_NE(b.find(".vst3"), std::string::npos)
        << "Non-.vst3 entry found: " << b;
  }
}

TEST(PluginScannerTest, CollectVst3BundlesMultipleDirs) {
  std::string dir = findTestdataDir();
  if (dir.empty()) GTEST_SKIP() << "testdata/ not found";

  // Same dir twice should find bundles from both
  auto bundles = collectVst3Bundles({dir, dir});
  // Should have at least 2x what a single scan finds
  auto single = collectVst3Bundles({dir});
  EXPECT_EQ(bundles.size(), single.size() * 2);
}

// ─── scanBundlesParallel ──────────────────────────────────────────

TEST(PluginScannerTest, ScanBundlesParallelWithFakeScan) {
  std::string dir = findTestdataDir();
  if (dir.empty()) GTEST_SKIP() << "testdata/ not found";

  auto bundles = collectVst3Bundles({dir});
  if (bundles.empty()) GTEST_SKIP() << "No .vst3 bundles found";

  std::vector<std::pair<std::string, std::vector<PluginDescription>>> results;

  scanBundlesParallel(
      bundles, fakeScanFn,
      [&results](const std::string& path,
                 const std::vector<PluginDescription>& plugins) {
        results.push_back({path, plugins});
      });

  // One callback per bundle
  EXPECT_EQ(results.size(), bundles.size());

  // Check Dexed result
  bool found_dexed = false;
  for (const auto& [path, plugins] : results) {
    if (path.find("Dexed.vst3") != std::string::npos) {
      found_dexed = true;
      ASSERT_EQ(plugins.size(), 1u);
      EXPECT_EQ(plugins[0].name, "Dexed.vst3");
      EXPECT_EQ(plugins[0].vendor, "TestVendor");
    }
  }
  EXPECT_TRUE(found_dexed);
}

TEST(PluginScannerTest, ScanBundlesParallelEmptyBundles) {
  std::vector<std::string> empty;
  int callback_count = 0;

  scanBundlesParallel(
      empty, fakeScanFn,
      [&callback_count](const std::string&,
                        const std::vector<PluginDescription>&) {
        callback_count++;
      });

  EXPECT_EQ(callback_count, 0);
}

TEST(PluginScannerTest, ScanBundlesParallelCallbackPerBundle) {
  std::string dir = findTestdataDir();
  if (dir.empty()) GTEST_SKIP() << "testdata/ not found";

  auto bundles = collectVst3Bundles({dir});
  if (bundles.empty()) GTEST_SKIP() << "No .vst3 bundles found";

  std::atomic<int> callback_count{0};

  scanBundlesParallel(
      bundles, fakeScanFn,
      [&callback_count](const std::string&,
                        const std::vector<PluginDescription>&) {
        callback_count++;
      });

  EXPECT_EQ(callback_count.load(), static_cast<int>(bundles.size()));
}

// ─── AsyncPluginCache ─────────────────────────────────────────────

TEST(PluginScannerTest, AsyncPluginCacheGetNewEntries) {
  AsyncPluginCache cache;

  // Empty cache returns empty
  auto entries = cache.getNewEntries(0);
  EXPECT_TRUE(entries.empty());

  // Add some entries
  {
    std::lock_guard<std::mutex> lock(cache.mu);
    cache.entries.push_back({"/path/a.vst3", "PluginA", 0});
    cache.entries.push_back({"/path/b.vst3", "PluginB", 0});
  }

  // Get all from 0
  entries = cache.getNewEntries(0);
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].name, "PluginA");
  EXPECT_EQ(entries[1].name, "PluginB");

  // Get from index 1 (only second entry)
  entries = cache.getNewEntries(1);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].name, "PluginB");

  // Get from index 2 (nothing new)
  entries = cache.getNewEntries(2);
  EXPECT_TRUE(entries.empty());

  // Get from beyond end
  entries = cache.getNewEntries(100);
  EXPECT_TRUE(entries.empty());
}

TEST(PluginScannerTest, AsyncPluginCacheCompleteFlag) {
  AsyncPluginCache cache;
  EXPECT_FALSE(cache.complete.load());
  cache.complete.store(true);
  EXPECT_TRUE(cache.complete.load());
}

}  // namespace
}  // namespace hibiki
