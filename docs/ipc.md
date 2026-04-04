# Hibiki IPC Architecture

Hibiki uses two independent IPC channels with different transports,
each chosen for the specific requirements of its communication pair.

## 1. GUI ↔ Backend (`hibiki-gui-java` ↔ `hbk-play`)

```
┌──────────────────┐   stdin (commands)    ┌─────────┐
│ hibiki-gui-java  │ ────────────────────▶ │ hbk-play│
│    (Java/Swing)  │ ◀──────────────────── │  (C++)  │
└──────────────────┘  stdout (notifications)└─────────┘
```

**Transport:** stdin/stdout pipes (length-prefixed protobuf)

**Why pipes?**
- **Simplicity**: The Java GUI spawns `hbk-play` as a single child
  process. Pipes are automatically created by `ProcessBuilder` — no
  port allocation, no socket path management, no cleanup.
- **Lifecycle**: Pipe EOF automatically signals process death. If the
  backend crashes, the GUI gets EOF on stdout. If the GUI closes,
  the backend gets EOF on stdin. No heartbeat needed.
- **Security**: No listening socket exposed. The channel is private
  to the parent–child pair.
- **Precedent**: This is the same pattern used by LSP servers,
  `ffmpeg -f pipe:`, and many UNIX tools.

**Protocol:**
- GUI→Engine: `pb.commands.CommandRequest` (framed protobuf)
- Engine→GUI: `pb.notifications.Notification` (framed protobuf)
- Framing: `uint32_t size` + `bytes[size]`

**Limitations:**
- 1:1 only (one GUI, one backend)
- Not suitable for multiple concurrent clients
- No random-access or shared memory

---

## 2. Backend ↔ Plugin Worker (`hbk-play` ↔ `hbk-plugin-worker`)

```
┌─────────┐  Unix socket (commands)  ┌────────────────────┐
│ hbk-play│ ──────────────────────▶  │ hbk-plugin-worker  │
│  (host) │ ◀──────────────────────  │    (per-plugin)    │
│         │   shared memory (audio)  │                    │
│         │ ◀═══════════════════════▶│                    │
└─────────┘                          └────────────────────┘
     (can have multiple workers, one per plugin)
```

**Transport:** Unix domain socket + POSIX shared memory

**Why sockets + shm?**
- **Multiple workers**: Each plugin runs in its own process. The
  host needs N independent connections — pipes only give you one
  stdin/stdout pair per child.
- **Shared memory for audio**: Audio buffers (512 × 2ch × 4B = 4KB
  per block) must be zero-copy for real-time performance. Protobuf
  serialization would add unacceptable latency.
- **Crash isolation**: If a plugin crashes, only its worker dies.
  The host detects the broken socket, cleans up, and can respawn.
- **Respawn**: Workers can be killed and restarted without restarting
  the host. Socket reconnect is straightforward; pipe reconnect
  would require re-forking.

**Protocol:**
- Host→Worker: `pb.worker.WorkerRequest` (framed protobuf)
- Worker→Host: `pb.worker.WorkerResponse` (framed protobuf)
- Audio data: POSIX shared memory (`shm_open`/`mmap`)
- Framing: `uint32_t size` + `bytes[size]`

**Shared Memory Layout:**
```
Offset    Size           Content
0         64 B           Header (block_size, num_channels, flags)
64        N×4 B          Input L
64+N×4    N×4 B          Input R
64+N×8    N×4 B          Output L
64+N×12   N×4 B          Output R
```
Where N = `block_size` (default 512 samples).

---

## Design Decision Summary

| Aspect         | GUI ↔ Backend   | Backend ↔ Worker     |
|----------------|-----------------|----------------------|
| Transport      | stdin/stdout    | Unix socket + shm    |
| Multiplexing   | 1:1             | 1:N                  |
| Audio transfer | N/A             | Shared memory        |
| Crash handling | EOF detection   | Socket break + respawn |
| Setup cost     | Zero (pipes)    | Socket + shm setup   |
| Platform       | Cross-platform  | Linux/macOS          |

## Future Considerations

- **Migrating GUI IPC to sockets** would enable remote hosting or
  multiple GUI clients, but adds complexity without current benefit.
- **Windows support** would replace Unix sockets with named pipes and
  shared memory with `CreateFileMapping`.
