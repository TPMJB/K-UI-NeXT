/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the real server's closing-data-socket policy without a card image
 * or TCP timing model. Unused server functions are discarded by the linker. */
#include "../src/apps/ftp_server.c"
#include <assert.h>

static struct server server;
static struct kui_wifi_session wifi_server;
static struct kui_ftp_status status;
static struct {
    uint8_t state;
    bool readable, closable;
    unsigned reads, closes;
} sockets[DATA_SOCKETS];
static unsigned cleanup_logs;
static char cleanup_log[160];

static bool socket_state(void *ctx, unsigned s, uint8_t *state) {
    assert(ctx == &server && s < DATA_SOCKETS);
    ++sockets[s].reads;
    /* A failed read can leave zero in the output: it is not CLOSED evidence. */
    *state = sockets[s].readable ? sockets[s].state : KUI_W5500_CLOSED;
    return sockets[s].readable;
}
static bool socket_close(void *ctx, unsigned s) {
    assert(ctx == &server && s < DATA_SOCKETS);
    ++sockets[s].closes;
    return sockets[s].closable;
}
static void log_line(const char *format, ...) {
    char line[160];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if(strstr(line, "closed from")) {
        ++cleanup_logs;
        snprintf(cleanup_log, sizeof(cleanup_log), "%s", line);
    }
}
static void reset(void) {
    memset(&server, 0, sizeof(server));
    memset(&status, 0, sizeof(status));
    memset(sockets, 0, sizeof(sockets));
    server.status = &status;
    server.net.ctx = &server;
    server.net.state = socket_state;
    server.net.close = socket_close;
    server.log = log_line;
    cleanup_logs = 0;
    cleanup_log[0] = 0;
    for(unsigned j = 0; j < DATA_SOCKETS; ++j) {
        server.data_owner[j] = FREE;
        server.data_since[j] = 1000;
        server.data_close_state[j] = 0xff;
        sockets[j].state = KUI_W5500_TIME_WAIT;
        sockets[j].readable = sockets[j].closable = true;
    }
}
int main(void) {
    /* Close promptly after FIN exchange; do not log or close it every poll. */
    reset();
    server.data_draining[0] = true;
    data_poll(&server, 1125);
    assert(!server.data_draining[0] && sockets[0].closes == 1);
    assert(cleanup_logs == 1 && strstr(cleanup_log, "1B") &&
           strstr(cleanup_log, "125 ms") && strstr(cleanup_log, "TIME_WAIT"));
    data_poll(&server, 1225);
    assert(sockets[0].reads == 1 && sockets[0].closes == 1 && cleanup_logs == 1);

    /* Failed status/close I/O must retain responsibility for the socket. */
    reset();
    server.data_draining[0] = true;
    sockets[0].readable = false;
    data_poll(&server, 1125);
    assert(server.data_draining[0] && !sockets[0].closes && !cleanup_logs);
    sockets[0].readable = true;
    sockets[0].closable = false;
    data_poll(&server, 1225);
    assert(server.data_draining[0] && sockets[0].closes == 1 && !cleanup_logs);
    sockets[0].closable = true;
    data_poll(&server, 1325);
    assert(!server.data_draining[0] && sockets[0].closes == 2 && cleanup_logs == 1);

    /* Neither a live owner nor an idle, non-draining socket may be touched. */
    reset();
    server.data_draining[0] = server.data_draining[1] = true;
    server.data_owner[0] = 0;
    server.data_owner[1] = RENEWING;
    data_poll(&server, 1000 + DRAIN_MS + 1);
    for(unsigned j = 0; j < DATA_SOCKETS; ++j)
        assert(!sockets[j].reads && !sockets[j].closes);

    /* A stuck CLOSING connection gets 250 ms from its observed transition,
     * even if it spent several seconds in FIN_WAIT after release. */
    reset();
    server.data_draining[0] = true;
    sockets[0].state = KUI_W5500_FIN_WAIT;
    data_poll(&server, 1000);
    sockets[0].state = KUI_W5500_CLOSING;
    data_poll(&server, 6000);
    assert(server.data_draining[0] && !sockets[0].closes);
    assert(server.data_close_since[0] == 6000);
    data_poll(&server, 6249);
    assert(server.data_draining[0] && !sockets[0].closes && !cleanup_logs);
    data_poll(&server, 6250);
    assert(!server.data_draining[0] && sockets[0].closes == 1 && cleanup_logs == 1);
    assert(strstr(cleanup_log, "1A") && strstr(cleanup_log, "CLOSING timeout"));
    data_poll(&server, 6350);
    assert(sockets[0].closes == 1 && cleanup_logs == 1);

    /* The Wi-Fi bridge's 18 state represents a graceful close still in
     * progress; it does not share the W5500's observed 1A closing storm. */
    reset();
    server.wifi = &wifi_server;
    server.data_draining[0] = true;
    sockets[0].state = KUI_NET_CLOSING;
    data_poll(&server, 1000);
    data_poll(&server, 1000 + CLOSING_MS);
    assert(server.data_draining[0] && !sockets[0].closes);
    data_poll(&server, 1000 + DRAIN_MS + 1);
    assert(!server.data_draining[0] && sockets[0].closes == 1);

    /* Failed reads cannot start the CLOSING clock. Once it starts, failed
     * reads or closes cannot reset the clock or discard cleanup ownership. */
    reset();
    server.data_draining[0] = true;
    sockets[0].state = KUI_W5500_CLOSING;
    sockets[0].readable = false;
    data_poll(&server, 1000);
    data_poll(&server, 2000);
    assert(server.data_close_state[0] == 0xff && !sockets[0].closes);
    sockets[0].readable = true;
    data_poll(&server, 3000);
    assert(server.data_close_since[0] == 3000 && !sockets[0].closes);
    data_poll(&server, 3249);
    assert(server.data_draining[0] && !sockets[0].closes);
    sockets[0].readable = false;
    data_poll(&server, 3250);
    assert(server.data_draining[0] && !sockets[0].closes);
    sockets[0].readable = true;
    sockets[0].closable = false;
    data_poll(&server, 3251);
    assert(server.data_draining[0] && sockets[0].closes == 1 && !cleanup_logs);
    assert(server.data_close_since[0] == 3000);
    sockets[0].closable = true;
    data_poll(&server, 3252);
    assert(!server.data_draining[0] && sockets[0].closes == 2 && cleanup_logs == 1);

    /* A normally advancing CLOSING connection still closes immediately on
     * TIME_WAIT, without waiting for the CLOSING timeout. */
    reset();
    server.data_draining[0] = true;
    sockets[0].state = KUI_W5500_CLOSING;
    data_poll(&server, 1000);
    assert(!sockets[0].closes);
    sockets[0].state = KUI_W5500_TIME_WAIT;
    data_poll(&server, 1001);
    assert(!server.data_draining[0] && sockets[0].closes == 1 && cleanup_logs == 1);
    assert(strstr(cleanup_log, "1B") && strstr(cleanup_log, "TIME_WAIT cleanup"));

    /* FIN_WAIT/LAST_ACK still need the peer's FIN/ACK; keep the old deadline. */
    reset();
    server.data_draining[0] = server.data_draining[1] = server.data_draining[2] = true;
    sockets[0].state = KUI_W5500_FIN_WAIT;
    sockets[1].state = KUI_W5500_LAST_ACK;
    sockets[2].state = KUI_W5500_CLOSED;
    data_poll(&server, 1000 + DRAIN_MS);
    assert(server.data_draining[0] && server.data_draining[1] && !server.data_draining[2]);
    for(unsigned j = 0; j < DATA_SOCKETS; ++j) assert(!sockets[j].closes);
    sockets[0].readable = false;
    sockets[1].closable = false;
    data_poll(&server, 1000 + DRAIN_MS + 1);
    assert(server.data_draining[0] && !sockets[0].closes);
    assert(server.data_draining[1] && sockets[1].closes == 1);
    sockets[0].readable = sockets[1].closable = true;
    data_poll(&server, 1000 + DRAIN_MS + 2);
    assert(!server.data_draining[0] && !server.data_draining[1]);
    assert(sockets[0].closes == 1 && sockets[1].closes == 2);

    /* A socket cannot be reassigned if forced cleanup failed. */
    reset();
    for(unsigned j = 0; j < DATA_SOCKETS; ++j) {
        server.data_draining[j] = true;
        sockets[j].closable = false;
    }
    assert(data_take(&server, &server.sessions[0], 2500) == -1);
    for(unsigned j = 0; j < DATA_SOCKETS; ++j)
        assert(server.data_owner[j] == FREE && server.data_draining[j]);

    /* Pressure from a fourth transfer cannot bypass Wi-Fi's closing grace;
     * the board's TCP stack may still be delivering the retired payload. */
    reset();
    server.wifi = &wifi_server;
    for(unsigned j = 0; j < DATA_SOCKETS; ++j) server.data_draining[j] = true;
    assert(data_take(&server, &server.sessions[0], 2500) == -1);
    assert(data_take(&server, &server.sessions[0], 1000 + DRAIN_MS - 1) == -1);
    for(unsigned j = 0; j < DATA_SOCKETS; ++j)
        assert(server.data_draining[j] && !sockets[j].closes);
    assert(data_take(&server, &server.sessions[0], 1000 + DRAIN_MS) == 0);
    assert(!server.data_draining[0] && server.data_owner[0] == 0 && sockets[0].closes == 1);

    /* A full outgoing queue must not abandon an aborted data listener.
     * The later poll retries the close before the slot can be reused. */
    reset();
    server.wifi = &wifi_server;
    server.sessions[0].data = 0;
    server.data_owner[0] = 0;
    sockets[0].closable = false;
    data_release(&server, &server.sessions[0], false, 1000);
    assert(server.sessions[0].data == -1 && server.data_owner[0] == FREE);
    assert(server.data_draining[0] && server.data_aborting[0] && sockets[0].closes == 1);
    data_poll(&server, 1001);
    assert(server.data_draining[0] && server.data_aborting[0] && sockets[0].closes == 2);
    sockets[0].closable = true;
    data_poll(&server, 1002);
    assert(!server.data_draining[0] && !server.data_aborting[0] && sockets[0].closes == 3);
    puts("FTP closing data sockets: ok");
    return 0;
}
