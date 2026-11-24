/*
 * The command server (--listen, ADR 0022): a TCP listener on 127.0.0.1, one
 * client at a time, lines in and reply lines out. Never blocks the game loop
 * while reading: polled once per frame.
 *
 *   rb_command_server_start(&server, port);
 *   char line[RB_COMMAND_LINE_MAX + 1];
 *   while (rb_command_server_next_line(&server, line) == RB_COMMAND_SERVER_LINE) {
 *       rb_command_server_reply(&server, run(line));
 *   }
 */
#ifndef RB_COMMAND_SERVER_H
#define RB_COMMAND_SERVER_H

#include <reborn/command.h>

#include "platform/platform_socket.h"

/* How long a reply waits for a client that does not read before it is dropped. */
#define RB_COMMAND_SERVER_SEND_TIMEOUT_MS 1000u

typedef struct rb_command_server {
    rb_socket listener;
    rb_socket client; /* RB_SOCKET_INVALID while nobody is connected */
    u16       port;
    /* Bytes received and not yet returned as lines. */
    char  input[RB_COMMAND_LINE_MAX + 1];
    usize input_length;
    /* A line was too long: the rest of it is skipped up to its '\n'. */
    bool discarding;
} rb_command_server;

/* Starts the sockets and listens; port 0 lets the system choose. False on failure (logged). */
bool rb_command_server_start(rb_command_server *server, u16 port);

/* Closes the connection and the listener. Safe on a server that did not start. */
void rb_command_server_stop(rb_command_server *server);

typedef enum rb_command_server_next {
    RB_COMMAND_SERVER_NONE,     /* no complete line for now */
    RB_COMMAND_SERVER_LINE,     /* `line` holds one, without its "\r\n" */
    RB_COMMAND_SERVER_TOO_LONG, /* a line longer than RB_COMMAND_LINE_MAX: reply an error */
} rb_command_server_next;

/*
 * Accepts a waiting client if none is connected, reads what arrived, returns
 * the next complete line. `line` has room for RB_COMMAND_LINE_MAX + 1 bytes.
 */
rb_command_server_next rb_command_server_next_line(rb_command_server *server, char *line);

/* Sends a reply to the current client; one that does not read is disconnected. */
void rb_command_server_reply(rb_command_server *server, const char *reply);

#endif /* RB_COMMAND_SERVER_H */
