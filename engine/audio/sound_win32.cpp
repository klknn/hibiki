#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

#include "engine/audio/sound.hpp"

namespace hibiki {

namespace {

class SoundDeviceWin32 : public SoundDevice {
  struct Impl {
    IAudioClient* pAudioClient = nullptr;
    IAudioRenderClient* pRenderClient = nullptr;
    UINT32 bufferFrameCount = 0;
    HANDLE hEvent = nullptr;
  };

  std::unique_ptr<Impl> impl;
  int sample_rate;
  int channels;

 public:
  SoundDeviceWin32(int rate, int ch, int latency_ms)
      : sample_rate(rate), channels(ch) {
    impl = std::make_unique<Impl>();

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
      std::cerr << "CoInitializeEx failed: " << std::hex << hr << std::endl;
      return;
    }

    IMMDeviceEnumerator* pEnumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (FAILED(hr)) {
      std::cerr << "CoCreateInstance(MMDeviceEnumerator) failed" << std::endl;
      return;
    }

    IMMDevice* pDevice = nullptr;
    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    pEnumerator->Release();
    if (FAILED(hr)) {
      std::cerr << "GetDefaultAudioEndpoint failed" << std::endl;
      return;
    }

    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL,
                           (void**)&impl->pAudioClient);
    pDevice->Release();
    if (FAILED(hr)) {
      std::cerr << "IAudioClient Activation failed" << std::endl;
      return;
    }

    WAVEFORMATEX* pwfx = nullptr;
    hr = impl->pAudioClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) {
      std::cerr << "GetMixFormat failed" << std::endl;
      return;
    }

    // Update actual sample rate and channels from mix format
    sample_rate = pwfx->nSamplesPerSec;
    channels = pwfx->nChannels;
    std::cerr << "[SoundDeviceWin32] Selected format: " << sample_rate
              << " Hz, " << channels << " channels" << std::endl;

    if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
      WAVEFORMATEXTENSIBLE* pEx = (WAVEFORMATEXTENSIBLE*)pwfx;
      if (pEx->SubFormat != KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
        std::cerr
            << "[SoundDeviceWin32] Warning: Mix format is not IEEE Float. "
               "Audio might be distorted."
            << std::endl;
      }
    } else if (pwfx->wFormatTag != WAVE_FORMAT_IEEE_FLOAT) {
      std::cerr << "[SoundDeviceWin32] Warning: Mix format is not IEEE Float."
                << std::endl;
    }

    REFERENCE_TIME hnsRequestedDuration = (REFERENCE_TIME)latency_ms * 10000;
    hr = impl->pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                        hnsRequestedDuration, 0, pwfx, NULL);
    if (FAILED(hr)) {
      std::cerr << "IAudioClient::Initialize failed: " << std::hex << hr
                << std::endl;
      CoTaskMemFree(pwfx);
      return;
    }
    CoTaskMemFree(pwfx);

    hr = impl->pAudioClient->GetBufferSize(&impl->bufferFrameCount);
    hr = impl->pAudioClient->GetService(__uuidof(IAudioRenderClient),
                                        (void**)&impl->pRenderClient);
    if (FAILED(hr)) {
      std::cerr << "GetService(IAudioRenderClient) failed" << std::endl;
      return;
    }

    hr = impl->pAudioClient->Start();
  }

  ~SoundDeviceWin32() override {
    if (impl->pAudioClient) {
      impl->pAudioClient->Stop();
      impl->pAudioClient->Release();
    }
    if (impl->pRenderClient) impl->pRenderClient->Release();
    CoUninitialize();
  }

  int get_sample_rate() const override { return sample_rate; }
  int get_channels() const override { return channels; }
  bool is_ready() const override { return impl->pRenderClient != nullptr; }

  void write(const std::vector<float>& interleaved_data,
             int num_frames) override {
    if (!impl->pRenderClient) return;

    UINT32 padding = 0;
    int retry = 0;
    while (SUCCEEDED(impl->pAudioClient->GetCurrentPadding(&padding)) &&
           retry < 100) {
      if (impl->bufferFrameCount - padding >= (UINT32)num_frames) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      retry++;
    }

    BYTE* pData;
    HRESULT hr = impl->pRenderClient->GetBuffer(num_frames, &pData);
    if (SUCCEEDED(hr)) {
      float* floatData = (float*)pData;
      for (size_t i = 0; i < interleaved_data.size(); ++i) {
        float sample = interleaved_data[i];
        if (!std::isfinite(sample)) sample = 0.0f;
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        floatData[i] = sample;
      }
      impl->pRenderClient->ReleaseBuffer(num_frames, 0);
    } else if (hr == AUDCLNT_E_BUFFER_TOO_LARGE) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
};

}  // namespace

std::unique_ptr<SoundDevice> SoundDevice::create(int rate, int ch,
                                                 int latency_ms) {
  return std::make_unique<SoundDeviceWin32>(rate, ch, latency_ms);
}

}  // namespace hibiki
