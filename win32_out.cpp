#include "win32_out.hpp"
#include <audioclient.h>
#include <iostream>
#include <mmdeviceapi.h>
#include <windows.h>


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

void Win32Playback::write(const std::vector<float> &interleaved_data,
                          int num_frames) {
  if (!impl->pRenderClient)
    return;

  BYTE *pData;
  HRESULT hr = impl->pRenderClient->GetBuffer(num_frames, &pData);
  if (SUCCEEDED(hr)) {
    memcpy(pData, interleaved_data.data(),
           num_frames * channels * sizeof(float));
    impl->pRenderClient->ReleaseBuffer(num_frames, 0);
  } else if (hr == AUDCLNT_E_BUFFER_TOO_LARGE) {
    // Just skip if buffer is full, for simplicity
  }
}
