// Windows (Named Pipes + File Mapping) implementation of WorkerChannelLocal.
// See worker_channel_posix.cpp for the POSIX equivalent.

#include "engine/ipc/worker_channel_local.hpp"

#ifdef _WIN32

#include <windows.h>

#include <cstring>
#include <iostream>

#include "absl/log/log.h"

namespace hibiki {

// Platform-specific state for Windows.
struct WorkerChannelLocal::Impl {
  HANDLE pipe_handle = INVALID_HANDLE_VALUE;
  HANDLE shm_handle = NULL;
};

WorkerChannelLocal* WorkerChannelLocal::createServer(
    const std::string& pipe_name, const std::string& shm_name, int block_size,
    int num_channels) {
  auto* ch = new WorkerChannelLocal();
  ch->impl_ = std::make_unique<Impl>();
  ch->path_or_name_ = pipe_name;
  ch->shm_name_ = shm_name;
  ch->block_size_ = block_size;
  ch->num_channels_ = num_channels;
  ch->is_server_ = true;

  size_t header_size = sizeof(SharedMemHeader);
  size_t buf_size = block_size * sizeof(float);
  ch->shm_size_ = header_size + buf_size * num_channels * 2;

  // Create Named Pipe
  std::string full_pipe = "\\\\.\\pipe\\" + pipe_name;
  ch->impl_->pipe_handle =
      CreateNamedPipeA(full_pipe.c_str(), PIPE_ACCESS_DUPLEX,
                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
                       65536, 65536, 0, NULL);

  if (ch->impl_->pipe_handle == INVALID_HANDLE_VALUE) {
    LOG(ERROR) << "WorkerChannelLocal: CreateNamedPipe failed: "
               << GetLastError();
    delete ch;
    return nullptr;
  }

  // Create File Mapping (shared memory)
  std::string full_shm = "Local\\" + shm_name;
  ch->impl_->shm_handle =
      CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                         (DWORD)ch->shm_size_, full_shm.c_str());

  if (ch->impl_->shm_handle == NULL) {
    LOG(ERROR) << "WorkerChannelLocal: CreateFileMapping failed: "
               << GetLastError();
    CloseHandle(ch->impl_->pipe_handle);
    delete ch;
    return nullptr;
  }

  ch->shm_ptr_ = MapViewOfFile(ch->impl_->shm_handle, FILE_MAP_ALL_ACCESS, 0, 0,
                               ch->shm_size_);

  if (ch->shm_ptr_ == nullptr) {
    LOG(ERROR) << "WorkerChannelLocal: MapViewOfFile failed: "
               << GetLastError();
    CloseHandle(ch->impl_->shm_handle);
    CloseHandle(ch->impl_->pipe_handle);
    delete ch;
    return nullptr;
  }

  ch->header_ = reinterpret_cast<SharedMemHeader*>(ch->shm_ptr_);
  memset(ch->header_, 0, sizeof(SharedMemHeader));
  ch->header_->block_size = block_size;
  ch->header_->num_channels = num_channels;

  return ch;
}

WorkerChannelLocal* WorkerChannelLocal::createClient(
    const std::string& pipe_name, const std::string& shm_name) {
  auto* ch = new WorkerChannelLocal();
  ch->impl_ = std::make_unique<Impl>();
  ch->path_or_name_ = pipe_name;
  ch->shm_name_ = shm_name;
  ch->is_server_ = false;

  std::string full_pipe = "\\\\.\\pipe\\" + pipe_name;
  if (!WaitNamedPipeA(full_pipe.c_str(), 5000)) {
    LOG(INFO) << "WorkerChannelLocal: WaitNamedPipe timeout";
    delete ch;
    return nullptr;
  }

  ch->impl_->pipe_handle =
      CreateFileA(full_pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                  OPEN_EXISTING, 0, NULL);

  if (ch->impl_->pipe_handle == INVALID_HANDLE_VALUE) {
    LOG(ERROR) << "WorkerChannelLocal: CreateFile for pipe failed: "
               << GetLastError();
    delete ch;
    return nullptr;
  }

  DWORD mode = PIPE_READMODE_BYTE;
  SetNamedPipeHandleState(ch->impl_->pipe_handle, &mode, NULL, NULL);

  std::string full_shm = "Local\\" + shm_name;
  ch->impl_->shm_handle =
      OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, full_shm.c_str());

  if (ch->impl_->shm_handle == NULL) {
    LOG(ERROR) << "WorkerChannelLocal: OpenFileMapping failed: "
               << GetLastError();
    CloseHandle(ch->impl_->pipe_handle);
    delete ch;
    return nullptr;
  }

  ch->shm_ptr_ =
      MapViewOfFile(ch->impl_->shm_handle, FILE_MAP_ALL_ACCESS, 0, 0, 0);

  if (ch->shm_ptr_ == nullptr) {
    LOG(ERROR) << "WorkerChannelLocal: MapViewOfFile failed: "
               << GetLastError();
    CloseHandle(ch->impl_->shm_handle);
    CloseHandle(ch->impl_->pipe_handle);
    delete ch;
    return nullptr;
  }

  ch->header_ = reinterpret_cast<SharedMemHeader*>(ch->shm_ptr_);
  ch->block_size_ = ch->header_->block_size;
  ch->num_channels_ = ch->header_->num_channels;

  return ch;
}

bool WorkerChannelLocal::accept() {
  if (!impl_ || impl_->pipe_handle == INVALID_HANDLE_VALUE) return false;
  if (!ConnectNamedPipe(impl_->pipe_handle, NULL)) {
    DWORD err = GetLastError();
    if (err != ERROR_PIPE_CONNECTED) {
      LOG(ERROR) << "WorkerChannelLocal: ConnectNamedPipe failed: " << err;
      return false;
    }
  }
  return true;
}

WorkerChannelLocal::~WorkerChannelLocal() {
  if (shm_ptr_) UnmapViewOfFile(shm_ptr_);
  if (impl_) {
    if (impl_->shm_handle) CloseHandle(impl_->shm_handle);
    if (impl_->pipe_handle != INVALID_HANDLE_VALUE) {
      if (is_server_) DisconnectNamedPipe(impl_->pipe_handle);
      CloseHandle(impl_->pipe_handle);
    }
  }
}

bool WorkerChannelLocal::send(const void* data, size_t len) {
  if (!impl_ || impl_->pipe_handle == INVALID_HANDLE_VALUE) return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
  size_t remaining = len;
  while (remaining > 0) {
    DWORD written = 0;
    if (!WriteFile(impl_->pipe_handle, p, (DWORD)remaining, &written, NULL))
      return false;
    if (written == 0) return false;
    p += written;
    remaining -= written;
  }
  return true;
}

bool WorkerChannelLocal::recv(void* buf, size_t len) {
  if (!impl_ || impl_->pipe_handle == INVALID_HANDLE_VALUE) return false;
  uint8_t* p = reinterpret_cast<uint8_t*>(buf);
  size_t remaining = len;
  while (remaining > 0) {
    DWORD nread = 0;
    if (!ReadFile(impl_->pipe_handle, p, (DWORD)remaining, &nread, NULL))
      return false;
    if (nread == 0) return false;
    p += nread;
    remaining -= nread;
  }
  return true;
}

bool WorkerChannelLocal::sendMessage(const void* data, size_t len) {
  uint32_t size = static_cast<uint32_t>(len);
  if (!send(&size, sizeof(size))) return false;
  return send(data, len);
}

int WorkerChannelLocal::recvMessage(std::string& out) {
  uint32_t size = 0;
  if (!recv(&size, sizeof(size))) return -1;
  if (size > 4 * 1024 * 1024) return -1;
  out.resize(size);
  if (!recv(out.data(), size)) return -1;
  return (int)size;
}

float* WorkerChannelLocal::inputBuffer(int channel) {
  if (channel < 0 || channel >= num_channels_ || !shm_ptr_) return nullptr;
  uint8_t* base = reinterpret_cast<uint8_t*>(shm_ptr_);
  base += sizeof(SharedMemHeader);
  base += channel * block_size_ * sizeof(float);
  return reinterpret_cast<float*>(base);
}

float* WorkerChannelLocal::outputBuffer(int channel) {
  if (channel < 0 || channel >= num_channels_ || !shm_ptr_) return nullptr;
  uint8_t* base = reinterpret_cast<uint8_t*>(shm_ptr_);
  base += sizeof(SharedMemHeader);
  base += num_channels_ * block_size_ * sizeof(float);
  base += channel * block_size_ * sizeof(float);
  return reinterpret_cast<float*>(base);
}

#endif  // _WIN32

}  // namespace hibiki
