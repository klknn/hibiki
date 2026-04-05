#include "midi.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// No debug prints in final version

#include "test_utils.hpp"


namespace hibiki {

TEST(MidiTest, ParseTestMid) {
  std::cout << "Testing parseMidi with test.mid..." << std::endl;
  auto events = hibiki::parseMidi(hibiki::find_test_file("testdata/test.mid"));
  ASSERT_FALSE(events.empty());
  EXPECT_EQ(events.size(), 894u);

  // Check first event
  const auto& first = events.front();
  EXPECT_DOUBLE_EQ(first.beats,
                   0.0);        // .beats is actually beats (quarter notes)
  EXPECT_EQ(first.type, 0x91);  // Note on

  // Check last event (time is in beats, not seconds)
  const auto& last = events.back();
  EXPECT_NEAR(last.beats, 234.0, 0.01);  // 234 beats

  std::cout << "test.mid: Found " << events.size()
            << " events. First type=" << (int)first.type
            << " Last beat=" << last.beats << " - PASSED" << std::endl;
}

TEST(MidiTest, ParseRickrollMid) {
  std::cout << "Testing parseMidi with rickroll.mid..." << std::endl;
  auto events =
      hibiki::parseMidi(hibiki::find_test_file("testdata/rickroll.mid"));
  ASSERT_FALSE(events.empty());
  EXPECT_EQ(events.size(), 2446u);

  const auto& first = events.front();
  EXPECT_EQ(first.type, 0x93);

  // Last event time is in beats (quarter notes), not seconds
  const auto& last = events.back();
  EXPECT_NEAR(last.beats, 408.0, 0.01);  // 408 beats

  std::cout << "rickroll.mid: Found " << events.size()
            << " events. First type=" << (int)first.type
            << " Last beat=" << last.beats << " - PASSED" << std::endl;
}

}  // namespace hibiki
