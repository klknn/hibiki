#include "midi.hpp"
#include <fstream>
#include <algorithm>

namespace hbk {

bool isNoteOn(const MidiEvent& ev) {
    return (ev.type >= 0x90 && ev.type <= 0x9F) && ev.velocity > 0;
}

bool isNoteOff(const MidiEvent& ev) {
    return (ev.type >= 0x80 && ev.type <= 0x8F) || ((ev.type >= 0x90 && ev.type <= 0x9F) && ev.velocity == 0);
}

static uint32_t readBE32(std::ifstream& f) {
    uint8_t b[4];
    f.read((char*)b, 4);
    return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

static uint16_t readBE16(std::ifstream& f) {
    uint8_t b[2];
    f.read((char*)b, 2);
    return (b[0] << 8) | b[1];
}

static uint32_t readVLQ(std::ifstream& f) {
    uint32_t val = 0;
    uint8_t b;
    do {
        b = (uint8_t)f.get();
        val = (val << 7) | (b & 0x7F);
    } while (b & 0x80);
    return val;
}

std::vector<MidiEvent> parseMidi(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};

    uint8_t chunkId[4];
    file.read((char*)chunkId, 4);
    if (std::string((char*)chunkId, 4) != "MThd") return {};

    uint32_t headerSize = readBE32(file);
    /* uint16_t format = */ readBE16(file);
    uint16_t numTracks = readBE16(file);
    uint16_t ticksPerQuarter = readBE16(file);

    if (headerSize > 6) file.seekg(headerSize - 6, std::ios::cur);

    uint32_t tempoMicros = 500000;
    std::vector<MidiEvent> allEvents;

    for (int i = 0; i < numTracks; ++i) {
        file.read((char*)chunkId, 4);
        if (file.eof()) break;
        if (std::string((char*)chunkId, 4) != "MTrk") {
            uint32_t size = readBE32(file);
            file.seekg(size, std::ios::cur);
            continue;
        }

        uint32_t trackSize = readBE32(file);
        size_t trackEnd = (size_t)file.tellg() + trackSize;

        uint32_t currentTicks = 0;
        uint8_t runningStatus = 0;

        while ((size_t)file.tellg() < trackEnd) {
            currentTicks += readVLQ(file);
            int firstByte = file.get();
            if (firstByte == EOF) break;
            uint8_t status = (uint8_t)firstByte;

            if (status < 0x80) {
                status = runningStatus;
                file.seekg(-1, std::ios::cur);
            } else if (status < 0xF0) {
                runningStatus = status;
            }

            uint8_t type = status & 0xF0;
            if (type == 0x80 || type == 0x90 || type == 0xA0 || type == 0xB0 || type == 0xE0) {
                uint8_t data1 = (uint8_t)file.get();
                uint8_t data2 = (uint8_t)file.get();
                if (type == 0x80 || type == 0x90) {
                    MidiEvent ev;
                    ev.seconds = (double)currentTicks * tempoMicros / (1000000.0 * ticksPerQuarter);
                    ev.type = status;
                    ev.channel = (uint8_t)(status & 0x0F);
                    ev.note = data1;
                    ev.velocity = data2;
                    allEvents.push_back(ev);
                }
            } else if (type == 0xC0 || type == 0xD0) {
                file.get();
            } else if (status == 0xFF) {
                uint8_t metaType = (uint8_t)file.get();
                uint32_t len = readVLQ(file);
                if (metaType == 0x51 && len == 3) {
                    tempoMicros = (file.get() << 16) | (file.get() << 8) | file.get();
                } else {
                    file.seekg(len, std::ios::cur);
                }
            } else if (status == 0xF0 || status == 0xF7) {
                uint32_t len = readVLQ(file);
                file.seekg(len, std::ios::cur);
            }
        }
    }

    std::sort(allEvents.begin(), allEvents.end(), [](const MidiEvent& a, const MidiEvent& b) {
        return a.seconds < b.seconds;
    });
    return allEvents;
}

} // namespace hbk
