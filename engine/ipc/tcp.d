module engine.ipc.tcp;

version (Windows) {
    import core.sys.windows.winsock2 : closesocket, recv, send, setsockopt, SOCKET,
                                       WSADATA, WSAGetLastError, WSAStartup;
    import core.sys.windows.windef : MAKEWORD;

    alias SocketHandle = SOCKET;
    private __gshared char[64] errBuf = 0;
} else {
    import core.stdc.errno : errno;
    import core.stdc.string : strerror;
    import core.sys.posix.sys.socket : setsockopt;
    import core.sys.posix.unistd : close, read, write;

    alias SocketHandle = int;
}

extern (C++, "hibiki") {
    /// Initializes the platform TCP stack (WSAStartup on Windows, no-op on POSIX).
    void tcp_init() @nogc nothrow {
        version (Windows) {
            static bool initialized = false;
            if (!initialized) {
                WSADATA wsa;
                WSAStartup(MAKEWORD(2, 2), &wsa);
                initialized = true;
            }
        }
    }

    /// Closes a TCP socket.
    int tcp_close(SocketHandle s) @nogc nothrow {
        version (Windows) {
            return closesocket(s);
        } else {
            return close(s);
        }
    }

    /// Sends raw bytes over a TCP socket. Returns bytes sent, or <= 0 on error.
    int tcp_send(SocketHandle s, const(void)* buf, size_t len) @nogc nothrow {
        version (Windows) {
            return send(s, cast(const(char)*)buf, cast(int)len, 0);
        } else {
            return cast(int)write(s, buf, len);
        }
    }

    /// Receives raw bytes from a TCP socket. Returns bytes received, or <= 0 on error.
    int tcp_recv(SocketHandle s, void* buf, size_t len) @nogc nothrow {
        version (Windows) {
            return recv(s, cast(char*)buf, cast(int)len, 0);
        } else {
            return cast(int)read(s, buf, len);
        }
    }

    /// Sets an integer socket option.
    void tcp_setsockopt(SocketHandle s, int level, int optname, int val) @nogc nothrow {
        version (Windows) {
            setsockopt(s, level, optname, cast(const(char)*)&val, cast(int)val.sizeof);
        } else {
            setsockopt(s, level, optname, &val, cast(uint)val.sizeof);
        }
    }

    /// Returns a human-readable error string for the last socket error.
    const(char)* tcp_strerror() @nogc nothrow {
        version (Windows) {
            import core.stdc.stdio : snprintf;

            snprintf(errBuf.ptr, errBuf.length, "WSA error %d", WSAGetLastError());
            return errBuf.ptr;
        } else {
            return strerror(errno);
        }
    }
}

unittest {
    tcp_init();
    assert(tcp_strerror() !is null);
}
