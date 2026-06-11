#include "engine/vst3/vst3_host.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "engine/test_utils.hpp"

namespace hibiki {
namespace {

std::string GetDexedPath() {
  std::string path = "testdata/Dexed.vst3";
#ifdef _WIN32
  std::string win_path = "testdata/Dexed.vst3/Contents/x86_64-win/Dexed.vst3";
  std::string resolved = hibiki::find_test_file(win_path);
  if (resolved != win_path) return resolved;
#endif
  return hibiki::find_test_file(path);
}

TEST(Vst3HostTest, TestSaveLoadState) {
  auto plugin = std::make_unique<Vst3Plugin>();

  // 1. getState / setState should fail when plugin is not loaded
  std::vector<uint8_t> temp_state;
  EXPECT_FALSE(plugin->getState(temp_state));
  EXPECT_FALSE(plugin->setState({1, 2, 3}));

  // 2. Load the plugin
  ASSERT_TRUE(plugin->load(GetDexedPath(), 0, 44100.0));

  // 3. setState should fail with invalid/empty state data
  EXPECT_FALSE(plugin->setState({}));
  EXPECT_FALSE(plugin->setState({0, 0, 0}));  // too short

  // 4. getState should succeed and return non-empty state data
  std::vector<uint8_t> initial_state;
  ASSERT_TRUE(plugin->getState(initial_state)) << "getState failed";
  ASSERT_FALSE(initial_state.empty()) << "getState returned empty state";

  // 5. Get a valid parameter to test state modification
  int param_count = plugin->getParameterCount();
  ASSERT_GT(param_count, 0) << "No parameters found in the plugin";

  VstParamInfo param_info;
  ASSERT_TRUE(plugin->getParameterInfo(0, param_info))
      << "Failed to get parameter info for first param";
  uint32_t param_id = param_info.id;

  double original_val = plugin->getParameterValue(param_id);

  // 6. Modify the parameter
  double target_val = (original_val > 0.5) ? 0.1 : 0.9;
  plugin->setParameterValue(param_id, target_val);
  EXPECT_DOUBLE_EQ(plugin->getParameterValue(param_id), target_val);

  // 7. Restore the initial state and verify the parameter is reverted
  ASSERT_TRUE(plugin->setState(initial_state)) << "setState failed";
  EXPECT_DOUBLE_EQ(plugin->getParameterValue(param_id), original_val)
      << "State restore did not revert parameter value to original";
}

}  // namespace
}  // namespace hibiki
