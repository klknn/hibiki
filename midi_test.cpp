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

static std::string g_argv0;

// No debug prints in final version

std::string find_test_file(const std::string& path) {
    if (std::ifstream(path).good()) return path;

    // Search up from argv[0] to find testdata
    size_t last_slash = g_argv0.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        std::string dir = g_argv0.substr(0, last_slash);
        for (int i = 0; i < 10; ++i) {
            std::string p = dir + "/" + path;
            if (std::ifstream(p).good()) return p;

            // Try in runfiles if it exists
            std::string pr = dir + "/midi_test.exe.runfiles/_main/" + path;
            if (std::ifstream(pr).good()) return pr;
            std::string pr2 = dir + "/midi_test.exe.runfiles/hibiki/" + path;
            if (std::ifstream(pr2).good()) return pr2;

            size_t next_slash = dir.find_last_of("/\\");
            if (next_slash == std::string::npos) break;
            dir = dir.substr(0, next_slash);
        }
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

int main(int argc, char** argv) {
    g_argv0 = argv[0];
    //test_parse_midi();
    //test_rickroll_midi();
    int result = Catch::Session().run( argc, argv );

    std::cout << "All tests passed!" << std::endl;
    return 0;
}

