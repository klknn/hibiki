# Windows Plugin Worker & TCP Support — Task List

- [ ] Extract `SharedMemHeader` to `shared_mem_header.hpp`
- [ ] Update `worker_channel_unix.hpp` to use shared header
- [ ] Create `worker_channel_win32.hpp`
- [ ] Create `worker_channel_win32.cpp`
- [ ] Port `worker_channel_tcp.hpp` for cross-platform
- [ ] Port `worker_channel_tcp.cpp` for cross-platform (Winsock2)
- [ ] Port `plugin_proxy.hpp` for Windows
- [ ] Port `plugin_proxy.cpp` for Windows
- [ ] Port `plugin_worker_main.cpp` for Windows
- [ ] Port `worker_daemon_main.cpp` for Windows
- [ ] Update BUILD file
- [ ] Create `worker_channel_win32_test.cc`
- [ ] Update documentation (`ipc.md`, `sandbox.md`)
- [ ] Build verification
