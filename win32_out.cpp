#include "win32_out.hpp"
#include <audioclient.h>
#include <iostream>
#include <mmdeviceapi.h>
#include <windows.h>
<<<<<<< HEAD
#include <thread>
#include <chrono>
=======
>>>>>>> 47601e7bb99debf560fbb194795a6862d325182c


struct Win32Playback::Impl {
  IAudioClient *pAudioClient = nullptr;
  IAudioRenderClient *pRenderClient = nullptr;
  UINT32 bufferFrameCount = 0;
  HANDLE hEvent = nullptr;
};

Win32Playback::Win32Playback(int rate, int ch)
    : sample_rate(rate), channels(ch) {
  impl = new Impl();

  HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    std::cerr << "CoInitializeEx failed: " << std::hex << hr << std::endl;
    return;
  }

  IMMDeviceEnumerator *pEnumerator = nullptr;
  hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                        __uuidof(IMMDeviceEnumerator), (void **)&pEnumerator);
  if (FAILED(hr)) {
    std::cerr << "CoCreateInstance(MMDeviceEnumerator) failed" << std::endl;
    return;
  }

  IMMDevice *pDevice = nullptr;
  hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
  pEnumerator->Release();
  if (FAILED(hr)) {
    std::cerr << "GetDefaultAudioEndpoint failed" << std::endl;
    return;
  }

  hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL,
                         (void **)&impl->pAudioClient);
  pDevice->Release();
  if (FAILED(hr)) {
    std::cerr << "IAudioClient Activation failed" << std::endl;
    return;
  }

<<<<<<< HEAD
  WAVEFORMATEX *pwfx = nullptr;
  hr = impl->pAudioClient->GetMixFormat(&pwfx);
  if (FAILED(hr)) {
    std::cerr << "GetMixFormat failed" << std::endl;
    return;
  }

  // Update actual sample rate and channels from mix format
  sample_rate = pwfx->nSamplesPerSec;
  channels = pwfx->nChannels;
  std::cerr << "[Win32Playback] Selected format: " << sample_rate << " Hz, "
            << channels << " channels" << std::endl;

  // Ensure we are using float format for our internal processing
  if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
    WAVEFORMATEXTENSIBLE *pEx = (WAVEFORMATEXTENSIBLE *)pwfx;
    if (pEx->SubFormat != KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
      std::cerr << "[Win32Playback] Warning: Mix format is not IEEE Float. "
                   "Audio might be distorted."
                << std::endl;
    }
  } else if (pwfx->wFormatTag != WAVE_FORMAT_IEEE_FLOAT) {
    std::cerr << "[Win32Playback] Warning: Mix format is not IEEE Float."
              << std::endl;
  }

  REFERENCE_TIME hnsRequestedDuration = 500000; // 50ms
  hr = impl->pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                       hnsRequestedDuration, 0,
                                       pwfx, NULL);
  if (FAILED(hr)) {
    std::cerr << "IAudioClient::Initialize failed: " << std::hex << hr
              << std::endl;
    CoTaskMemFree(pwfx);
    return;
  }
  CoTaskMemFree(pwfx);
=======
  WAVEFORMATEXTENSIBLE wfx = {};
  wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  wfx.Format.nChannels = channels;
  wfx.Format.nSamplesPerSec = sample_rate;
  wfx.Format.wBitsPerSample = 32;
  wfx.Format.nBlockAlign =
      (wfx.Format.nChannels * wfx.Format.wBitsPerSample) / 8;
  wfx.Format.nAvgBytesPerSec =
      wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
  wfx.Format.cbSize = 22;
  wfx.Samples.wValidBitsPerSample = 32;
  wfx.dwChannelMask = (channels == 2)
                          ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT)
                          : SPEAKER_FRONT_LEFT;
  wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

  REFERENCE_TIME hnsRequestedDuration = 500000; // 50ms
  hr = impl->pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                      hnsRequestedDuration, 0,
                                      (WAVEFORMATEX *)&wfx, NULL);
  if (FAILED(hr)) {
    std::cerr << "IAudioClient::Initialize failed: " << std::hex << hr
              << std::endl;
    return;
  }
>>>>>>> 47601e7bb99debf560fbb194795a6862d325182c

  hr = impl->pAudioClient->GetBufferSize(&impl->bufferFrameCount);
  hr = impl->pAudioClient->GetService(__uuidof(IAudioRenderClient),
                                      (void **)&impl->pRenderClient);
  if (FAILED(hr)) {
    std::cerr << "GetService(IAudioRenderClient) failed" << std::endl;
    return;
  }

  hr = impl->pAudioClient->Start();
}

Win32Playback::~Win32Playback() {
  if (impl->pAudioClient) {
    impl->pAudioClient->Stop();
    impl->pAudioClient->Release();
  }
  if (impl->pRenderClient)
    impl->pRenderClient->Release();
  delete impl;
  CoUninitialize();
}

bool Win32Playback::is_ready() const { return impl->pRenderClient != nullptr; }

<<<<<<< HEAD

=======
>>>>>>> 47601e7bb99debf560fbb194795a6862d325182c
void Win32Playback::write(const std::vector<float> &interleaved_data,
                          int num_frames) {
  if (!impl->pRenderClient)
    return;

<<<<<<< HEAD
  // Simple back-pressure: if the buffer is too full, wait a bit
  UINT32 padding = 0;
  int retry = 0;
  while (SUCCEEDED(impl->pAudioClient->GetCurrentPadding(&padding)) && retry < 100) {
      if (impl->bufferFrameCount - padding >= (UINT32)num_frames) {
          break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      retry++;
  }

  BYTE *pData;
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
=======
  BYTE *pData;
  HRESULT hr = impl->pRenderClient->GetBuffer(num_frames, &pData);
  if (SUCCEEDED(hr)) {
    memcpy(pData, interleaved_data.data(),
           num_frames * channels * sizeof(float));
    impl->pRenderClient->ReleaseBuffer(num_frames, 0);
  } else if (hr == AUDCLNT_E_BUFFER_TOO_LARGE) {
    // Just skip if buffer is full, for simplicity
>>>>>>> 47601e7bb99debf560fbb194795a6862d325182c
  }
}
