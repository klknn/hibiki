#include "midi.hpp"
#include <iostream>
#include <cassert>
#include <vector>

#include <cmath>

void test_parse_midi() {
    std::cout << "Testing parseMidi with test.mid..." << std::endl;
    auto events = hbk::parseMidi("testdata/test.mid");
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

void test_rickroll_midi() {
    std::cout << "Testing parseMidi with rickroll.mid..." << std::endl;
    auto events = hbk::parseMidi("testdata/rickroll.mid");
    assert(!events.empty());
    assert(events.size() == 2446);

    const auto& first = events.front();
    assert(first.type == 0x93);

    const auto& last = events.back();
    assert(std::abs(last.seconds - 222.545) < 0.001);
    
    std::cout << "rickroll.mid: Found " << events.size() << " events. First type=" << (int)first.type << " Last time=" << last.seconds << " - PASSED" << std::endl;
}

int main() {
    test_parse_midi();
    test_rickroll_midi();
    std::cout << "All tests passed!" << std::endl;
    return 0;
}
