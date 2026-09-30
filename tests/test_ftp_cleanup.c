/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the real server's closing-data-socket policy without a card image
 * or TCP timing model. Unused server functions are discarded by the linker. */
#include "../src/apps/ftp_server.c"
#include <assert.h>

static struct server server;
static struct kui_ftp_status status;
static struct {
    uint8_t state;
    bool readable, closable;
    unsigned reads, closes;
} sockets[DATA_SOCKETS];
static unsigned cleanup_logs;
static char cleanup_log[160];

bool kui_w5500_status(struct kui_w5500 *w, unsigned s, uint8_t *state) {
    assert(w == &server.net.chip && s < DATA_SOCKETS);
    ++sockets[s].reads;
    /* A failed read can leave zero in the output: it is not CLOSED evidence. */
    *state = sockets[s].readable ? sockets[s].state : KUI_W5500_CLOSED;
    return sockets[s].readable;
}
bool kui_w5500_close(struct kui_w5500 *w, unsigned s) {
    assert(w == &server.net.chip && s < DATA_SOCKETS);
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

    /* FIN_WAIT/CLOSING still need the peer's FIN/ACK; keep the old deadline. */
    reset();
    server.data_draining[0] = server.data_draining[1] = server.data_draining[2] = true;
    sockets[0].state = KUI_W5500_FIN_WAIT;
    sockets[1].state = KUI_W5500_CLOSING;
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
    puts("FTP closing data sockets: ok");
    return 0;
}
