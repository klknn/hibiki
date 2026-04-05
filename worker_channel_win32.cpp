#ifdef _WIN32

#include "worker_channel_local.hpp"

#include <iostream>

// Named pipe path format: \\.\pipe\hbk-plugin-XXXX
// Shared memory name format: Local\hbk-plugin-XXXX

WorkerChannelLocal* WorkerChannelLocal::createServer(
    const std::string& pipe_name, const std::string& shm_name,
    int block_size, int num_channels) {
  auto* ch = new WorkerChannelLocal();
  ch->path_or_name_ = pipe_name;
  ch->shm_name_ = shm_name;
  ch->block_size_ = block_size;
  ch->num_channels_ = num_channels;
  ch->is_server_ = true;

  // Create Named Pipe (server side)
  // PIPE_ACCESS_DUPLEX: both read and write
  // PIPE_TYPE_BYTE | PIPE_READMODE_BYTE: raw byte stream (like Unix socket)
  // PIPE_WAIT: blocking mode
  std::string full_pipe = "\\\\.\\pipe\\" + pipe_name;
  ch->(HANDLE)pipe_handle_ = CreateNamedPipeA(
      full_pipe.c_str(),
      PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
      1,       // max instances
      65536,   // output buffer size
      65536,   // input buffer size
      0,       // default timeout
      NULL);   // default security

  if (ch->(HANDLE)pipe_handle_ == INVALID_HANDLE_VALUE) {
    std::cerr << "WorkerChannelLocal: CreateNamedPipe failed: "
              << GetLastError() << "\n";
    delete ch;
    return nullptr;
  }

  // Create File Mapping (shared memory)
  // Equivalent to shm_open + ftruncate + mmap
  size_t header_size = sizeof(SharedMemHeader);
  size_t buf_size = block_size * sizeof(float);
  ch->shm_size_ = header_size + buf_size * num_channels * 2;  // in + out

  std::string full_shm = "Local\\" + shm_name;
  ch->shm_handle_ = CreateFileMappingA(
      INVALID_HANDLE_VALUE,   // Use paging file (not a real file)
      NULL,                   // Default security
      PAGE_READWRITE,         // Read/write access
      0,                      // High DWORD of size
      (DWORD)ch->shm_size_,   // Low DWORD of size
      full_shm.c_str());

  if (ch->shm_handle_ == NULL) {
    std::cerr << "WorkerChannelLocal: CreateFileMapping failed: "
              << GetLastError() << "\n";
    CloseHandle(ch->(HANDLE)pipe_handle_);
    delete ch;
    return nullptr;
  }

  // Map the entire shared memory region
  ch->shm_ptr_ = MapViewOfFile(
      ch->shm_handle_,
      FILE_MAP_ALL_ACCESS,
      0, 0,
      ch->shm_size_);

  if (ch->shm_ptr_ == nullptr) {
    std::cerr << "WorkerChannelLocal: MapViewOfFile failed: "
              << GetLastError() << "\n";
    CloseHandle(ch->shm_handle_);
    CloseHandle(ch->(HANDLE)pipe_handle_);
    delete ch;
    return nullptr;
  }

  // Initialize header
  ch->header_ = reinterpret_cast<SharedMemHeader*>(ch->shm_ptr_);
  memset(ch->header_, 0, sizeof(SharedMemHeader));
  ch->header_->block_size = block_size;
  ch->header_->num_channels = num_channels;

  return ch;
}

WorkerChannelLocal* WorkerChannelLocal::createClient(
    const std::string& pipe_name, const std::string& shm_name) {
  auto* ch = new WorkerChannelLocal();
  ch->path_or_name_ = pipe_name;
  ch->shm_name_ = shm_name;
  ch->is_server_ = false;

  // Connect to existing Named Pipe
  std::string full_pipe = "\\\\.\\pipe\\" + pipe_name;

  // Wait for the pipe to become available (server may not have called
  // ConnectNamedPipe yet)
  if (!WaitNamedPipeA(full_pipe.c_str(), 5000)) {
    std::cerr << "WorkerChannelLocal: WaitNamedPipe timeout\n";
    delete ch;
    return nullptr;
  }

  ch->(HANDLE)pipe_handle_ = CreateFileA(
      full_pipe.c_str(),
      GENERIC_READ | GENERIC_WRITE,
      0,       // no sharing
      NULL,    // default security
      OPEN_EXISTING,
      0,       // default attributes
      NULL);

  if (ch->(HANDLE)pipe_handle_ == INVALID_HANDLE_VALUE) {
    std::cerr << "WorkerChannelLocal: CreateFile for pipe failed: "
              << GetLastError() << "\n";
    delete ch;
    return nullptr;
  }

  // Set pipe to byte mode
  DWORD mode = PIPE_READMODE_BYTE;
  SetNamedPipeHandleState(ch->(HANDLE)pipe_handle_, &mode, NULL, NULL);

  // Open existing File Mapping (shared memory)
  std::string full_shm = "Local\\" + shm_name;
  ch->shm_handle_ = OpenFileMappingA(
      FILE_MAP_ALL_ACCESS,
      FALSE,
      full_shm.c_str());

  if (ch->shm_handle_ == NULL) {
    std::cerr << "WorkerChannelLocal: OpenFileMapping failed: "
              << GetLastError() << "\n";
    CloseHandle(ch->(HANDLE)pipe_handle_);
    delete ch;
    return nullptr;
  }

  // Map — we don't know the size yet, but the header tells us
  // First map just the header to read block_size and num_channels
  ch->shm_ptr_ = MapViewOfFile(
      ch->shm_handle_,
      FILE_MAP_ALL_ACCESS,
      0, 0, 0);  // 0 = map entire object

  if (ch->shm_ptr_ == nullptr) {
    std::cerr << "WorkerChannelLocal: MapViewOfFile failed: "
              << GetLastError() << "\n";
    CloseHandle(ch->shm_handle_);
    CloseHandle(ch->(HANDLE)pipe_handle_);
    delete ch;
    return nullptr;
  }

  ch->header_ = reinterpret_cast<SharedMemHeader*>(ch->shm_ptr_);
  ch->block_size_ = ch->header_->block_size;
  ch->num_channels_ = ch->header_->num_channels;

  return ch;
}

bool WorkerChannelLocal::accept() {
  if (!is_server_ || (HANDLE)pipe_handle_ == INVALID_HANDLE_VALUE) return false;

  // Block until a client connects
  if (!ConnectNamedPipe((HANDLE)pipe_handle_, NULL)) {
    DWORD err = GetLastError();
    if (err != ERROR_PIPE_CONNECTED) {
      std::cerr << "WorkerChannelLocal: ConnectNamedPipe failed: "
                << err << "\n";
      return false;
    }
    // ERROR_PIPE_CONNECTED means client connected before we called
    // ConnectNamedPipe — that's fine
  }
  return true;
}

WorkerChannelLocal::~WorkerChannelLocal() {
  if (shm_ptr_) UnmapViewOfFile(shm_ptr_);
  if (shm_handle_) CloseHandle(shm_handle_);

  if ((HANDLE)pipe_handle_ != INVALID_HANDLE_VALUE) {
    if (is_server_) DisconnectNamedPipe((HANDLE)pipe_handle_);
    CloseHandle((HANDLE)pipe_handle_);
  }
  // Note: shared memory is automatically freed when all handles are closed
  // (no equivalent of shm_unlink needed on Windows)
}

bool WorkerChannelLocal::send(const void* data, size_t len) {
  if ((HANDLE)pipe_handle_ == INVALID_HANDLE_VALUE) return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
  size_t remaining = len;
  while (remaining > 0) {
    DWORD written = 0;
    if (!WriteFile((HANDLE)pipe_handle_, p, (DWORD)remaining, &written, NULL))
      return false;
    if (written == 0) return false;
    p += written;
    remaining -= written;
  }
  return true;
}

bool WorkerChannelLocal::recv(void* buf, size_t len) {
  if ((HANDLE)pipe_handle_ == INVALID_HANDLE_VALUE) return false;
  uint8_t* p = reinterpret_cast<uint8_t*>(buf);
  size_t remaining = len;
  while (remaining > 0) {
    DWORD nread = 0;
    if (!ReadFile((HANDLE)pipe_handle_, p, (DWORD)remaining, &nread, NULL))
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
  if (size > 4 * 1024 * 1024) return -1;  // 4MB safety limit
  out.resize(size);
  if (!recv(out.data(), size)) return -1;
  return (int)size;
}

float* WorkerChannelLocal::inputBuffer(int channel) {
  if (channel < 0 || channel >= num_channels_ || !shm_ptr_) return nullptr;
  uint8_t* base = reinterpret_cast<uint8_t*>(shm_ptr_);
  base += sizeof(SharedMemHeader);  // Skip header
  base += channel * block_size_ * sizeof(float);  // Input buffers
  return reinterpret_cast<float*>(base);
}

float* WorkerChannelLocal::outputBuffer(int channel) {
  if (channel < 0 || channel >= num_channels_ || !shm_ptr_) return nullptr;
  uint8_t* base = reinterpret_cast<uint8_t*>(shm_ptr_);
  base += sizeof(SharedMemHeader);
  base += num_channels_ * block_size_ * sizeof(float);  // Past input buffers
  base += channel * block_size_ * sizeof(float);  // Output buffers
  return reinterpret_cast<float*>(base);
}

#endif  // _WIN32
