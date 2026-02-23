#include "mock_audio.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <memory>

void test_mock_playback_and_plugin() {
    std::cout << "Testing MockPlayback and MockPlugin interaction..." << std::endl;

    auto mock_playback = std::make_unique<MockPlayback>();
    auto mock_plugin = std::make_unique<MockPlugin>();

    HostProcessContext context;
    context.sampleRate = 44100.0;
    context.tempo = 120.0;
    context.continuousTimeSamples = 0;
    context.projectTimeMusic = 0;

    int block_size = 512;
    float bufferL[512];
    float bufferR[512];
    float* outChannels[] = {bufferL, bufferR};

    // 1. Process without note - should be silent
    mock_plugin->process(nullptr, outChannels, block_size, context, {});
    
    for (int i = 0; i < block_size; ++i) {
        assert(bufferL[i] == 0.0f);
        assert(bufferR[i] == 0.0f);
    }
    std::cout << "  - Silent without MIDI: PASSED" << std::endl;

    // 2. Process with note-on
    std::vector<MidiNoteEvent> events;
    MidiNoteEvent e;
    e.sampleOffset = 0;
    e.channel = 0;
    e.pitch = 60; // middle C
    e.velocity = 0.8f;
    e.isNoteOn = true;
    events.push_back(e);

    mock_plugin->process(nullptr, outChannels, block_size, context, events);

    bool has_audio = false;
    for (int i = 0; i < block_size; ++i) {
        if (std::abs(bufferL[i]) > 0.0f) {
            has_audio = true;
            break;
        }
    }
    assert(has_audio);
    std::cout << "  - Audio generated with MIDI note-on: PASSED" << std::endl;

    // 3. Write to mock playback
    std::vector<float> interleaved(block_size * 2);
    for (int i = 0; i < block_size; ++i) {
        interleaved[i * 2] = bufferL[i];
        interleaved[i * 2 + 1] = bufferR[i];
    }
    mock_playback->write(interleaved, block_size);

    assert(mock_playback->recorded_data.size() == (size_t)block_size * 2);
    assert(mock_playback->recorded_data[0] == bufferL[0]);
    std::cout << "  - Data successfully recorded by MockPlayback: PASSED" << std::endl;

    std::cout << "Mock interaction test: PASSED" << std::endl;
}

int main() {
    test_mock_playback_and_plugin();
    return 0;
}
