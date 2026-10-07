/* SPDX-License-Identifier: GPL-3.0-only */
#define _DEFAULT_SOURCE
#include "kui/network_wifi.h"
#include "wifi_model.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* The Wi-Fi board's driver, the Network app's use of it and the Wi-Fi
 * page's jobs, on the model: the firmware's own bridge core behind a
 * simulated SPI bus, with real sockets on this machine. */
const struct kui_wifi_port *kui_wifi_console_port(void) { return &wifi_model_port; }

static bool never(void) { return false; }
static bool line(const struct kui_app_status *s, const char *text) {
    for(unsigned i = 0; i < s->line_count; ++i) if(strstr(s->lines[i], text)) return true;
    return false;
}
static struct kui_wifi_session *session(void) {
    struct kui_wifi_session *s = calloc(1, sizeof(*s));
    assert(s);
    return s;
}
static void start(unsigned select, unsigned bad_rates, uint8_t state) {
    struct wifi_model_options options = {false, select, bad_rates, false, state};
    wifi_model_start(&options);
}

static void finding(void) {
    struct kui_wifi_session *s = session();
    start(0, 0, KWM_WIFI_ONLINE);
    assert(kui_wifi_session_find(s, &wifi_model_port, NULL));
    assert(s->found && s->open && s->level == 0 && !s->bad && s->ready_changes && !s->ready_timeouts);
    assert(!strcmp(s->host.hello.version, "model-1") && s->host.hello.chip == 5 && s->host.hello.mac[5] == 0x05);
    kui_wifi_session_end(s);
    assert(!s->open);
    wifi_model_stop();

    /* On the other chip select: nothing at the first one's fastest rate,
     * so its slower rate is skipped. */
    start(1, 0, KWM_WIFI_ONLINE);
    assert(kui_wifi_session_find(s, &wifi_model_port, NULL) && s->level == 2);
    assert(wifi_model_unanswered <= 12);
    kui_wifi_session_end(s);
    wifi_model_stop();

    /* Too fast for the wires: the slower rate. */
    start(1, 1, KWM_WIFI_ONLINE);
    assert(kui_wifi_session_find(s, &wifi_model_port, NULL) && s->level == 3 && !s->bad);
    kui_wifi_session_end(s);
    wifi_model_stop();

    /* Damaged at every rate: reported as answering badly. */
    start(0, 2, KWM_WIFI_ONLINE);
    assert(!kui_wifi_session_find(s, &wifi_model_port, NULL) && s->answered && !s->open);
    assert(strstr(s->problem, "not reliably"));
    wifi_model_stop();

    /* Nothing there: one try at each chip select. */
    struct wifi_model_options absent = {true, 0, 0, false, KWM_WIFI_ONLINE};
    wifi_model_start(&absent);
    assert(!kui_wifi_session_find(s, &wifi_model_port, NULL) && !s->answered && !s->open);
    assert(!strcmp(s->problem, "No Wi-Fi board answered on the SCI port"));
    assert(wifi_model_transfers == 24);
    wifi_model_stop();

    /* No READY wire: it works, and the inspection says so. */
    struct wifi_model_options no_ready = {false, 0, 0, true, KWM_WIFI_ONLINE};
    wifi_model_start(&no_ready);
    assert(kui_wifi_session_find(s, &wifi_model_port, NULL) && !s->ready_changes && s->ready_timeouts);
    kui_wifi_session_end(s);
    struct kui_app_status out;
    assert(kui_wifi_network_inspect(&out, NULL, never) && line(&out, "never changed (GPIO5)"));
    wifi_model_stop();
    free(s);
    printf("PASS Wi-Fi board found at either chip select, slower on errors, absent and damaged reported\n");
}

static unsigned stages;
static char last_stage[128];
static void stage(void *ctx, const char *text) {
    (void)ctx;
    ++stages;
    snprintf(last_stage, sizeof(last_stage), "%s", text);
}
static void wifi_control(void) {
    struct kui_wifi_session *s = session();
    start(0, 0, KWM_WIFI_IDLE);
    assert(kui_wifi_session_find(s, &wifi_model_port, NULL));
    assert(kui_wifi_session_status(s, never) && s->host.wifi.state == KWM_WIFI_IDLE);
    assert(!kui_wifi_session_online(s, 2000, never, stage, NULL) && strstr(s->problem, "No Wi-Fi network is set up"));
    assert(kui_wifi_session_scan(s, never) && s->host.scan_count == 5);
    stages = 0;
    assert(!kui_wifi_session_join(s, "Home", "wrong password", KWM_BAND_KEEP, never, stage, NULL));
    assert(!strcmp(s->problem, "Home refused the password") && stages >= 2);
    assert(!kui_wifi_session_join(s, "Home", "short", KWM_BAND_KEEP, never, stage, NULL));
    assert(strstr(s->problem, "8 to 63 characters"));
    assert(kui_wifi_session_join(s, "Home 5G", WIFI_MODEL_PASSWORD, KWM_BAND_5, never, stage, NULL));
    assert(!strcmp(last_stage, "Wi-Fi: online on Home 5G") && s->host.wifi.band == 5 && s->host.wifi.saved);
    assert(!strcmp(wifi_model_joined.password, WIFI_MODEL_PASSWORD) && wifi_model_joined.save);
    assert(wifi_model_joined.band == KWM_BAND_5 && s->host.wifi.band_mode == KWM_BAND_5);
    assert(kui_wifi_session_online(s, 2000, never, NULL, NULL));
    assert(kui_wifi_session_join(s, "Cafe", "", KWM_BAND_KEEP, never, NULL, NULL) && s->host.wifi.channel == 1);
    assert(kui_wifi_session_band(s, KWM_BAND_24, never) && s->host.wifi.band_mode == KWM_BAND_24);
    assert(!kui_wifi_session_band(s, 7, never));
    assert(kui_wifi_session_leave(s, true, never) && s->host.wifi.state == KWM_WIFI_IDLE && !s->host.wifi.saved);
    kui_wifi_session_end(s);
    wifi_model_stop();
    free(s);
    printf("PASS Wi-Fi status, scan, joins (refused, too short, 5 GHz, open), bands, forget\n");
}

static int client(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(fd >= 0 && connect(fd, (struct sockaddr *)&a, sizeof(a)) == 0);
    assert(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) == 0);
    return fd;
}
static void sockets(void) {
    struct kui_wifi_session *s = session();
    start(0, 0, KWM_WIFI_ONLINE);
    assert(kui_wifi_session_find(s, &wifi_model_port, NULL));
    struct kui_net_sockets net;
    kui_wifi_session_sockets(s, &net);
    assert(net.count == 8);
    uint16_t port = (uint16_t)(40000u + (unsigned)getpid() % 20000u);
    assert(net.listen(net.ctx, 3, port));
    uint8_t state = 0;
    for(unsigned i = 0; i < 20; ++i) assert(kui_wifi_session_step(s));
    assert(net.state(net.ctx, 3, &state) && state == KUI_NET_LISTEN);
    int fd = client(port);
    for(unsigned i = 0; i < 500 && state != KUI_NET_ESTABLISHED; ++i) {
        assert(kui_wifi_session_step(s));
        assert(net.state(net.ctx, 3, &state));
    }
    assert(state == KUI_NET_ESTABLISHED);
    uint8_t ip[4];
    uint16_t peer = 0;
    assert(net.peer(net.ctx, 3, ip, &peer) && ip[0] == 127 && peer);
    assert(send(fd, "USER kui\r\n", 10, 0) == 10);
    size_t waiting = 0;
    for(unsigned i = 0; i < 500 && waiting < 10; ++i) {
        assert(kui_wifi_session_step(s));
        assert(net.received(net.ctx, 3, &waiting));
    }
    char text[16];
    assert(waiting == 10 && net.receive(net.ctx, 3, text, 10) && !memcmp(text, "USER kui\r\n", 10));
    size_t room = 0;
    assert(net.room(net.ctx, 3, &room) && room >= 1024u);
    bool all = false;
    assert(net.sent(net.ctx, 3, &all) && all);
    assert(net.send(net.ctx, 3, "331 Password\r\n", 14));
    assert(net.sent(net.ctx, 3, &all) && !all);
    for(unsigned i = 0; i < 500 && !all; ++i) {
        assert(kui_wifi_session_step(s));
        assert(net.sent(net.ctx, 3, &all));
    }
    assert(all);
    ssize_t got = -1;
    for(unsigned i = 0; i < 500 && got < 0; ++i) {
        got = recv(fd, text, sizeof(text), 0);
        assert(kui_wifi_session_step(s));
    }
    assert(got == 14 && !memcmp(text, "331 Password\r\n", 14));
    /* The client leaves: the peer's close shows once its data is read. */
    close(fd);
    for(unsigned i = 0; i < 500 && state != KUI_NET_PEER_CLOSED; ++i) {
        assert(kui_wifi_session_step(s));
        assert(net.state(net.ctx, 3, &state));
    }
    assert(state == KUI_NET_PEER_CLOSED && net.disconnect(net.ctx, 3));
    for(unsigned i = 0; i < 1000 && state != KUI_NET_CLOSED; ++i) {
        assert(kui_wifi_session_step(s));
        assert(net.state(net.ctx, 3, &state));
    }
    assert(state == KUI_NET_CLOSED);
    /* Ending the session leaves nothing listening even when its outgoing
     * queue is full and the first close requests cannot be queued. */
    assert(net.listen(net.ctx, 3, port));
    assert(net.listen(net.ctx, 4, port));
    for(unsigned i = 0; i < 20; ++i) assert(kui_wifi_session_step(s));
    assert(net.state(net.ctx, 3, &state) && state == KUI_NET_LISTEN);
    assert(net.state(net.ctx, 4, &state) && state == KUI_NET_LISTEN);
    assert(!kui_wifi_session_busy(s));
    unsigned queued = 0;
    while(kwh_wifi_query(&s->host)) assert(++queued <= KWH_QUEUE / KWM_HEADER);
    assert(queued == KWH_QUEUE / KWM_HEADER && s->host.queue_len == KWH_QUEUE);
    kui_wifi_session_end(s);
    assert(!s->open && !s->host.queue_len && !kwl_busy(&s->host.link));
    int refused = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    wifi_model_live()->absent = true; /* its service still runs on each transfer */
    assert(connect(refused, (struct sockaddr *)&a, sizeof(a)) < 0 && errno == ECONNREFUSED);
    close(refused);
    wifi_model_stop();
    free(s);
    printf("PASS Wi-Fi sockets: listen, data both ways, sent, peer close, orderly close, full-queue cleanup\n");
}

static void silence(void) {
    struct kui_wifi_session *s = session();
    start(0, 0, KWM_WIFI_ONLINE);
    assert(kui_wifi_session_find(s, &wifi_model_port, NULL));
    wifi_model_live()->absent = true;
    unsigned steps = 0;
    while(kui_wifi_session_step(s)) {
        usleep(10000);
        assert(++steps < 1000);
    }
    assert(!strcmp(s->problem, "The Wi-Fi board stopped answering") && steps >= 250);
    kui_wifi_session_end(s);
    wifi_model_stop();
    free(s);
    printf("PASS Wi-Fi board that stops answering is noticed after three seconds\n");
}

static void network_app(void) {
    struct kui_app_status out;
    start(0, 0, KWM_WIFI_ONLINE);
    assert(kui_wifi_network_inspect(&out, NULL, never));
    assert(out.complete && out.passed && !out.errors && out.line_count == 8);
    assert(line(&out, "K-UI Wi-Fi board (XIAO ESP32-C5), firmware model-1"));
    assert(line(&out, "SPI 12.5 MHz, select GPIO6; link checked; READY working"));
    assert(line(&out, "Wi-Fi: online on Home 5G") && line(&out, "5 GHz channel 36, signal -48 dBm"));
    assert(line(&out, "Address: 127.0.0.1") && line(&out, "MAC: 02:4B:55:49:00:05"));
    assert(kui_wifi_network_test(&out, NULL, never, NULL));
    assert(out.complete && out.passed && line(&out, "pool.ntp.org: 127.0.0.1"));
    assert(line(&out, "Network time: 2026-09-21 ") && line(&out, "UTC"));
    wifi_model_stop();

    start(0, 0, KWM_WIFI_IDLE);
    assert(kui_wifi_network_inspect(&out, NULL, never) && !out.passed && line(&out, "no network set up"));
    assert(kui_wifi_network_test(&out, NULL, never, NULL) && !out.passed && strstr(out.message, "No Wi-Fi network"));
    wifi_model_stop();

    struct wifi_model_options absent = {true, 0, 0, false, KWM_WIFI_ONLINE};
    wifi_model_start(&absent);
    strcpy(out.message, "untouched");
    assert(!kui_wifi_network_inspect(&out, NULL, never) && !strcmp(out.message, "untouched"));
    assert(!kui_wifi_network_test(&out, NULL, never, NULL) && !strcmp(out.message, "untouched"));
    wifi_model_stop();

    start(0, 2, KWM_WIFI_ONLINE);
    assert(kui_wifi_network_inspect(&out, NULL, never) && out.errors && strstr(out.message, "not reliably"));
    wifi_model_stop();
    printf("PASS Network app: inspection and connection test on the Wi-Fi board\n");
}

static struct kui_wifi_view published;
static unsigned publications;
static void publish(const struct kui_wifi_view *view) {
    published = *view;
    ++publications;
}
static void page(void) {
    struct kui_wifi_view view;
    struct kui_wifi_request request;
    memset(&request, 0, sizeof(request));
    start(0, 0, KWM_WIFI_IDLE);
    request.action = KUI_WIFI_REFRESH;
    kui_wifi_run(&wifi_model_port, &request, &view, NULL, never, publish);
    assert(view.found && !view.working && !view.failed && view.scanned && view.count == 3);
    assert(!strcmp(view.message, "3 networks in range") && !strcmp(published.message, view.message));
    assert(!strcmp(view.networks[0].ssid, "Home 5G") && view.networks[0].five && view.networks[0].security == 3);
    assert(!strcmp(view.networks[1].ssid, "Home") && view.networks[1].rssi == -55 && view.networks[1].channel == 11);
    assert(!strcmp(view.networks[2].ssid, "Cafe") && !view.networks[2].security);
    assert(strstr(view.board, "XIAO ESP32-C5, firmware model-1 (SPI 12.5 MHz"));
    request.action = KUI_WIFI_JOIN;
    strcpy(request.ssid, "Home");
    strcpy(request.password, "not it at all");
    kui_wifi_run(&wifi_model_port, &request, &view, NULL, never, publish);
    assert(view.failed && !strcmp(view.message, "Home refused the password") && !view.scanned);
    assert(view.wifi.state == KWM_WIFI_BAD_PASSWORD);
    strcpy(request.password, WIFI_MODEL_PASSWORD);
    publications = 0;
    kui_wifi_run(&wifi_model_port, &request, &view, NULL, never, publish);
    assert(!view.failed && !strcmp(view.message, "Online on Home as 127.0.0.1; saved on the board"));
    assert(view.wifi.state == KWM_WIFI_ONLINE && publications >= 3);
    request.action = KUI_WIFI_BAND;
    request.band = KWM_BAND_5;
    kui_wifi_run(&wifi_model_port, &request, &view, NULL, never, publish);
    assert(!view.failed && view.wifi.band_mode == KWM_BAND_5 && strstr(view.message, "5 GHz only"));
    request.action = KUI_WIFI_FORGET;
    kui_wifi_run(&wifi_model_port, &request, &view, NULL, never, publish);
    assert(!view.failed && view.wifi.state == KWM_WIFI_IDLE && !view.wifi.saved);
    wifi_model_stop();

    struct wifi_model_options absent = {true, 0, 0, false, KWM_WIFI_ONLINE};
    wifi_model_start(&absent);
    request.action = KUI_WIFI_REFRESH;
    kui_wifi_run(&wifi_model_port, &request, &view, NULL, never, publish);
    assert(!view.found && view.failed && !view.working && !strcmp(view.message, "No Wi-Fi board answered on the SCI port"));
    wifi_model_stop();
    printf("PASS Wi-Fi page: scan list, refused and accepted joins, band, forget, no board\n");
}

int main(void) {
    finding();
    wifi_control();
    sockets();
    silence();
    network_app();
    page();
    printf("test_network_wifi: all passed\n");
    return 0;
}
