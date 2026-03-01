#include "audio_io.hpp"

#include <fstream>
#include <string>
#include <vector>
#include <cstdint>

// Simple WAV loader (16-bit PCM)
bool loadWav(const std::string& path, std::vector<float>& out_data, int& out_channels, double& out_duration) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char chunkId[4];
    f.read(chunkId, 4);
    if (std::string(chunkId, 4) != "RIFF") return false;
    f.seekg(4, std::ios::cur); // Skip size
    f.read(chunkId, 4);
    if (std::string(chunkId, 4) != "WAVE") return false;

    int sample_rate = 0;
    int bits_per_sample = 0;
    int channels = 0;

    while (f.read(chunkId, 4)) {
        uint32_t size;
        f.read((char*)&size, 4);
        if (std::string(chunkId, 4) == "fmt ") {
            uint16_t format;
            f.read((char*)&format, 2);
            if (format != 1) return false; // Only PCM supported
            uint16_t chans;
            f.read((char*)&chans, 2);
            channels = chans;
            f.read((char*)&sample_rate, 4);
            f.seekg(6, std::ios::cur); // Skip byte rate and block align
            f.read((char*)&bits_per_sample, 2);
            if (bits_per_sample != 16) return false; // Only 16-bit supported
            if (size > 16) f.seekg(size - 16, std::ios::cur);
        } else if (std::string(chunkId, 4) == "data") {
            int num_samples = size / 2;
            std::vector<int16_t> pcm(num_samples);
            f.read((char*)pcm.data(), size);
            out_data.resize(num_samples);
            for (int i = 0; i < num_samples; ++i) {
                out_data[i] = pcm[i] / 32768.0f;
            }
            out_channels = channels;
            out_duration = (double)num_samples / (channels * sample_rate);
            return true;
        } else {
            f.seekg(size, std::ios::cur);
        }
    }
    return false;
}
