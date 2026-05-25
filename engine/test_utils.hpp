#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace hibiki {

inline std::string find_test_file(const std::string& path) {
  if (std::filesystem::exists(path)) return path;

  // Check if bazel TEST_SRCDIR is set
  if (const char* srcdir = std::getenv("TEST_SRCDIR")) {
    std::string p = std::string(srcdir) + "/hibiki/" + path;
    if (std::filesystem::exists(p)) return p;
  }

  // Search up to find testdata
  std::string dir = ".";
  for (int i = 0; i < 10; ++i) {
    std::string p = dir + "/" + path;
    if (std::filesystem::exists(p)) return p;

    std::string pr = dir + "/midi_test.exe.runfiles/_main/" + path;
    if (std::filesystem::exists(pr)) return pr;
    std::string pr2 = dir + "/midi_test.exe.runfiles/hibiki/" + path;
    if (std::filesystem::exists(pr2)) return pr2;

    dir = dir + "/..";
  }

  return path;
}

}  // namespace hibiki
