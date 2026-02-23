#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct HostProcessContext {
    double sampleRate;
    double tempo;
    int32_t timeSigNumerator;
    int32_t timeSigDenominator;
    int64_t continuousTimeSamples;
    double projectTimeMusic;
};

struct MidiNoteEvent {
    int32_t sampleOffset;
    uint8_t channel;
    uint8_t pitch;
    float velocity; // 0.0 - 1.0
    bool isNoteOn;
};

struct VstParamInfo {
    uint32_t id;
    std::string name;
    double defaultValue;
};
