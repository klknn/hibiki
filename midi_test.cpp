#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "midi.hpp"

// No debug prints in final version

std::string find_test_file(const std::string& path) {
    if (std::ifstream(path).good()) return path;

    // Search up from current directory to find testdata
    std::string dir = ".";
    for (int i = 0; i < 10; ++i) {
        std::string p = dir + "/" + path;
        if (std::ifstream(p).good()) return p;

        // Try in runfiles if it exists
        std::string pr = dir + "/midi_test.exe.runfiles/_main/" + path;
        if (std::ifstream(pr).good()) return pr;
        std::string pr2 = dir + "/midi_test.exe.runfiles/hibiki/" + path;
        if (std::ifstream(pr2).good()) return pr2;

        dir += "/..";
    }

    return path;
}

//void test_parse_midi() {
TEST_CASE( "Parse test.mid", "[midi]" ) {

    std::cout << "Testing parseMidi with test.mid..." << std::endl;
    auto events = hbk::parseMidi(find_test_file("testdata/test.mid"));
    assert(!events.empty());
    assert(events.size() == 894);

    // Check first event
    const auto& first = events.front();
    assert(first.seconds == 0.0);
    assert(first.type == 0x91); // Note on

    // Check last event
    const auto& last = events.back();
    assert(std::abs(last.seconds - 154.286) < 0.001);

    std::cout << "test.mid: Found " << events.size() << " events. First type=" << (int)first.type << " Last time=" << last.seconds << " - PASSED" << std::endl;
}

// void test_rickroll_midi() {
TEST_CASE("Parse rickroll.mid", "[midi]") {
    std::cout << "Testing parseMidi with rickroll.mid..." << std::endl;
    auto events = hbk::parseMidi(find_test_file("testdata/rickroll.mid"));
    assert(!events.empty());
    assert(events.size() == 2446);

    const auto& first = events.front();
    assert(first.type == 0x93);

    const auto& last = events.back();
    assert(std::abs(last.seconds - 222.545) < 0.001);

    std::cout << "rickroll.mid: Found " << events.size() << " events. First type=" << (int)first.type << " Last time=" << last.seconds << " - PASSED" << std::endl;
}

