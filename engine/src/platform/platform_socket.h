/*
 * TCP sockets on the local machine only (ADR 0022): what the command server
 * and the reborn-cmd client need, nothing more. Stateless OS services like
 * platform_os.h, implemented by win32/win32_socket.c and linux/linux_socket.c.
 *
 * A socket is one end of a byte stream between two programs, named by an
 * address and a port. 127.0.0.1 (loopback) never leaves the machine.
 */
#ifndef RB_PLATFORM_SOCKET_H
#define RB_PLATFORM_SOCKET_H

#include <reborn/defines.h>

/* A SOCKET on Windows, a file descriptor on Linux, widened to one type. */
typedef u64 rb_socket;

#define RB_SOCKET_INVALID UINT64_MAX

/*
 * Before any other call: loads Winsock on Windows (reference counted, so each
 * startup needs its shutdown), nothing on Linux. False on failure (logged).
 */
bool rb_socket_startup(void);
void rb_socket_shutdown(void);

/*
 * A listening socket on 127.0.0.1:`port`, non-blocking; port 0 lets the system
 * choose (rb_socket_local_port tells which). RB_SOCKET_INVALID on failure
 * (logged): most often the port is taken.
 */
rb_socket rb_socket_listen(u16 port);

/* The port `socket` is bound to; 0 on failure. */
u16 rb_socket_local_port(rb_socket socket);

/* A waiting connection, made non-blocking; RB_SOCKET_INVALID when none is waiting. */
rb_socket rb_socket_accept(rb_socket listener);

/* A blocking connection to 127.0.0.1:`port`. RB_SOCKET_INVALID on failure (logged). */
rb_socket rb_socket_connect(u16 port);

typedef enum rb_socket_result {
    RB_SOCKET_OK,      /* `received` bytes, at least 1 */
    RB_SOCKET_NO_DATA, /* non-blocking and nothing arrived: try again later */
    RB_SOCKET_CLOSED,  /* the other end closed the connection */
    RB_SOCKET_ERROR,   /* the connection is broken (logged) */
} rb_socket_result;

rb_socket_result rb_socket_receive(rb_socket socket, void *buffer, usize capacity, usize *received);

/*
 * Waits up to `timeout_ms` for data (or a close) to read. False on timeout or
 * error.
 */
bool rb_socket_wait_readable(rb_socket socket, u32 timeout_ms);

/*
 * All `size` bytes, waiting up to `timeout_ms` whenever the other end does not
 * read: a stuck client cannot freeze the game. False on timeout or error
 * (logged); the connection should then be closed.
 */
bool rb_socket_send_all(rb_socket socket, const void *data, usize size, u32 timeout_ms);

void rb_socket_close(rb_socket socket);

#endif /* RB_PLATFORM_SOCKET_H */
