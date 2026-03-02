#include <gtest/gtest.h>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "midi.hpp"

// No debug prints in final version

#include "test_utils.hpp"

TEST(MidiTest, ParseTestMid) {
    std::cout << "Testing parseMidi with test.mid..." << std::endl;
    auto events = hibiki::parseMidi(hibiki::find_test_file("testdata/test.mid"));
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.size(), 894u);

    // Check first event
    const auto& first = events.front();
    EXPECT_DOUBLE_EQ(first.seconds, 0.0);
    EXPECT_EQ(first.type, 0x91); // Note on

    // Check last event
    const auto& last = events.back();
    EXPECT_NEAR(last.seconds, 154.286, 0.001);

    std::cout << "test.mid: Found " << events.size() << " events. First type=" << (int)first.type << " Last time=" << last.seconds << " - PASSED" << std::endl;
}

TEST(MidiTest, ParseRickrollMid) {
    std::cout << "Testing parseMidi with rickroll.mid..." << std::endl;
    auto events = hibiki::parseMidi(hibiki::find_test_file("testdata/rickroll.mid"));
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.size(), 2446u);

    const auto& first = events.front();
    EXPECT_EQ(first.type, 0x93);

    const auto& last = events.back();
    EXPECT_NEAR(last.seconds, 222.545, 0.001);

    std::cout << "rickroll.mid: Found " << events.size() << " events. First type=" << (int)first.type << " Last time=" << last.seconds << " - PASSED" << std::endl;
}
