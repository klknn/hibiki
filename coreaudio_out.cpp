#include "coreaudio_out.hpp"
#include <AudioUnit/AudioUnit.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <thread>

struct CoreAudioPlayback::Impl {
    AudioUnit audioUnit;
    int sampleRate;
    int channels;
    bool ready = false;

    // A simple ring buffer to bridge push (write) and pull (callback)
    std::vector<float> ringBuffer;
    std::atomic<size_t> readPtr{0};
    std::atomic<size_t> writePtr{0};
    size_t capacity;

    Impl(int rate, int ch) : sampleRate(rate), channels(ch) {
        capacity = rate * ch; // 1 second buffer
        ringBuffer.resize(capacity, 0.0f);
    }

    size_t getAvailableRead() {
        size_t r = readPtr.load(std::memory_order_acquire);
        size_t w = writePtr.load(std::memory_order_acquire);
        if (w >= r) return w - r;
        return capacity - r + w;
    }

    size_t getAvailableWrite() {
        return capacity - getAvailableRead() - 1;
    }

    void writeData(const float* data, size_t count) {
        size_t w = writePtr.load(std::memory_order_relaxed);
        for (size_t i = 0; i < count; ++i) {
            ringBuffer[w] = data[i];
            w = (w + 1) % capacity;
        }
        writePtr.store(w, std::memory_order_release);
    }

    void readData(float* data, size_t count) {
        size_t r = readPtr.load(std::memory_order_relaxed);
        for (size_t i = 0; i < count; ++i) {
            data[i] = ringBuffer[r];
            r = (r + 1) % capacity;
        }
        readPtr.store(r, std::memory_order_release);
    }
};

static OSStatus PlaybackCallback(void* inRefCon,
                                 AudioUnitRenderActionFlags* ioActionFlags,
                                 const AudioTimeStamp* inTimeStamp,
                                 UInt32 inBusNumber,
                                 UInt32 inNumberFrames,
                                 AudioBufferList* ioData) {
    auto* impl = static_cast<CoreAudioPlayback::Impl*>(inRefCon);
    float* outL = static_cast<float*>(ioData->mBuffers[0].mData);
    float* outR = static_cast<float*>(ioData->mBuffers[1].mData);

    size_t needed = inNumberFrames * 2;
    size_t available = impl->getAvailableRead();

    if (available < needed) {
        // Underflow
        std::fill(outL, outL + inNumberFrames, 0.0f);
        std::fill(outR, outR + inNumberFrames, 0.0f);
        return noErr;
    }

    for (UInt32 i = 0; i < inNumberFrames; ++i) {
        float interleaved[2];
        impl->readData(interleaved, 2);
        outL[i] = interleaved[0];
        outR[i] = interleaved[1];
    }

    return noErr;
}

CoreAudioPlayback::CoreAudioPlayback(int rate, int ch) : impl(std::make_unique<Impl>(rate, ch)) {
    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (!comp) {
        std::cerr << "CoreAudio: Failed to find component" << std::endl;
        return;
    }

    if (AudioComponentInstanceNew(comp, &impl->audioUnit) != noErr) {
        std::cerr << "CoreAudio: Failed to create AudioUnit instance" << std::endl;
        return;
    }

    if (AudioUnitInitialize(impl->audioUnit) != noErr) {
        std::cerr << "CoreAudio: Failed to initialize AudioUnit" << std::endl;
        return;
    }

    AudioStreamBasicDescription format;
    format.mSampleRate = rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsNonInterleaved | kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = sizeof(float);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(float);
    format.mChannelsPerFrame = 2; // Fixed to stereo for now
    format.mBitsPerChannel = 32;

    if (AudioUnitSetProperty(impl->audioUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof(format)) != noErr) {
        std::cerr << "CoreAudio: Failed to set stream format" << std::endl;
        return;
    }

    AURenderCallbackStruct callback;
    callback.inputProc = PlaybackCallback;
    callback.inputProcRefCon = impl.get();

    if (AudioUnitSetProperty(impl->audioUnit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Global, 0, &callback, sizeof(callback)) != noErr) {
        std::cerr << "CoreAudio: Failed to set render callback" << std::endl;
        return;
    }

    if (AudioOutputUnitStart(impl->audioUnit) != noErr) {
        std::cerr << "CoreAudio: Failed to start AudioUnit" << std::endl;
        return;
    }

    impl->ready = true;
}

CoreAudioPlayback::~CoreAudioPlayback() {
    if (impl->ready) {
        AudioOutputUnitStop(impl->audioUnit);
        AudioUnitUninitialize(impl->audioUnit);
        AudioComponentInstanceDispose(impl->audioUnit);
    }
}

bool CoreAudioPlayback::is_ready() const {
    return impl->ready;
}

void CoreAudioPlayback::write(const std::vector<float>& interleaved_data, int num_frames) {
    if (!impl->ready) return;

    size_t count = interleaved_data.size();
    
    // Simple back-pressure if buffer is full
    while (impl->getAvailableWrite() < count) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    impl->writeData(interleaved_data.data(), count);
}
