/* Command server over local TCP (ADR 0022). */
#include "command/command_server.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <string.h>

bool rb_command_server_start(rb_command_server *server, u16 port) {
    RB_ASSERT(server != NULL);
    *server = (rb_command_server){.listener = RB_SOCKET_INVALID, .client = RB_SOCKET_INVALID};
    if (!rb_socket_startup()) {
        return false;
    }
    server->listener = rb_socket_listen(port);
    if (server->listener == RB_SOCKET_INVALID) {
        RB_LOG_ERROR("Commands: cannot listen on 127.0.0.1:%u", (unsigned)port);
        rb_socket_shutdown();
        return false;
    }
    server->port = rb_socket_local_port(server->listener);
    RB_LOG_INFO("Commands: listening on 127.0.0.1:%u", (unsigned)server->port);
    return true;
}

static void server_disconnect(rb_command_server *server) {
    rb_socket_close(server->client);
    server->client = RB_SOCKET_INVALID;
    /* A partial line belongs to the client that sent it. */
    server->input_length = 0;
    server->discarding = false;
}

void rb_command_server_stop(rb_command_server *server) {
    RB_ASSERT(server != NULL);
    if (server->listener == RB_SOCKET_INVALID) {
        return;
    }
    server_disconnect(server);
    rb_socket_close(server->listener);
    server->listener = RB_SOCKET_INVALID;
    rb_socket_shutdown();
}

/*
 * The first line in `input` into `line`, shifted out of the buffer. A line is
 * at most RB_COMMAND_LINE_MAX bytes: the buffer holds one plus its '\n'.
 */
static bool server_take_line(rb_command_server *server, char *line) {
    char *newline = memchr(server->input, '\n', server->input_length);
    if (newline == NULL) {
        return false;
    }
    usize length = (usize)(newline - server->input);
    usize rest = server->input_length - length - 1;
    bool  discarded = server->discarding;
    if (!discarded) {
        memcpy(line, server->input, length);
        if (length > 0 && line[length - 1] == '\r') {
            --length;
        }
        line[length] = '\0';
    }
    memmove(server->input, newline + 1, rest);
    server->input_length = rest;
    server->discarding = false;
    /* The tail of a line already reported as too long: not a line of its own. */
    return !discarded;
}

rb_command_server_next rb_command_server_next_line(rb_command_server *server, char *line) {
    RB_ASSERT(server != NULL && line != NULL);
    if (server->listener == RB_SOCKET_INVALID) {
        return RB_COMMAND_SERVER_NONE;
    }
    if (server->client == RB_SOCKET_INVALID) {
        server->client = rb_socket_accept(server->listener);
        if (server->client == RB_SOCKET_INVALID) {
            return RB_COMMAND_SERVER_NONE;
        }
        RB_LOG_DEBUG("Commands: client connected");
    }
    for (;;) {
        if (server_take_line(server, line)) {
            return RB_COMMAND_SERVER_LINE;
        }
        /* Not a single line fits in the buffer: report it once, then skip it. */
        if (server->input_length == sizeof(server->input)) {
            bool first = !server->discarding;
            server->input_length = 0;
            server->discarding = true;
            if (first) {
                return RB_COMMAND_SERVER_TOO_LONG;
            }
        }
        usize            received = 0;
        rb_socket_result result =
            rb_socket_receive(server->client, server->input + server->input_length,
                              sizeof(server->input) - server->input_length, &received);
        if (result == RB_SOCKET_OK) {
            server->input_length += received;
            continue;
        }
        if (result != RB_SOCKET_NO_DATA) {
            RB_LOG_DEBUG("Commands: client disconnected");
            server_disconnect(server);
        }
        return RB_COMMAND_SERVER_NONE;
    }
}

void rb_command_server_reply(rb_command_server *server, const char *reply) {
    RB_ASSERT(server != NULL && reply != NULL);
    if (server->client == RB_SOCKET_INVALID) {
        return;
    }
    if (!rb_socket_send_all(server->client, reply, strlen(reply),
                            RB_COMMAND_SERVER_SEND_TIMEOUT_MS)) {
        RB_LOG_WARN("Commands: reply not delivered, client disconnected");
        server_disconnect(server);
    }
}
