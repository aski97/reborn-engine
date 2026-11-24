/*
 * Local TCP sockets, Win32 (ADR 0022): Winsock 2, the Berkeley sockets API
 * with a startup call and its own error codes.
 */
#include "platform/platform_socket.h"
#include "platform/win32/win32_error.h"

#include <reborn/assert.h>
#include <reborn/log.h>

/* winsock2.h before windows.h: without WIN32_LEAN_AND_MEAN, windows.h pulls in the old winsock.h.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <limits.h>

/* Pending connections the system queues while the server serves another one. */
#define WIN32_SOCKET_BACKLOG 4

static SOCKET win32_socket_of(rb_socket socket) {
    return (SOCKET)socket;
}

/* INVALID_SOCKET is ~0, the same bits as RB_SOCKET_INVALID: the widening keeps it. */
static rb_socket win32_socket_wrap(SOCKET socket) {
    return socket == INVALID_SOCKET ? RB_SOCKET_INVALID : (rb_socket)socket;
}

bool rb_socket_startup(void) {
    WSADATA data;
    /* Returns the error itself: WSAGetLastError needs a started Winsock. */
    int error = WSAStartup(MAKEWORD(2, 2), &data);
    if (error != 0) {
        WIN32_LOG_ERROR("WSAStartup", (u32)error);
        return false;
    }
    return true;
}

void rb_socket_shutdown(void) {
    WSACleanup();
}

/*
 * Nagle's algorithm holds small writes back until the previous one is
 * acknowledged, and the other side delays its acknowledgements: a short reply
 * line can wait tens of milliseconds. Each line is one whole message: send it now.
 */
static void win32_socket_no_delay(SOCKET socket) {
    BOOL on = TRUE;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on));
}

static struct sockaddr_in win32_socket_loopback(u16 port) {
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    /* Network byte order (big-endian) for both, whatever the CPU's. */
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    return address;
}

rb_socket rb_socket_listen(u16 port) {
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        WIN32_LOG_ERROR("socket", (u32)WSAGetLastError());
        return RB_SOCKET_INVALID;
    }
    /*
     * On Windows SO_REUSEADDR lets another process bind the same port and steal
     * its connections. SO_EXCLUSIVEADDRUSE forbids that, and a restarted game
     * still gets its port while the old connections linger in TIME_WAIT.
     */
    BOOL on = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&on, sizeof(on));
    struct sockaddr_in address = win32_socket_loopback(port);
    u_long             non_blocking = 1;
    if (bind(listener, (const struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        WIN32_LOG_ERROR("bind to 127.0.0.1", (u32)WSAGetLastError());
    } else if (listen(listener, WIN32_SOCKET_BACKLOG) == SOCKET_ERROR) {
        WIN32_LOG_ERROR("listen", (u32)WSAGetLastError());
    } else if (ioctlsocket(listener, FIONBIO, &non_blocking) == SOCKET_ERROR) {
        WIN32_LOG_ERROR("ioctlsocket(FIONBIO)", (u32)WSAGetLastError());
    } else {
        return win32_socket_wrap(listener);
    }
    closesocket(listener);
    return RB_SOCKET_INVALID;
}

u16 rb_socket_local_port(rb_socket socket) {
    struct sockaddr_in address = {0};
    int                size = sizeof(address);
    if (getsockname(win32_socket_of(socket), (struct sockaddr *)&address, &size) == SOCKET_ERROR) {
        WIN32_LOG_ERROR("getsockname", (u32)WSAGetLastError());
        return 0;
    }
    return ntohs(address.sin_port);
}

rb_socket rb_socket_accept(rb_socket listener) {
    /* A socket accepted from a non-blocking listener is non-blocking too, on Windows. */
    SOCKET client = accept(win32_socket_of(listener), NULL, NULL);
    if (client == INVALID_SOCKET) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            WIN32_LOG_ERROR("accept", (u32)error);
        }
        return RB_SOCKET_INVALID;
    }
    win32_socket_no_delay(client);
    return win32_socket_wrap(client);
}

rb_socket rb_socket_connect(u16 port) {
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET) {
        WIN32_LOG_ERROR("socket", (u32)WSAGetLastError());
        return RB_SOCKET_INVALID;
    }
    struct sockaddr_in address = win32_socket_loopback(port);
    if (connect(client, (const struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        WIN32_LOG_ERROR("connect to 127.0.0.1", (u32)WSAGetLastError());
        closesocket(client);
        return RB_SOCKET_INVALID;
    }
    win32_socket_no_delay(client);
    return win32_socket_wrap(client);
}

rb_socket_result rb_socket_receive(rb_socket socket, void *buffer, usize capacity,
                                   usize *received) {
    RB_ASSERT(buffer != NULL && received != NULL && capacity > 0);
    *received = 0;
    int count = capacity > INT_MAX ? INT_MAX : (int)capacity;
    int result = recv(win32_socket_of(socket), buffer, count, 0);
    if (result > 0) {
        *received = (usize)result;
        return RB_SOCKET_OK;
    }
    if (result == 0) {
        return RB_SOCKET_CLOSED;
    }
    int error = WSAGetLastError();
    if (error == WSAEWOULDBLOCK) {
        return RB_SOCKET_NO_DATA;
    }
    /* A client killed without closing: as good as closed. */
    if (error == WSAECONNRESET || error == WSAECONNABORTED) {
        return RB_SOCKET_CLOSED;
    }
    WIN32_LOG_ERROR("recv", (u32)error);
    return RB_SOCKET_ERROR;
}

static bool win32_socket_wait(rb_socket socket, SHORT events, u32 timeout_ms) {
    WSAPOLLFD entry = {.fd = win32_socket_of(socket), .events = events};
    int       timeout = timeout_ms > INT_MAX ? INT_MAX : (int)timeout_ms;
    int       result = WSAPoll(&entry, 1, timeout);
    if (result == SOCKET_ERROR) {
        WIN32_LOG_ERROR("WSAPoll", (u32)WSAGetLastError());
        return false;
    }
    return result > 0;
}

bool rb_socket_wait_readable(rb_socket socket, u32 timeout_ms) {
    return win32_socket_wait(socket, POLLRDNORM, timeout_ms);
}

bool rb_socket_send_all(rb_socket socket, const void *data, usize size, u32 timeout_ms) {
    RB_ASSERT(data != NULL || size == 0);
    const char *bytes = data;
    while (size > 0) {
        int count = size > INT_MAX ? INT_MAX : (int)size;
        int sent = send(win32_socket_of(socket), bytes, count, 0);
        if (sent == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                WIN32_LOG_ERROR("send", (u32)error);
                return false;
            }
            /* The system's send buffer is full: the other end is not reading. */
            if (!win32_socket_wait(socket, POLLWRNORM, timeout_ms)) {
                RB_LOG_ERROR("send: the other end has not read for %u ms", timeout_ms);
                return false;
            }
            continue;
        }
        bytes += sent;
        size -= (usize)sent;
    }
    return true;
}

void rb_socket_close(rb_socket socket) {
    if (socket != RB_SOCKET_INVALID) {
        closesocket(win32_socket_of(socket));
    }
}
