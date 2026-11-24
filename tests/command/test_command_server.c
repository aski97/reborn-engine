/*
 * The command server (ADR 0022) over real loopback sockets: no window, so it
 * runs in the Linux container too. Port 0: the system picks a free one, so
 * tests never collide with a game or with each other.
 */
#include "rb_test.h"

#include <string.h>

#include "command/command_server.h"

/* Generous for loopback, where data arrives in microseconds. */
#define TEST_WAIT_MS 2000u

typedef struct server_fixture {
    rb_command_server server;
    rb_socket         client;
} server_fixture;

static bool fixture_start(server_fixture *f) {
    f->client = RB_SOCKET_INVALID;
    if (!rb_command_server_start(&f->server, 0)) {
        return false;
    }
    if (!rb_socket_startup()) {
        rb_command_server_stop(&f->server);
        return false;
    }
    f->client = rb_socket_connect(f->server.port);
    return f->client != RB_SOCKET_INVALID;
}

static void fixture_stop(server_fixture *f) {
    rb_socket_close(f->client);
    rb_socket_shutdown();
    rb_command_server_stop(&f->server);
}

static bool client_send(server_fixture *f, const char *text) {
    return rb_socket_send_all(f->client, text, strlen(text), TEST_WAIT_MS);
}

/* Polls like the game does once per frame, until a line comes or time runs out. */
static rb_command_server_next server_wait_line(server_fixture *f, char *line) {
    for (u32 i = 0; i < TEST_WAIT_MS; ++i) {
        rb_command_server_next next = rb_command_server_next_line(&f->server, line);
        if (next != RB_COMMAND_SERVER_NONE) {
            return next;
        }
        /* Waits for the client's bytes once it is accepted; before that, a short pause. */
        if (f->server.client != RB_SOCKET_INVALID) {
            rb_socket_wait_readable(f->server.client, 1);
        } else {
            rb_socket_wait_readable(f->server.listener, 1);
        }
    }
    return RB_COMMAND_SERVER_NONE;
}

RB_TEST(command_server, listens_on_the_port_the_system_picked) {
    server_fixture f;
    RB_REQUIRE(fixture_start(&f));
    RB_EXPECT(f.server.port != 0);
    fixture_stop(&f);
}

RB_TEST(command_server, lines_arrive_whole_and_in_order) {
    server_fixture f;
    RB_REQUIRE(fixture_start(&f));
    char line[RB_COMMAND_LINE_MAX + 1];
    /* Two lines and the start of a third in one send; the rest later. */
    RB_REQUIRE(client_send(&f, "pause\r\nstep 10\nqu"));
    RB_REQUIRE_EQ_INT(server_wait_line(&f, line), RB_COMMAND_SERVER_LINE);
    RB_EXPECT_EQ_STR(line, "pause");
    RB_REQUIRE_EQ_INT(server_wait_line(&f, line), RB_COMMAND_SERVER_LINE);
    RB_EXPECT_EQ_STR(line, "step 10");
    RB_EXPECT_EQ_INT(rb_command_server_next_line(&f.server, line), RB_COMMAND_SERVER_NONE);
    RB_REQUIRE(client_send(&f, "it\n"));
    RB_REQUIRE_EQ_INT(server_wait_line(&f, line), RB_COMMAND_SERVER_LINE);
    RB_EXPECT_EQ_STR(line, "quit");
    fixture_stop(&f);
}

RB_TEST(command_server, replies_reach_the_client) {
    server_fixture f;
    RB_REQUIRE(fixture_start(&f));
    char line[RB_COMMAND_LINE_MAX + 1];
    RB_REQUIRE(client_send(&f, "status\n"));
    RB_REQUIRE_EQ_INT(server_wait_line(&f, line), RB_COMMAND_SERVER_LINE);
    rb_command_server_reply(&f.server, "{\"id\":1}\n");

    char  reply[64] = {0};
    usize received = 0;
    RB_REQUIRE(rb_socket_wait_readable(f.client, TEST_WAIT_MS));
    RB_REQUIRE_EQ_INT(rb_socket_receive(f.client, reply, sizeof(reply) - 1, &received),
                      RB_SOCKET_OK);
    RB_EXPECT_EQ_STR(reply, "{\"id\":1}\n");
    fixture_stop(&f);
}

RB_TEST(command_server, a_line_too_long_is_reported_once_then_skipped) {
    server_fixture f;
    RB_REQUIRE(fixture_start(&f));
    static char long_line[3000];
    memset(long_line, 'x', sizeof(long_line) - 1);
    char line[RB_COMMAND_LINE_MAX + 1];
    RB_REQUIRE(client_send(&f, long_line));
    RB_REQUIRE(client_send(&f, "\nstatus\n"));
    RB_REQUIRE_EQ_INT(server_wait_line(&f, line), RB_COMMAND_SERVER_TOO_LONG);
    RB_REQUIRE_EQ_INT(server_wait_line(&f, line), RB_COMMAND_SERVER_LINE);
    RB_EXPECT_EQ_STR(line, "status");
    fixture_stop(&f);
}

/* After a client leaves, its half line is dropped and the next client is served. */
RB_TEST(command_server, serves_the_next_client_after_a_disconnect) {
    server_fixture f;
    RB_REQUIRE(fixture_start(&f));
    char line[RB_COMMAND_LINE_MAX + 1];
    RB_REQUIRE(client_send(&f, "status\nhalf"));
    RB_REQUIRE_EQ_INT(server_wait_line(&f, line), RB_COMMAND_SERVER_LINE);
    rb_socket_close(f.client);
    for (u32 i = 0; i < TEST_WAIT_MS && f.server.client != RB_SOCKET_INVALID; ++i) {
        RB_EXPECT_EQ_INT(rb_command_server_next_line(&f.server, line), RB_COMMAND_SERVER_NONE);
        if (f.server.client != RB_SOCKET_INVALID) {
            rb_socket_wait_readable(f.server.client, 1);
        }
    }
    RB_EXPECT(f.server.client == RB_SOCKET_INVALID);

    f.client = rb_socket_connect(f.server.port);
    RB_REQUIRE(f.client != RB_SOCKET_INVALID);
    RB_REQUIRE(client_send(&f, "quit\n"));
    RB_REQUIRE_EQ_INT(server_wait_line(&f, line), RB_COMMAND_SERVER_LINE);
    RB_EXPECT_EQ_STR(line, "quit");
    fixture_stop(&f);
}

RB_TEST(command_server, a_port_in_use_fails) {
    server_fixture f;
    RB_REQUIRE(fixture_start(&f));
    rb_command_server second;
    RB_EXPECT(!rb_command_server_start(&second, f.server.port));
    fixture_stop(&f);
}
