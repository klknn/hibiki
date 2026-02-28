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
