/*
 * reborn-cmd: sends command lines to a game started with --listen and prints
 * each reply (ADR 0022). Made for agents and scripts: one process per call,
 * replies on stdout as JSON lines, the outcome in the exit code.
 *
 *   reborn-cmd 7777 "pause" "blink.state"
 *
 * Exit code: 0 when every command succeeded, 1 when one failed, 2 when the
 * game cannot be reached or stops answering.
 */
#include <reborn/command.h>
#include <reborn/log.h>

#include <stdio.h>
#include <string.h>

#include "platform/platform_socket.h"

/* A reply may come after a long step command: generous, but not forever. */
#define CMD_REPLY_TIMEOUT_MS 60000u

#define CMD_EXIT_OK          0
#define CMD_EXIT_FAILED      1
#define CMD_EXIT_UNREACHABLE 2

static int cmd_usage(void) {
    fputs("Usage: reborn-cmd <port> <command line>...\n"
          "Sends each line to the game listening on 127.0.0.1:<port> (--listen) and prints\n"
          "its JSON reply. Exit code: 0 all succeeded, 1 one failed, 2 no game or no reply.\n",
          stderr);
    return CMD_EXIT_UNREACHABLE;
}

static bool cmd_parse_port(const char *text, u16 *port) {
    u32 value = 0;
    if (*text == '\0' || strlen(text) > 5) {
        return false;
    }
    for (const char *c = text; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9') {
            return false;
        }
        value = value * 10 + (u32)(*c - '0');
    }
    if (value == 0 || value > 65535) {
        return false;
    }
    *port = (u16)value;
    return true;
}

/*
 * Reads up to the reply's '\n' into `reply`. Replies arrive in order, one per
 * line sent: what follows the '\n' belongs to the next one and stays in `pending`.
 */
typedef struct cmd_reader {
    char  pending[RB_COMMAND_REPLY_MAX];
    usize pending_length;
} cmd_reader;

static bool cmd_read_reply(rb_socket socket, cmd_reader *reader, char *reply) {
    for (;;) {
        char *newline = memchr(reader->pending, '\n', reader->pending_length);
        if (newline != NULL) {
            usize length = (usize)(newline - reader->pending) + 1;
            memcpy(reply, reader->pending, length);
            reply[length] = '\0';
            reader->pending_length -= length;
            memmove(reader->pending, newline + 1, reader->pending_length);
            return true;
        }
        if (reader->pending_length == sizeof(reader->pending)) {
            RB_LOG_ERROR("Reply longer than %u bytes", RB_COMMAND_REPLY_MAX);
            return false;
        }
        if (!rb_socket_wait_readable(socket, CMD_REPLY_TIMEOUT_MS)) {
            RB_LOG_ERROR("No reply within %u s", CMD_REPLY_TIMEOUT_MS / 1000u);
            return false;
        }
        usize            received = 0;
        rb_socket_result result =
            rb_socket_receive(socket, reader->pending + reader->pending_length,
                              sizeof(reader->pending) - reader->pending_length, &received);
        if (result != RB_SOCKET_OK) {
            RB_LOG_ERROR("The game closed the connection before replying");
            return false;
        }
        reader->pending_length += received;
    }
}

/* The engine writes "ok" second, after the id, always: no JSON parser needed (ADR 0022). */
static bool cmd_reply_succeeded(const char *reply) {
    const char *ok = strstr(reply, ",\"ok\":");
    return ok != NULL && strncmp(ok + 6, "true", 4) == 0;
}

int main(int argc, char **argv) {
    u16 port = 0;
    if (argc < 3 || !cmd_parse_port(argv[1], &port)) {
        return cmd_usage();
    }
    if (!rb_socket_startup()) {
        return CMD_EXIT_UNREACHABLE;
    }
    rb_socket socket = rb_socket_connect(port);
    if (socket == RB_SOCKET_INVALID) {
        RB_LOG_ERROR("No game listening on 127.0.0.1:%u (started with --listen %u?)",
                     (unsigned)port, (unsigned)port);
        rb_socket_shutdown();
        return CMD_EXIT_UNREACHABLE;
    }

    /* Static: two 16 KiB buffers are better off the stack. */
    static cmd_reader reader;
    static char       reply[RB_COMMAND_REPLY_MAX + 1];
    int               code = CMD_EXIT_OK;
    for (int i = 2; i < argc; ++i) {
        const char *line = argv[i];
        if (strchr(line, '\n') != NULL) {
            RB_LOG_ERROR("Argument %d holds a line break: one command per argument", i - 1);
            code = CMD_EXIT_FAILED;
            continue;
        }
        if (!rb_socket_send_all(socket, line, strlen(line), CMD_REPLY_TIMEOUT_MS) ||
            !rb_socket_send_all(socket, "\n", 1, CMD_REPLY_TIMEOUT_MS)) {
            code = CMD_EXIT_UNREACHABLE;
            break;
        }
        /* An empty line or a comment gets no reply: nothing to wait for. */
        const char *first = line + strspn(line, " \t");
        if (*first == '\0' || *first == '#') {
            continue;
        }
        if (!cmd_read_reply(socket, &reader, reply)) {
            code = CMD_EXIT_UNREACHABLE;
            break;
        }
        fputs(reply, stdout);
        fflush(stdout);
        if (!cmd_reply_succeeded(reply) && code == CMD_EXIT_OK) {
            code = CMD_EXIT_FAILED;
        }
    }
    rb_socket_close(socket);
    rb_socket_shutdown();
    return code;
}
