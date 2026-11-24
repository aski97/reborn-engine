/*
 * Local TCP sockets, Linux (ADR 0022). POSIX sockets, not X11-specific.
 */

/* Sockets, poll and fcntl are POSIX, hidden by -std=c17 unless asked. */
#define _POSIX_C_SOURCE 200809L

#include "platform/platform_socket.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Pending connections the system queues while the server serves another one. */
#define LINUX_SOCKET_BACKLOG 4

static int linux_socket_of(rb_socket socket) {
    return (int)socket;
}

bool rb_socket_startup(void) {
    return true;
}

void rb_socket_shutdown(void) {}

/*
 * Nagle's algorithm holds small writes back until the previous one is
 * acknowledged, and the other side delays its acknowledgements: a short reply
 * line can wait tens of milliseconds. Each line is one whole message: send it now.
 */
static void linux_socket_no_delay(int fd) {
    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

static bool linux_socket_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        RB_LOG_ERROR("fcntl(O_NONBLOCK) failed: %s", strerror(errno));
        return false;
    }
    return true;
}

static struct sockaddr_in linux_socket_loopback(u16 port) {
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    /* Network byte order (big-endian) for both, whatever the CPU's. */
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    return address;
}

rb_socket rb_socket_listen(u16 port) {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        RB_LOG_ERROR("socket failed: %s", strerror(errno));
        return RB_SOCKET_INVALID;
    }
    /*
     * Without it, a restarted game cannot bind its port while the previous
     * run's connections linger in TIME_WAIT (up to a minute). On Linux it does
     * not let two live sockets share the port.
     */
    int on = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in address = linux_socket_loopback(port);
    if (bind(listener, (const struct sockaddr *)&address, sizeof(address)) < 0) {
        RB_LOG_ERROR("bind to 127.0.0.1:%u failed: %s", (unsigned)port, strerror(errno));
    } else if (listen(listener, LINUX_SOCKET_BACKLOG) < 0) {
        RB_LOG_ERROR("listen failed: %s", strerror(errno));
    } else if (linux_socket_non_blocking(listener)) {
        return (rb_socket)listener;
    }
    close(listener);
    return RB_SOCKET_INVALID;
}

u16 rb_socket_local_port(rb_socket socket) {
    struct sockaddr_in address;
    socklen_t          size = sizeof(address);
    memset(&address, 0, sizeof(address));
    if (getsockname(linux_socket_of(socket), (struct sockaddr *)&address, &size) < 0) {
        RB_LOG_ERROR("getsockname failed: %s", strerror(errno));
        return 0;
    }
    return ntohs(address.sin_port);
}

rb_socket rb_socket_accept(rb_socket listener) {
    int client = accept(linux_socket_of(listener), NULL, NULL);
    if (client < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            RB_LOG_ERROR("accept failed: %s", strerror(errno));
        }
        return RB_SOCKET_INVALID;
    }
    /* Unlike Windows, Linux does not pass the listener's O_NONBLOCK on to accepted sockets. */
    if (!linux_socket_non_blocking(client)) {
        close(client);
        return RB_SOCKET_INVALID;
    }
    linux_socket_no_delay(client);
    return (rb_socket)client;
}

rb_socket rb_socket_connect(u16 port) {
    int client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) {
        RB_LOG_ERROR("socket failed: %s", strerror(errno));
        return RB_SOCKET_INVALID;
    }
    struct sockaddr_in address = linux_socket_loopback(port);
    if (connect(client, (const struct sockaddr *)&address, sizeof(address)) < 0) {
        RB_LOG_ERROR("connect to 127.0.0.1:%u failed: %s", (unsigned)port, strerror(errno));
        close(client);
        return RB_SOCKET_INVALID;
    }
    linux_socket_no_delay(client);
    return (rb_socket)client;
}

rb_socket_result rb_socket_receive(rb_socket socket, void *buffer, usize capacity,
                                   usize *received) {
    RB_ASSERT(buffer != NULL && received != NULL && capacity > 0);
    *received = 0;
    for (;;) {
        ssize_t result = recv(linux_socket_of(socket), buffer, capacity, 0);
        if (result > 0) {
            *received = (usize)result;
            return RB_SOCKET_OK;
        }
        if (result == 0) {
            return RB_SOCKET_CLOSED;
        }
        /* A signal arrived during the call: nothing was read, ask again. */
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return RB_SOCKET_NO_DATA;
        }
        /* A client killed without closing: as good as closed. */
        if (errno == ECONNRESET) {
            return RB_SOCKET_CLOSED;
        }
        RB_LOG_ERROR("recv failed: %s", strerror(errno));
        return RB_SOCKET_ERROR;
    }
}

static bool linux_socket_wait(rb_socket socket, short events, u32 timeout_ms) {
    struct pollfd entry = {.fd = linux_socket_of(socket), .events = events};
    int           timeout = timeout_ms > 0x7fffffffu ? 0x7fffffff : (int)timeout_ms;
    int           result;
    do {
        result = poll(&entry, 1, timeout);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        RB_LOG_ERROR("poll failed: %s", strerror(errno));
        return false;
    }
    return result > 0;
}

bool rb_socket_wait_readable(rb_socket socket, u32 timeout_ms) {
    return linux_socket_wait(socket, POLLIN, timeout_ms);
}

bool rb_socket_send_all(rb_socket socket, const void *data, usize size, u32 timeout_ms) {
    RB_ASSERT(data != NULL || size == 0);
    const char *bytes = data;
    while (size > 0) {
        /*
         * MSG_NOSIGNAL: writing to a connection the other end closed raises
         * SIGPIPE, whose default action kills the process. With it, send fails
         * with EPIPE instead and the game keeps running.
         */
        ssize_t sent = send(linux_socket_of(socket), bytes, size, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                RB_LOG_ERROR("send failed: %s", strerror(errno));
                return false;
            }
            /* The system's send buffer is full: the other end is not reading. */
            if (!linux_socket_wait(socket, POLLOUT, timeout_ms)) {
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
        close(linux_socket_of(socket));
    }
}
