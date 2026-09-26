/* SPDX-License-Identifier: GPL-3.0-only */
/* K-UI's FTP server over the W5500's TCP sockets, written for K-UI. One
 * loop on the storage worker serves up to three clients: their commands,
 * their data connections (passive or active) and the card through FatFs.
 *
 * Sockets: 0-2 carry data (4 KB buffers each way; 0 is first used for
 * DHCP), 3-6 listen for control connections on port 21 (1 KB each): one
 * more than there are sessions, so a client over the limit is told why.
 * Lease renewals borrow a free data socket for UDP. */
#include "kui/ftp.h"
#include "kui/clock.h"
#include "platform.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATA_SOCKETS 3u
#define CONTROL_FIRST 3u
#define CONTROL_SOCKETS 4u
#define CARD_CAP (KUI_FILES_PATH_CAP + 2u)
#define BUFFER_BYTES (16u * 1024u)
#define IN_CAP 1024u
#define OUT_CAP 2048u
#define CONNECT_MS 30000u   /* for the client to open a data connection */
#define STALL_MS 60000u     /* a transfer with no progress is given up */
#define LOGIN_MS 60000u
#define IDLE_MS 600000u
#define HOLD_MS 2000u       /* after a wrong password */
#define PUBLISH_MS 250u
#define RENEW_LIMIT_MS 3000u
#define RENEW_RETRY_MS 60000u
#define DRAIN_MS 10000u     /* for a closing data connection to finish */
#define STOP_MS 1000u
#define PART_LIMIT 99u
enum {FREE = -1, REJECTING = -2, RENEWING = -3};
enum transfer {T_NONE, T_LIST, T_NLST, T_MLSD, T_RETR, T_STOR};
enum phase {P_IDLE, P_CONNECT, P_RUN, P_DRAIN};

struct session {
    bool active, user, logged_in, closing;
    unsigned control, failures;
    uint8_t peer[4];
    uint64_t hold_until, last_command, opened_ms, closing_ms;
    char cwd[KUI_FILES_PATH_CAP], rename_from[KUI_FILES_PATH_CAP];
    bool rename_ready;
    uint64_t rest;
    char in[IN_CAP], out[OUT_CAP];
    size_t in_len, out_len;
    bool discarding;
    /* The data connection: a listening (passive) socket, or where to
     * connect (active, after PORT or EPRT). */
    int data;
    bool active_ready;
    uint8_t active_ip[4];
    uint16_t active_port;
    enum transfer kind;
    enum phase phase;
    uint64_t phase_ms, progress_ms;
    FIL file;
    DIR dir;
    FILINFO info;
    bool file_open, dir_open, single, source_done;
    char path[KUI_FILES_PATH_CAP], part[KUI_FILES_PATH_CAP];
    uint8_t *buffer;
    size_t buffered, sent;
    uint64_t done, total, rate_mark_ms, rate_mark_bytes;
    uint32_t rate;
};
struct server {
    struct kui_w5500_session net;
    struct kui_ftp_status *status;
    kui_log_fn log;
    kui_cancel_fn cancel;
    kui_ftp_publish_fn publish;
    uint16_t control_port, passive_first, passive_count, passive_next;
    struct session sessions[KUI_FTP_SESSIONS];
    int data_owner[DATA_SOCKETS];
    bool data_draining[DATA_SOCKETS];
    uint64_t data_since[DATA_SOCKETS];
    int control_owner[CONTROL_SOCKETS];
    uint64_t control_since[CONTROL_SOCKETS];
    char password[KUI_FTP_PASSWORD_CAP];
    uint64_t published_ms, link_ms, renew_after_ms;
    bool changed, connected, mounted;
    const char *stop_reason;
    FATFS fs;
};

static uint64_t now_ms(struct server *sv) { return sv->net.port->bus->now_ms(sv->net.port->bus->ctx); }
static bool card(char out[CARD_CAP], const char *path) {
    int n = snprintf(out, CARD_CAP, "0:%s", path);
    return n > 0 && n < (int)CARD_CAP;
}
/* FatFs cannot stat the root folder; it always exists. */
static FRESULT stat_path(const char *path, FILINFO *info) {
    if(!path[1]) { memset(info, 0, sizeof(*info)); info->fattrib = AM_DIR; return FR_OK; }
    char c[CARD_CAP];
    if(!card(c, path)) return FR_INVALID_NAME;
    return f_stat(c, info);
}
static bool missing(FRESULT r) { return r == FR_NO_FILE || r == FR_NO_PATH; }
static void event(struct server *sv, const char *format, ...) {
    struct kui_ftp_status *st = sv->status;
    memmove(st->events[1], st->events[0], sizeof(st->events[0]) * (KUI_FTP_EVENTS - 1u));
    va_list args;
    va_start(args, format);
    vsnprintf(st->events[0], sizeof(st->events[0]), format, args);
    va_end(args);
    if(st->event_count < KUI_FTP_EVENTS) ++st->event_count;
    if(sv->log) sv->log("FTP: %s", st->events[0]);
    sv->changed = true;
}
static void size_words(char out[16], uint64_t bytes) { kui_files_size_text(out, bytes); }
static unsigned index_of(const struct server *sv, const struct session *s) { return (unsigned)(s - sv->sessions); }

/* ---- Control connection output ---- */
static void reply(struct session *s, const char *format, ...) {
    char line[KUI_FTP_LINE_CAP + 160u];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line, sizeof(line) - 2u, format, args);
    va_end(args);
    if(n < 0) return;
    size_t len = (size_t)n < sizeof(line) - 2u ? (size_t)n : sizeof(line) - 3u;
    line[len++] = '\r';
    line[len++] = '\n';
    if(s->out_len + len > sizeof(s->out)) { s->closing = true; return; } /* a client that never reads */
    memcpy(s->out + s->out_len, line, len);
    s->out_len += len;
}
static bool flush(struct server *sv, struct session *s, uint64_t now) {
    if(!s->out_len || now < s->hold_until) return false;
    uint16_t room = 0;
    if(!kui_w5500_room(&sv->net.chip, s->control, &room) || !room) return false;
    uint16_t n = (uint16_t)(s->out_len < room ? s->out_len : room);
    if(!kui_w5500_send(&sv->net.chip, s->control, s->out, n)) return false;
    memmove(s->out, s->out + n, s->out_len - n);
    s->out_len -= n;
    return true;
}

/* ---- Data sockets ---- */
static int data_take(struct server *sv, struct session *s, uint64_t now) {
    for(unsigned pass = 0; pass < 2; ++pass)
        for(unsigned j = 0; j < DATA_SOCKETS; ++j) {
            if(sv->data_owner[j] != FREE) continue;
            /* A connection still closing is cut short only when needed. */
            if(sv->data_draining[j] && (!pass || now - sv->data_since[j] < 1000u)) continue;
            if(sv->data_draining[j]) (void)kui_w5500_close(&sv->net.chip, j);
            sv->data_draining[j] = false;
            sv->data_owner[j] = (int)index_of(sv, s);
            sv->data_since[j] = now;
            return (int)j;
        }
    return -1;
}
/* graceful: the transfer finished; let TCP close it in its own time. */
static void data_release(struct server *sv, struct session *s, bool graceful, uint64_t now) {
    if(s->data < 0) return;
    unsigned j = (unsigned)s->data;
    s->data = -1;
    sv->data_owner[j] = FREE;
    sv->data_draining[j] = graceful && kui_w5500_disconnect(&sv->net.chip, j);
    if(!sv->data_draining[j]) (void)kui_w5500_close(&sv->net.chip, j);
    sv->data_since[j] = now;
}
static void data_poll(struct server *sv, uint64_t now) {
    for(unsigned j = 0; j < DATA_SOCKETS; ++j) {
        if(!sv->data_draining[j]) continue;
        uint8_t state = KUI_W5500_CLOSED;
        (void)kui_w5500_status(&sv->net.chip, j, &state);
        if(state == KUI_W5500_CLOSED || now - sv->data_since[j] > DRAIN_MS) {
            if(state != KUI_W5500_CLOSED) (void)kui_w5500_close(&sv->net.chip, j);
            sv->data_draining[j] = false;
        }
    }
}
static uint16_t next_port(struct server *sv) {
    uint16_t port = (uint16_t)(sv->passive_first + sv->passive_next);
    sv->passive_next = (uint16_t)((sv->passive_next + 1u) % sv->passive_count);
    return port;
}
static void forget_data(struct server *sv, struct session *s, uint64_t now) {
    data_release(sv, s, false, now);
    s->active_ready = false;
}

/* ---- Transfers ---- */
static bool transferring(const struct session *s) { return s->kind != T_NONE; }
/* Another client's transfer is on `path`, inside it, or holds it. */
static bool in_use(const struct server *sv, const struct session *self, const char *path) {
    for(unsigned i = 0; i < KUI_FTP_SESSIONS; ++i) {
        const struct session *o = &sv->sessions[i];
        if(o == self || !o->active || (o->kind != T_RETR && o->kind != T_STOR)) continue;
        if(kui_files_within(o->path, path) || kui_files_within(path, o->path)) return true;
    }
    return false;
}
static void end_transfer(struct server *sv, struct session *s, bool ok, uint64_t now) {
    if(s->file_open) {
        FRESULT r = f_close(&s->file);
        if(r != FR_OK) ok = false;
        s->file_open = false;
    }
    if(s->dir_open) { (void)f_closedir(&s->dir); s->dir_open = false; }
    if(s->kind == T_STOR && s->part[0]) {
        char c[CARD_CAP];
        if(card(c, s->part)) (void)f_unlink(c);
        s->part[0] = 0;
    }
    free(s->buffer);
    s->buffer = NULL;
    s->buffered = s->sent = 0;
    data_release(sv, s, ok, now);
    s->active_ready = false;
    s->kind = T_NONE;
    s->phase = P_IDLE;
    s->rest = 0;
    s->rate = 0;
    sv->changed = true;
}
/* Stops the transfer with the given reply (426 and the like). */
static void fail_transfer(struct server *sv, struct session *s, uint64_t now, const char *format, ...) {
    char text[KUI_FTP_LINE_CAP];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if(s->kind == T_STOR || s->kind == T_RETR) {
        event(sv, "%s %.40s stopped: %.40s", s->kind == T_STOR ? "Upload of" : "Download of", kui_files_leaf(s->path),
            text + 4);
        ++sv->status->failures;
    }
    end_transfer(sv, s, false, now);
    reply(s, "%s", text);
}
static bool part_path(char out[KUI_FILES_PATH_CAP], const char *target) {
    char parent[KUI_FILES_PATH_CAP], name[32];
    if(!kui_files_parent(parent, target)) return false;
    for(unsigned n = 1; n <= PART_LIMIT; ++n) {
        snprintf(name, sizeof(name), "KUI-ftp-%u%s", n, KUI_FILES_PART_SUFFIX);
        FILINFO info;
        if(!kui_files_join(out, parent, name)) return false;
        if(stat_path(out, &info) == FR_NO_FILE) return true;
    }
    return false;
}
static void rate(struct session *s, uint64_t now) {
    if(!s->rate_mark_ms) { s->rate_mark_ms = now; s->rate_mark_bytes = s->done; return; }
    uint64_t span = now - s->rate_mark_ms;
    if(span < 1000u) return;
    s->rate = (uint32_t)((s->done - s->rate_mark_bytes) * 1000u / span);
    s->rate_mark_ms = now;
    s->rate_mark_bytes = s->done;
}
static void begin_transfer(struct server *sv, struct session *s, enum transfer kind, const char *path, uint64_t now) {
    s->kind = kind;
    s->phase = P_CONNECT;
    s->phase_ms = s->progress_ms = now;
    s->done = s->total = 0;
    s->rate = 0;
    s->rate_mark_ms = 0;
    s->source_done = false;
    s->buffered = s->sent = 0;
    snprintf(s->path, sizeof(s->path), "%s", path);
    if(s->data < 0 && s->active_ready) {
        /* Active mode: connect out to the address PORT or EPRT gave. */
        int j = data_take(sv, s, now);
        if(j < 0 || !kui_w5500_open(&sv->net.chip, (unsigned)j, KUI_W5500_TCP | KUI_W5500_MR_NODELAY, next_port(sv)) ||
           !kui_w5500_connect(&sv->net.chip, (unsigned)j, s->active_ip, s->active_port)) {
            if(j >= 0) { s->data = j; }
            fail_transfer(sv, s, now, "425 Cannot open the data connection");
            return;
        }
        s->data = j;
    }
    sv->changed = true;
}
/* The data connection, once open: the client must be the one on the
 * control connection. */
static bool data_ready(struct server *sv, struct session *s, uint64_t now, bool *failed) {
    uint8_t state = KUI_W5500_CLOSED;
    *failed = false;
    if(!kui_w5500_status(&sv->net.chip, (unsigned)s->data, &state)) { *failed = true; return false; }
    if(state == KUI_W5500_ESTABLISHED || (state == KUI_W5500_CLOSE_WAIT && s->kind == T_STOR)) {
        uint8_t ip[4];
        uint16_t port;
        if(!kui_w5500_peer(&sv->net.chip, (unsigned)s->data, ip, &port) || memcmp(ip, s->peer, 4)) {
            *failed = true;
            return false;
        }
        (void)kui_w5500_keepalive(&sv->net.chip, (unsigned)s->data, 60);
        return true;
    }
    if(state == KUI_W5500_CLOSED || state == KUI_W5500_CLOSE_WAIT || now - s->phase_ms > CONNECT_MS) *failed = true;
    return false;
}
static bool fill_listing(struct server *sv, struct session *s) {
    uint16_t today = (uint16_t)(kui_clock_fattime() >> 16);
    while(!s->source_done && BUFFER_BYTES - s->buffered >= 1024u) {
        FILINFO *info = &s->info;
        if(s->single) s->source_done = true;
        else {
            FRESULT r = f_readdir(&s->dir, info);
            if(r != FR_OK) return false;
            if(!info->fname[0]) { s->source_done = true; break; }
        }
        if(!strcmp(info->fname, ".") || !strcmp(info->fname, "..")) continue;
        bool directory = info->fattrib & AM_DIR;
        char *at = (char *)s->buffer + s->buffered;
        size_t room = BUFFER_BYTES - s->buffered, n;
        if(s->kind == T_NLST) {
            int w = snprintf(at, room, "%s\r\n", info->fname);
            n = w > 0 && (size_t)w < room ? (size_t)w : 0;
        } else if(s->kind == T_MLSD) n = kui_ftp_mlsd_line(at, room, info->fname, info->fsize, info->fdate, info->ftime, directory);
        else {
            char full[KUI_FILES_PATH_CAP];
            bool locked = (info->fattrib & AM_RDO) ||
                (kui_files_join(full, s->path, info->fname) && kui_files_protected(full));
            n = kui_ftp_list_line(at, room, info->fname, info->fsize, info->fdate, info->ftime, directory, locked, today);
        }
        s->buffered += n;
    }
    (void)sv;
    return true;
}
static void finish_upload(struct server *sv, struct session *s, uint64_t now) {
    char from[CARD_CAP], to[CARD_CAP];
    FRESULT r = f_close(&s->file);
    s->file_open = false;
    const char *problem = NULL;
    if(r != FR_OK) problem = "451 The SD card could not finish the file";
    else if(!card(from, s->part) || !card(to, s->path)) problem = "451 Invalid path";
    else {
        FILINFO old;
        FRESULT seen = stat_path(s->path, &old);
        if(seen == FR_OK && (old.fattrib & AM_DIR)) problem = "553 A folder now has that name";
        else if(seen == FR_OK && (r = f_unlink(to)) != FR_OK) problem = "451 Cannot replace the old file";
        else if(seen != FR_OK && seen != FR_NO_FILE) problem = "451 Cannot check the destination";
        else if((r = f_rename(from, to)) != FR_OK) problem = "451 Cannot give the upload its name";
    }
    if(problem) { fail_transfer(sv, s, now, "%s", problem); return; }
    s->part[0] = 0;
    char size[16];
    size_words(size, s->done);
    ++sv->status->files_in;
    event(sv, "Received %.56s (%s)", s->path, size);
    end_transfer(sv, s, true, now);
    reply(s, "226 Upload complete: %llu bytes", (unsigned long long)s->done);
}
static bool write_buffer(struct server *sv, struct session *s, uint64_t now) {
    if(!s->buffered) return true;
    UINT wrote = 0;
    FRESULT r = f_write(&s->file, s->buffer, (UINT)s->buffered, &wrote);
    if(r != FR_OK || wrote != s->buffered) {
        fail_transfer(sv, s, now, r == FR_OK ? "452 The SD card is full; upload discarded" :
            "451 The SD card could not be written; upload discarded");
        return false;
    }
    s->buffered = 0;
    return true;
}
static bool transfer_step(struct server *sv, struct session *s, uint64_t now) {
    if(s->phase == P_CONNECT) {
        bool failed;
        if(data_ready(sv, s, now, &failed)) { s->phase = P_RUN; s->progress_ms = now; return true; }
        if(failed) { fail_transfer(sv, s, now, "425 Cannot open the data connection"); return true; }
        return false;
    }
    unsigned j = (unsigned)s->data;
    uint8_t state = KUI_W5500_CLOSED;
    if(!kui_w5500_status(&sv->net.chip, j, &state)) { fail_transfer(sv, s, now, "426 The network adapter stopped"); return true; }
    bool work = false;
    if(s->kind == T_STOR) {
        uint16_t waiting = 0;
        if(!kui_w5500_received(&sv->net.chip, j, &waiting)) { fail_transfer(sv, s, now, "426 The network adapter stopped"); return true; }
        if(waiting && s->buffered < BUFFER_BYTES) {
            uint16_t take = (uint16_t)(BUFFER_BYTES - s->buffered < waiting ? BUFFER_BYTES - s->buffered : waiting);
            if(!kui_w5500_receive(&sv->net.chip, j, s->buffer + s->buffered, take)) {
                fail_transfer(sv, s, now, "426 The network adapter stopped");
                return true;
            }
            s->buffered += take;
            s->done += take;
            sv->status->bytes_in += take;
            s->progress_ms = now;
            work = true;
            waiting = (uint16_t)(waiting - take);
        }
        bool ended = state == KUI_W5500_CLOSE_WAIT && !waiting;
        /* Whole buffers suit the card; the rest is written at the end. */
        if(s->buffered == BUFFER_BYTES || (ended && s->buffered)) {
            if(!write_buffer(sv, s, now)) return true;
            work = true;
        }
        if(ended && !s->buffered) { finish_upload(sv, s, now); return true; }
        if(state != KUI_W5500_ESTABLISHED && state != KUI_W5500_CLOSE_WAIT) {
            fail_transfer(sv, s, now, "426 The connection was lost; upload discarded");
            return true;
        }
    } else if(s->phase == P_RUN) {
        /* A client may close its own side early and still read. */
        if(state != KUI_W5500_ESTABLISHED && state != KUI_W5500_CLOSE_WAIT) {
            fail_transfer(sv, s, now, "426 The connection was closed; transfer stopped");
            return true;
        }
        if(s->sent == s->buffered && !s->source_done) {
            s->sent = s->buffered = 0;
            if(s->kind == T_RETR) {
                UINT got = 0;
                FRESULT r = f_read(&s->file, s->buffer, BUFFER_BYTES, &got);
                if(r != FR_OK) { fail_transfer(sv, s, now, "451 The SD card could not be read"); return true; }
                s->buffered = got;
                if(!got) s->source_done = true;
            } else if(!fill_listing(sv, s)) {
                fail_transfer(sv, s, now, "451 The folder could not be read");
                return true;
            }
            work = true;
        }
        if(s->sent < s->buffered) {
            uint16_t room = 0;
            if(!kui_w5500_room(&sv->net.chip, j, &room)) { fail_transfer(sv, s, now, "426 The connection was lost"); return true; }
            if(room) {
                uint16_t n = (uint16_t)(s->buffered - s->sent < room ? s->buffered - s->sent : room);
                if(!kui_w5500_send(&sv->net.chip, j, s->buffer + s->sent, n)) {
                    fail_transfer(sv, s, now, "426 The connection was lost");
                    return true;
                }
                s->sent += n;
                s->done += n;
                if(s->kind == T_RETR) sv->status->bytes_out += n;
                s->progress_ms = now;
                work = true;
            }
        }
        if(s->source_done && s->sent == s->buffered) { s->phase = P_DRAIN; s->phase_ms = now; }
    } else {
        /* Everything sent: close once the client has acknowledged it all. */
        uint16_t room = 0;
        bool acked = kui_w5500_room(&sv->net.chip, j, &room) && room == sv->net.chip.tx_kb[j] * 1024u;
        if(acked && (state == KUI_W5500_ESTABLISHED || state == KUI_W5500_CLOSE_WAIT)) {
            if(s->kind == T_RETR) {
                char size[16];
                size_words(size, s->done);
                ++sv->status->files_out;
                event(sv, "Sent %.60s (%s)", s->path, size);
            }
            end_transfer(sv, s, true, now);
            reply(s, "226 Transfer complete");
            return true;
        }
        if(state != KUI_W5500_ESTABLISHED && state != KUI_W5500_CLOSE_WAIT) {
            fail_transfer(sv, s, now, "426 The connection was lost before the end");
            return true;
        }
        if(now - s->phase_ms > CONNECT_MS) { fail_transfer(sv, s, now, "426 The client did not take the end of the data"); return true; }
        return false;
    }
    if(s->phase != P_CONNECT && now - s->progress_ms > STALL_MS) {
        fail_transfer(sv, s, now, "426 No data moved for 60 seconds; transfer stopped");
        return true;
    }
    rate(s, now);
    return work;
}

/* ---- Commands ---- */
static bool need_login(struct session *s) {
    if(s->logged_in) return false;
    reply(s, "530 Log in with USER and PASS first");
    return true;
}
static bool resolved(struct session *s, char out[KUI_FILES_PATH_CAP], const char *argument) {
    if(kui_ftp_resolve(out, s->cwd, argument)) return true;
    reply(s, "553 Invalid name or path (names are FAT-safe, under 128 bytes)");
    return false;
}
static bool protect(struct session *s, const char *path) {
    if(!kui_files_protected(path)) return false;
    reply(s, "550 K-UI needs %s to start; it cannot be changed over FTP", path);
    return true;
}
static bool busy(struct server *sv, struct session *s, const char *path) {
    if(!in_use(sv, s, path)) return false;
    reply(s, "450 %s is being transferred by another client; try again after", path);
    return true;
}
static void open_passive(struct server *sv, struct session *s, bool extended, uint64_t now) {
    forget_data(sv, s, now);
    int j = data_take(sv, s, now);
    uint16_t port = next_port(sv);
    if(j < 0) { reply(s, "425 Too many data connections at once; try again"); return; }
    s->data = j;
    if(!kui_w5500_open(&sv->net.chip, (unsigned)j, KUI_W5500_TCP | KUI_W5500_MR_NODELAY, port) ||
       !kui_w5500_listen(&sv->net.chip, (unsigned)j)) {
        forget_data(sv, s, now);
        reply(s, "425 Cannot open a data port");
        return;
    }
    const uint8_t *ip = sv->net.config.ip;
    if(extended) reply(s, "229 Entering Extended Passive Mode (|||%u|)", port);
    else reply(s, "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)", ip[0], ip[1], ip[2], ip[3], port >> 8, port & 255u);
}
static void set_active(struct server *sv, struct session *s, const uint8_t ip[4], uint16_t port, uint64_t now) {
    /* Only to the client itself, and not to a system port: no bouncing. */
    if(memcmp(ip, s->peer, 4) || port < 1024u) { reply(s, "500 PORT is allowed only to your own address, above 1023"); return; }
    forget_data(sv, s, now);
    memcpy(s->active_ip, ip, 4);
    s->active_port = port;
    s->active_ready = true;
    reply(s, "200 PORT command successful");
}
static bool data_prepared(struct session *s) {
    if(s->data >= 0 || s->active_ready) return true;
    reply(s, "425 Use PASV, EPSV, PORT or EPRT first");
    return false;
}
static void start_list(struct server *sv, struct session *s, enum transfer kind, const char *argument, uint64_t now) {
    char path[KUI_FILES_PATH_CAP];
    const char *target = kind == T_MLSD ? argument : kui_ftp_list_path(argument);
    if(!resolved(s, path, target)) return;
    FILINFO info;
    FRESULT r = stat_path(path, &info);
    if(r != FR_OK) { reply(s, missing(r) ? "550 %s: no such file or folder" : "451 Cannot read %s", path); return; }
    bool directory = info.fattrib & AM_DIR;
    if(kind == T_MLSD && !directory) { reply(s, "501 %s is not a folder", path); return; }
    if(!data_prepared(s)) return;
    if(!(s->buffer = malloc(BUFFER_BYTES))) { reply(s, "451 Insufficient memory"); return; }
    s->single = !directory;
    if(directory) {
        char c[CARD_CAP];
        if(!card(c, path) || f_opendir(&s->dir, c) != FR_OK) {
            free(s->buffer);
            s->buffer = NULL;
            reply(s, "451 Cannot open the folder %s", path);
            return;
        }
        s->dir_open = true;
    } else {
        s->info = info;
        memcpy(s->info.fname, kui_files_leaf(path), strlen(kui_files_leaf(path)) + 1u);
    }
    begin_transfer(sv, s, kind, directory ? path : "/", now);
    if(s->kind == kind) {
        if(!directory) {
            /* One file: the listing's folder is its own. */
            char parent[KUI_FILES_PATH_CAP];
            if(kui_files_parent(parent, path)) snprintf(s->path, sizeof(s->path), "%s", parent);
        }
        reply(s, "150 Here comes the listing of %s", path);
    }
}
static void start_retr(struct server *sv, struct session *s, const char *argument, uint64_t now) {
    char path[KUI_FILES_PATH_CAP], c[CARD_CAP];
    if(!resolved(s, path, argument)) return;
    FILINFO info;
    FRESULT r = stat_path(path, &info);
    if(r != FR_OK || (info.fattrib & AM_DIR)) {
        reply(s, r != FR_OK && !missing(r) ? "451 Cannot read %s" : "550 %s: no such file", path);
        return;
    }
    if(busy(sv, s, path)) return;
    if(s->rest > info.fsize) { reply(s, "554 The restart point is past the end of the file"); s->rest = 0; return; }
    if(!data_prepared(s)) return;
    if(!(s->buffer = malloc(BUFFER_BYTES))) { reply(s, "451 Insufficient memory"); return; }
    if(!card(c, path) || f_open(&s->file, c, FA_READ) != FR_OK || f_lseek(&s->file, s->rest) != FR_OK) {
        if(s->file_open) f_close(&s->file);
        free(s->buffer);
        s->buffer = NULL;
        reply(s, "451 Cannot open %s", path);
        return;
    }
    s->file_open = true;
    uint64_t from = s->rest;
    begin_transfer(sv, s, T_RETR, path, now);
    if(s->kind != T_RETR) return;
    s->total = info.fsize - from;
    if(from) reply(s, "150 Sending %s from byte %llu (%llu bytes)", path, (unsigned long long)from, (unsigned long long)s->total);
    else reply(s, "150 Sending %s (%llu bytes)", path, (unsigned long long)s->total);
}
static void start_stor(struct server *sv, struct session *s, const char *argument, uint64_t now) {
    char path[KUI_FILES_PATH_CAP], parent[KUI_FILES_PATH_CAP], c[CARD_CAP];
    if(!resolved(s, path, argument)) return;
    if(!path[1]) { reply(s, "553 Name a file to store"); return; }
    if(s->rest) { s->rest = 0; reply(s, "554 Resuming an upload is not supported; send the whole file"); return; }
    if(protect(s, path) || busy(sv, s, path)) return;
    FILINFO info;
    FRESULT r = stat_path(path, &info);
    if(r == FR_OK && (info.fattrib & AM_DIR)) { reply(s, "553 %s is a folder", path); return; }
    if(r == FR_OK && (info.fattrib & AM_RDO)) { reply(s, "550 %s is read-only", path); return; }
    if(r != FR_OK && r != FR_NO_FILE) { reply(s, missing(r) ? "553 The folder for %s does not exist" : "451 Cannot check %s", path); return; }
    if(!kui_files_parent(parent, path) || stat_path(parent, &info) != FR_OK || !(info.fattrib & AM_DIR)) {
        reply(s, "553 The folder for %s does not exist", path);
        return;
    }
    if(!data_prepared(s)) return;
    if(!part_path(s->part, path)) { reply(s, "451 Too many unfinished uploads in that folder"); return; }
    if(!(s->buffer = malloc(BUFFER_BYTES))) { s->part[0] = 0; reply(s, "451 Insufficient memory"); return; }
    if(!card(c, s->part) || f_open(&s->file, c, FA_WRITE | FA_CREATE_NEW) != FR_OK) {
        free(s->buffer);
        s->buffer = NULL;
        s->part[0] = 0;
        reply(s, "451 Cannot create a file in that folder");
        return;
    }
    s->file_open = true;
    begin_transfer(sv, s, T_STOR, path, now);
    if(s->kind == T_STOR) reply(s, "150 Ready to receive %s", path);
}
static void simple_stat(struct server *sv, struct session *s) {
    reply(s, "211-K-UI FTP server on a Dreamcast");
    reply(s, " Connected from %u.%u.%u.%u%s", s->peer[0], s->peer[1], s->peer[2], s->peer[3],
        s->logged_in ? ", logged in" : "");
    if(s->kind == T_RETR || s->kind == T_STOR)
        reply(s, " %s %s: %llu bytes so far", s->kind == T_RETR ? "Sending" : "Receiving", s->path,
            (unsigned long long)s->done);
    else reply(s, " No transfer in progress");
    (void)sv;
    reply(s, "211 End of status");
}
static void remove_item(struct server *sv, struct session *s, const char *argument, bool folder) {
    char path[KUI_FILES_PATH_CAP], c[CARD_CAP];
    if(!resolved(s, path, argument)) return;
    if(!path[1]) { reply(s, "550 The card's root cannot be removed"); return; }
    if(protect(s, path) || busy(sv, s, path)) return;
    FILINFO info;
    FRESULT r = stat_path(path, &info);
    if(r != FR_OK) { reply(s, missing(r) ? "550 %s: no such file or folder" : "451 Cannot read %s", path); return; }
    bool directory = info.fattrib & AM_DIR;
    if(directory != folder) { reply(s, folder ? "550 %s is not a folder" : "550 %s is a folder; use RMD", path); return; }
    if(!card(c, path)) { reply(s, "553 Invalid path"); return; }
    /* Read-only marks are cleared first, as the File Manager does. */
    if(info.fattrib & AM_RDO) (void)f_chmod(c, 0, AM_RDO);
    r = f_unlink(c);
    if(r != FR_OK) {
        if(info.fattrib & AM_RDO) (void)f_chmod(c, AM_RDO, AM_RDO);
        reply(s, r == FR_DENIED && folder ? "550 %s is not empty" : "450 Cannot remove %s", path);
        return;
    }
    event(sv, "Deleted %.64s", path);
    reply(s, "250 %s removed", path);
}
static void rename_to(struct server *sv, struct session *s, const char *argument) {
    char path[KUI_FILES_PATH_CAP], from[CARD_CAP], to[CARD_CAP];
    bool ready = s->rename_ready;
    s->rename_ready = false;
    if(!ready) { reply(s, "503 Send RNFR first"); return; }
    if(!resolved(s, path, argument)) return;
    if(protect(s, path) || protect(s, s->rename_from) || busy(sv, s, s->rename_from) || busy(sv, s, path)) return;
    FILINFO info, seen;
    FRESULT r = stat_path(s->rename_from, &info);
    if(r != FR_OK) { reply(s, "550 %s is no longer there", s->rename_from); return; }
    if((info.fattrib & AM_DIR) && kui_files_within(path, s->rename_from) && strcmp(path, s->rename_from) &&
       !kui_files_within(s->rename_from, path)) {
        reply(s, "553 A folder cannot be moved into itself");
        return;
    }
    r = stat_path(path, &seen);
    /* The same item under other capitals is a rename FatFs allows. */
    bool same = r == FR_OK && kui_files_within(path, s->rename_from) && kui_files_within(s->rename_from, path);
    if(r == FR_OK && !same) { reply(s, "553 %s already exists", path); return; }
    if(r != FR_OK && r != FR_NO_FILE) { reply(s, missing(r) ? "553 The folder for %s does not exist" : "451 Cannot check %s", path); return; }
    if(!card(from, s->rename_from) || !card(to, path)) { reply(s, "553 Invalid path"); return; }
    r = f_rename(from, to);
    if(r != FR_OK) { reply(s, r == FR_EXIST ? "553 %s already exists" : "450 Cannot rename to %s", path); return; }
    event(sv, "Renamed %.34s to %.34s", kui_files_leaf(s->rename_from), kui_files_leaf(path));
    reply(s, "250 Renamed to %s", path);
}
static void feat(struct session *s) {
    reply(s, "211-Extensions supported:");
    reply(s, " EPRT");
    reply(s, " EPSV");
    reply(s, " MDTM");
    reply(s, " MLST type*;size*;modify*;");
    reply(s, " PASV");
    reply(s, " REST STREAM");
    reply(s, " SIZE");
    reply(s, " UTF8");
    reply(s, "211 End");
}
static bool password_matches(const struct server *sv, const char *given) {
    size_t a = strlen(sv->password), b = strlen(given);
    unsigned diff = (unsigned)(a != b);
    for(size_t i = 0; i < a; ++i) diff |= (unsigned)(sv->password[i] ^ (i < b ? given[i] : 0));
    return !diff;
}
static void command(struct server *sv, struct session *s, char *line, uint64_t now) {
    char verb[8], path[KUI_FILES_PATH_CAP];
    const char *arg;
    s->last_command = now;
    if(!kui_ftp_parse(line, verb, &arg)) { reply(s, "500 Unknown command"); return; }
    if(strcmp(verb, "RNTO") && strcmp(verb, "RNFR")) s->rename_ready = false;
    if(!strcmp(verb, "USER")) {
        if(s->logged_in) { reply(s, "503 Already logged in"); return; }
        s->user = true;
        reply(s, "331 Password required (it is shown on the Dreamcast)");
    } else if(!strcmp(verb, "PASS")) {
        if(s->logged_in) { reply(s, "230 Already logged in"); return; }
        if(!s->user) { reply(s, "503 Send USER first"); return; }
        if(password_matches(sv, arg)) {
            s->logged_in = true;
            event(sv, "%u.%u.%u.%u logged in", s->peer[0], s->peer[1], s->peer[2], s->peer[3]);
            reply(s, "230 Logged in; the card is at /");
            return;
        }
        s->user = false;
        if(++s->failures >= 3) {
            event(sv, "%u.%u.%u.%u: wrong password three times", s->peer[0], s->peer[1], s->peer[2], s->peer[3]);
            reply(s, "421 Too many wrong passwords; closing");
            s->closing = true;
            s->closing_ms = now;
            return;
        }
        /* Each wrong guess costs two seconds before the answer. */
        s->hold_until = now + HOLD_MS;
        reply(s, "530 Wrong password");
    } else if(!strcmp(verb, "QUIT")) {
        reply(s, "221 Goodbye");
        s->closing = true;
        s->closing_ms = now;
    } else if(!strcmp(verb, "NOOP")) reply(s, "200 OK");
    else if(!strcmp(verb, "SYST")) reply(s, "215 UNIX Type: L8");
    else if(!strcmp(verb, "FEAT")) feat(s);
    else if(!strcmp(verb, "AUTH") || !strcmp(verb, "PBSZ") || !strcmp(verb, "PROT"))
        reply(s, "502 TLS is not available; this server is for a trusted home network");
    else if(!strcmp(verb, "OPTS")) {
        if(!strncmp(arg, "UTF8", 4) || !strncmp(arg, "utf8", 4)) reply(s, "200 UTF-8 is always on");
        else if(!strncmp(arg, "MLST", 4) || !strncmp(arg, "mlst", 4)) reply(s, "200 MLST OPTS type;size;modify;");
        else reply(s, "501 Unknown option");
    } else if(!strcmp(verb, "HELP")) {
        reply(s, "214-Commands:");
        reply(s, " USER PASS QUIT NOOP SYST FEAT OPTS HELP STAT TYPE MODE STRU ALLO");
        reply(s, " PWD CWD CDUP LIST NLST MLSD MLST SIZE MDTM REST RETR STOR");
        reply(s, " DELE MKD RMD RNFR RNTO PASV EPSV PORT EPRT ABOR");
        reply(s, "214 End");
    } else if(!strcmp(verb, "ABOR")) {
        if(transferring(s)) {
            fail_transfer(sv, s, now, "426 Transfer aborted");
            reply(s, "226 Abort successful");
        } else reply(s, "225 No transfer to abort");
    } else if(!strcmp(verb, "STAT") && !arg[0]) simple_stat(sv, s);
    else if(need_login(s)) return;
    else if(!strcmp(verb, "PWD") || !strcmp(verb, "XPWD")) reply(s, "257 \"%s\" is the current folder", s->cwd);
    else if(!strcmp(verb, "CWD") || !strcmp(verb, "XCWD") || !strcmp(verb, "CDUP") || !strcmp(verb, "XCUP")) {
        bool up = !strcmp(verb, "CDUP") || !strcmp(verb, "XCUP");
        if(!resolved(s, path, up ? ".." : arg)) return;
        FILINFO info;
        FRESULT r = stat_path(path, &info);
        if(r != FR_OK || !(info.fattrib & AM_DIR)) { reply(s, "550 %s: no such folder", path); return; }
        snprintf(s->cwd, sizeof(s->cwd), "%s", path);
        reply(s, "250 The current folder is %s", path);
    } else if(!strcmp(verb, "TYPE")) {
        char t = (char)(arg[0] & ~0x20);
        if(t == 'I' || t == 'A' || t == 'L') reply(s, "200 Type set to %c (all transfers are binary)", t);
        else reply(s, "504 Type not supported");
    } else if(!strcmp(verb, "MODE")) reply(s, (arg[0] & ~0x20) == 'S' ? "200 Stream mode" : "504 Only stream mode");
    else if(!strcmp(verb, "STRU")) reply(s, (arg[0] & ~0x20) == 'F' ? "200 File structure" : "504 Only file structure");
    else if(!strcmp(verb, "ALLO")) reply(s, "202 No space needs reserving");
    else if(!strcmp(verb, "PASV")) open_passive(sv, s, false, now);
    else if(!strcmp(verb, "EPSV")) {
        if(!arg[0] || !strcmp(arg, "1")) open_passive(sv, s, true, now);
        else if(!strcmp(arg, "ALL") || !strcmp(arg, "all")) reply(s, "200 EPSV ALL accepted");
        else reply(s, "522 Only IPv4 is supported (1)");
    } else if(!strcmp(verb, "PORT") || !strcmp(verb, "EPRT")) {
        uint8_t ip[4];
        uint16_t port;
        bool ipv6 = false;
        bool ok = verb[0] == 'P' ? kui_ftp_parse_port(arg, ip, &port) : kui_ftp_parse_eprt(arg, ip, &port, &ipv6);
        if(!ok) reply(s, ipv6 ? "522 Only IPv4 is supported (1)" : "501 Invalid address");
        else set_active(sv, s, ip, port, now);
    } else if(!strcmp(verb, "REST")) {
        uint64_t at = 0;
        const char *p = arg;
        bool digits = *p;
        for(; *p; ++p) {
            if(*p < '0' || *p > '9' || at > UINT64_MAX / 10u - 9u) { digits = false; break; }
            at = at * 10u + (uint64_t)(*p - '0');
        }
        if(!digits) { reply(s, "501 REST needs a byte count"); return; }
        s->rest = at;
        reply(s, "350 Restarting at %llu; send RETR", (unsigned long long)at);
    } else if(!strcmp(verb, "LIST") || !strcmp(verb, "NLST") || !strcmp(verb, "MLSD"))
        start_list(sv, s, verb[0] == 'L' ? T_LIST : verb[0] == 'N' ? T_NLST : T_MLSD, arg, now);
    else if(!strcmp(verb, "RETR")) start_retr(sv, s, arg, now);
    else if(!strcmp(verb, "STOR")) start_stor(sv, s, arg, now);
    else if(!strcmp(verb, "SIZE") || !strcmp(verb, "MDTM") || !strcmp(verb, "MLST")) {
        if(!resolved(s, path, arg)) return;
        FILINFO info;
        FRESULT r = stat_path(path, &info);
        if(r != FR_OK) { reply(s, missing(r) ? "550 %s: no such file" : "451 Cannot read %s", path); return; }
        bool directory = info.fattrib & AM_DIR;
        if(verb[1] == 'L') {
            char facts[KUI_FTP_LINE_CAP + 64u];
            size_t n = kui_ftp_mlsd_line(facts, sizeof(facts), path, info.fsize, info.fdate, info.ftime, directory);
            if(n >= 2) facts[n - 2] = 0;
            reply(s, "250-Facts for %s", path);
            reply(s, " %s", n ? facts : path);
            reply(s, "250 End");
        } else if(directory) reply(s, "550 %s is a folder", path);
        else if(verb[0] == 'S') reply(s, "213 %llu", (unsigned long long)info.fsize);
        else {
            char stamp[15];
            kui_ftp_timestamp(stamp, info.fdate, info.ftime);
            reply(s, "213 %s", stamp);
        }
    } else if(!strcmp(verb, "DELE")) remove_item(sv, s, arg, false);
    else if(!strcmp(verb, "RMD") || !strcmp(verb, "XRMD")) remove_item(sv, s, arg, true);
    else if(!strcmp(verb, "MKD") || !strcmp(verb, "XMKD")) {
        char c[CARD_CAP];
        if(!resolved(s, path, arg)) return;
        if(!path[1] || !card(c, path)) { reply(s, "550 The root already exists"); return; }
        FRESULT r = f_mkdir(c);
        if(r != FR_OK) { reply(s, r == FR_EXIST ? "550 %s already exists" : missing(r) ? "550 The folder for %s does not exist" :
            "450 Cannot create %s", path); return; }
        event(sv, "Created folder %.56s", path);
        reply(s, "257 \"%s\" created", path);
    } else if(!strcmp(verb, "RNFR")) {
        if(!resolved(s, s->rename_from, arg)) return;
        FILINFO info;
        if(!s->rename_from[1] || stat_path(s->rename_from, &info) != FR_OK) { reply(s, "550 %s: no such file or folder", s->rename_from); return; }
        if(protect(s, s->rename_from)) return;
        s->rename_ready = true;
        reply(s, "350 Ready for RNTO");
    } else if(!strcmp(verb, "RNTO")) rename_to(sv, s, arg);
    else if(!strcmp(verb, "STAT")) reply(s, "502 STAT with a path is not supported; use LIST");
    else reply(s, "502 %s is not supported", verb);
}

/* ---- Control connection input ---- */
static bool read_input(struct server *sv, struct session *s) {
    uint16_t waiting = 0;
    if(!kui_w5500_received(&sv->net.chip, s->control, &waiting) || !waiting) return false;
    size_t room = sizeof(s->in) - s->in_len;
    if(!room) return false;
    uint16_t take = (uint16_t)(waiting < room ? waiting : room);
    if(!kui_w5500_receive(&sv->net.chip, s->control, s->in + s->in_len, take)) return false;
    s->in_len += take;
    return true;
}
/* The next whole line, with Telnet commands (IAC and the byte after it)
 * removed; an empty line when it was too long or held a NUL. consume:
 * take it out of the buffer. */
static bool next_line(struct session *s, char *line, size_t cap, bool consume) {
    char *end = memchr(s->in, '\n', s->in_len);
    if(!end) {
        if(s->in_len == sizeof(s->in)) {
            /* Longer than any command: dropped up to its end. */
            s->in_len = 0;
            if(!s->discarding) reply(s, "500 Command line too long");
            s->discarding = true;
        }
        return false;
    }
    size_t n = (size_t)(end - s->in), out = 0;
    bool bad = s->discarding;
    for(size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s->in[i];
        if(c == 0xff) { ++i; continue; }
        if(c == '\r' && i + 1u == n) break;
        if(!c || out + 1u >= cap) { bad = true; continue; }
        line[out++] = (char)c;
    }
    line[out] = 0;
    if(consume) {
        memmove(s->in, end + 1, s->in_len - n - 1u);
        s->in_len -= n + 1u;
        if(bad && !s->discarding) reply(s, "500 Command line too long or invalid");
        s->discarding = false;
    }
    if(bad) line[0] = 0;
    return true;
}

/* ---- Sessions ---- */
static void session_open(struct server *sv, struct session *s, unsigned control, uint64_t now) {
    memset(s, 0, sizeof(*s));
    s->data = -1;
    s->kind = T_NONE;
    s->phase = P_IDLE;
    s->active = true;
    s->control = control;
    s->opened_ms = s->last_command = now;
    strcpy(s->cwd, "/");
    uint16_t port;
    if(!kui_w5500_peer(&sv->net.chip, control, s->peer, &port)) memset(s->peer, 0, 4);
    (void)kui_w5500_keepalive(&sv->net.chip, control, 60);
    ++sv->status->connections;
    event(sv, "%u.%u.%u.%u connected", s->peer[0], s->peer[1], s->peer[2], s->peer[3]);
    reply(s, "220 K-UI FTP server on a Dreamcast; log in with the password on its screen");
}
static void session_close(struct server *sv, struct session *s, uint64_t now, bool graceful) {
    if(transferring(s)) fail_transfer(sv, s, now, "426 The connection is closing");
    forget_data(sv, s, now);
    unsigned c = s->control - CONTROL_FIRST;
    if(graceful) (void)kui_w5500_disconnect(&sv->net.chip, s->control);
    else (void)kui_w5500_close(&sv->net.chip, s->control);
    /* The control socket listens again once it has closed. */
    sv->control_owner[c] = REJECTING;
    sv->control_since[c] = now;
    event(sv, "%u.%u.%u.%u disconnected", s->peer[0], s->peer[1], s->peer[2], s->peer[3]);
    s->active = false;
    sv->changed = true;
}
static bool allowed_during_transfer(const char *line) {
    char verb[8];
    const char *arg;
    if(!kui_ftp_parse(line, verb, &arg)) return false;
    return !strcmp(verb, "ABOR") || !strcmp(verb, "QUIT") || !strcmp(verb, "NOOP") || (!strcmp(verb, "STAT") && !arg[0]);
}
static bool service(struct server *sv, struct session *s, uint64_t now) {
    bool work = flush(sv, s, now);
    uint8_t state = KUI_W5500_CLOSED;
    if(!kui_w5500_status(&sv->net.chip, s->control, &state)) return work;
    if(s->closing) {
        if(!s->out_len || now - s->closing_ms > STOP_MS || state != KUI_W5500_ESTABLISHED) {
            session_close(sv, s, now, true);
            return true;
        }
        return work;
    }
    if(state != KUI_W5500_ESTABLISHED && state != KUI_W5500_CLOSE_WAIT) { session_close(sv, s, now, false); return true; }
    work |= read_input(sv, s);
    if(transferring(s)) work |= transfer_step(sv, s, now);
    if(s->closing) return true;
    if(now >= s->hold_until) {
        char line[KUI_FTP_LINE_CAP];
        if(next_line(s, line, sizeof(line), false)) {
            if(!transferring(s) || !line[0] || allowed_during_transfer(line)) {
                next_line(s, line, sizeof(line), true);
                if(line[0]) command(sv, s, line, now);
                work = true;
            }
        } else if(state == KUI_W5500_CLOSE_WAIT && !transferring(s)) {
            /* The client has gone and said all it had to say. */
            session_close(sv, s, now, true);
            return true;
        }
    }
    if(!s->logged_in && now - s->opened_ms > LOGIN_MS) {
        reply(s, "421 No login within a minute; closing");
        s->closing = true;
        s->closing_ms = now;
    } else if(!transferring(s) && now - s->last_command > IDLE_MS) {
        reply(s, "421 Idle for ten minutes; closing");
        s->closing = true;
        s->closing_ms = now;
    }
    return work;
}
static bool listen_control(struct server *sv, unsigned c) {
    unsigned socket = CONTROL_FIRST + c;
    sv->control_owner[c] = FREE;
    return kui_w5500_open(&sv->net.chip, socket, KUI_W5500_TCP | KUI_W5500_MR_NODELAY, sv->control_port) &&
        kui_w5500_listen(&sv->net.chip, socket);
}
static bool controls(struct server *sv, uint64_t now) {
    bool work = false;
    for(unsigned c = 0; c < CONTROL_SOCKETS; ++c) {
        unsigned socket = CONTROL_FIRST + c;
        uint8_t state = KUI_W5500_CLOSED;
        if(!kui_w5500_status(&sv->net.chip, socket, &state)) return work;
        if(sv->control_owner[c] == FREE) {
            if(state == KUI_W5500_ESTABLISHED || state == KUI_W5500_CLOSE_WAIT) {
                struct session *free_session = NULL;
                for(unsigned i = 0; i < KUI_FTP_SESSIONS && !free_session; ++i)
                    if(!sv->sessions[i].active) free_session = &sv->sessions[i];
                if(free_session) {
                    sv->control_owner[c] = (int)index_of(sv, free_session);
                    session_open(sv, free_session, socket, now);
                } else {
                    static const char full[] = "421 K-UI serves 3 connections at once; try again later\r\n";
                    uint16_t room = 0;
                    if(kui_w5500_room(&sv->net.chip, socket, &room) && room >= sizeof(full) - 1u)
                        (void)kui_w5500_send(&sv->net.chip, socket, full, sizeof(full) - 1u);
                    (void)kui_w5500_disconnect(&sv->net.chip, socket);
                    sv->control_owner[c] = REJECTING;
                    sv->control_since[c] = now;
                }
                work = true;
            } else if(state == KUI_W5500_CLOSED) {
                if(!listen_control(sv, c)) return work;
                work = true;
            }
        } else if(sv->control_owner[c] == REJECTING) {
            if(state == KUI_W5500_CLOSED || now - sv->control_since[c] > 2000u) {
                if(!listen_control(sv, c)) return work;
                work = true;
            }
        }
    }
    return work;
}

/* ---- Lease, status ---- */
static void lease(struct server *sv, uint64_t now) {
    if(kui_w5500_session_expires_in(&sv->net, now) == 0) { sv->stop_reason = "The network address lease ran out"; return; }
    if(kui_w5500_session_renew_in(&sv->net, now) || now < sv->renew_after_ms) return;
    int j = -1;
    for(unsigned k = 0; k < DATA_SOCKETS && j < 0; ++k)
        if(sv->data_owner[k] == FREE && !sv->data_draining[k]) j = (int)k;
    if(j < 0) return; /* all busy: try again shortly */
    sv->data_owner[j] = RENEWING;
    bool refused = false;
    if(kui_w5500_session_renew(&sv->net, (unsigned)j, RENEW_LIMIT_MS, &refused, sv->cancel)) event(sv, "Address lease renewed");
    else if(refused) sv->stop_reason = "The network refused to renew K-UI's address";
    else {
        event(sv, "Lease renewal: %.60s; trying again in a minute", sv->net.problem);
        sv->renew_after_ms = now + RENEW_RETRY_MS;
    }
    sv->data_owner[j] = FREE;
}
static void publish(struct server *sv, uint64_t now, bool force) {
    if(!force && !sv->changed && now - sv->published_ms < PUBLISH_MS) return;
    if(!force && now - sv->published_ms < PUBLISH_MS / 2u) return;
    struct kui_ftp_status *st = sv->status;
    for(unsigned i = 0; i < KUI_FTP_SESSIONS; ++i) {
        const struct session *s = &sv->sessions[i];
        struct kui_ftp_client *c = &st->clients[i];
        memset(c, 0, sizeof(*c));
        if(!s->active) continue;
        c->active = true;
        c->logged_in = s->logged_in;
        memcpy(c->ip, s->peer, 4);
        if(s->kind == T_RETR || s->kind == T_STOR) {
            c->sending = s->kind == T_RETR;
            c->receiving = s->kind == T_STOR;
            snprintf(c->name, sizeof(c->name), "%s", kui_files_leaf(s->path));
            c->done = s->done;
            c->total = s->total;
            c->rate = s->rate;
        }
    }
    st->lease_left = kui_w5500_session_expires_in(&sv->net, now);
    if(sv->publish) sv->publish(st);
    sv->published_ms = now;
    sv->changed = false;
}

/* ---- Start and stop ---- */
static uint64_t mix(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static const char *password(struct server *sv, uint32_t seed) {
    FIL file;
    FRESULT r = f_open(&file, KUI_FTP_PASSWORD_PATH, FA_READ);
    if(r == FR_OK) {
        char text[80];
        UINT got = 0;
        r = f_read(&file, text, sizeof(text) - 1u, &got);
        bool closed = f_close(&file) == FR_OK;
        if(r != FR_OK || !closed) return "Cannot read KUI/ftp-password.txt";
        text[got] = 0;
        char *start = text, *end = text + got;
        while(*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') ++start;
        while(end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) --end;
        size_t n = (size_t)(end - start);
        bool valid = n && n < KUI_FTP_PASSWORD_CAP && got < sizeof(text) - 1u;
        for(size_t i = 0; valid && i < n; ++i) valid = start[i] >= 0x20 && start[i] <= 0x7e;
        if(!valid) return "KUI/ftp-password.txt must hold 1 to 32 letters, digits or symbols";
        memcpy(sv->password, start, n);
        sv->password[n] = 0;
        return NULL;
    }
    if(r != FR_NO_FILE && r != FR_NO_PATH) return "Cannot read KUI/ftp-password.txt";
    uint64_t x = mix((uint64_t)seed << 32 ^ now_ms(sv));
    for(unsigned i = 0; i < 6; ++i) x = mix(x ^ sv->net.mac[i]);
    snprintf(sv->password, sizeof(sv->password), "%08u", (unsigned)(x % 100000000u));
    (void)f_mkdir("0:/KUI");
    if(f_open(&file, KUI_FTP_PASSWORD_PATH, FA_WRITE | FA_CREATE_NEW) != FR_OK) return "Cannot create KUI/ftp-password.txt";
    char text[KUI_FTP_PASSWORD_CAP + 2u];
    int n = snprintf(text, sizeof(text), "%s\r\n", sv->password);
    UINT wrote = 0;
    r = f_write(&file, text, (UINT)n, &wrote);
    bool closed = f_close(&file) == FR_OK;
    if(r != FR_OK || wrote != (UINT)n || !closed) return "Cannot save KUI/ftp-password.txt";
    event(sv, "New password saved in KUI/ftp-password.txt");
    return NULL;
}
static void stop_all(struct server *sv, const char *why) {
    uint64_t now = now_ms(sv);
    for(unsigned i = 0; i < KUI_FTP_SESSIONS; ++i) {
        struct session *s = &sv->sessions[i];
        if(!s->active) continue;
        if(transferring(s)) fail_transfer(sv, s, now, "426 The server is stopping");
        if(!s->closing) reply(s, "421 %s", why);
        s->closing = true;
    }
    /* A moment for the goodbyes to go out. */
    uint64_t start = now;
    for(bool pending = true; pending && now - start < STOP_MS; now = now_ms(sv)) {
        pending = false;
        for(unsigned i = 0; i < KUI_FTP_SESSIONS; ++i) {
            struct session *s = &sv->sessions[i];
            if(!s->active || !s->out_len) continue;
            flush(sv, s, now);
            pending = pending || s->out_len;
        }
        if(pending) sv->net.port->bus->pause(sv->net.port->bus->ctx, 5);
    }
    for(unsigned i = 0; i < KUI_FTP_SESSIONS; ++i)
        if(sv->sessions[i].active) session_close(sv, &sv->sessions[i], now, true);
}
static void fail(struct server *sv, const char *why) {
    sv->status->state = KUI_FTP_FAILED;
    snprintf(sv->status->message, sizeof(sv->status->message), "%s", why);
    if(sv->log) sv->log("FTP server: %s", why);
}
static void stage(struct server *sv, const char *text) {
    snprintf(sv->status->message, sizeof(sv->status->message), "%s", text);
    if(sv->log) sv->log("FTP server: %s", text);
    publish(sv, now_ms(sv), true);
}
static void dhcp_stage(enum kui_network_stage s);
static struct server *dhcp_server;
static void dhcp_stage(enum kui_network_stage s) {
    if(dhcp_server && (s == KUI_NET_DISCOVER || s == KUI_NET_REQUEST || s == KUI_NET_CONFLICT))
        stage(dhcp_server, s == KUI_NET_CONFLICT ? "Checking that the address is free" : "Asking the router for an address (DHCP)");
}
static bool find_chip(struct server *sv) {
    stage(sv, "Looking for the W5500 on the SCI port");
    if(!kui_w5500_session_find(&sv->net, sv->net.port, sv->log)) { fail(sv, sv->net.problem); return false; }
    static const uint8_t rx[KUI_W5500_SOCKETS] = {4, 4, 4, 1, 1, 1, 1, 0}, tx[KUI_W5500_SOCKETS] = {4, 4, 4, 1, 1, 1, 1, 0};
    if(!kui_w5500_buffers(&sv->net.chip, rx, tx)) { fail(sv, "The W5500 stopped answering"); return false; }
    return true;
}
static bool start_network(struct server *sv) {
    stage(sv, "Waiting for the network cable link");
    if(!kui_w5500_session_link(&sv->net, sv->cancel)) { fail(sv, sv->net.problem); return false; }
    const struct kui_w5500_link *l = &sv->net.link;
    snprintf(sv->status->adapter, sizeof(sv->status->adapter), "W5500 on SCI at %s; %s Mbit/s %s duplex",
        sv->net.port->speed ? sv->net.port->speed(sv->net.level) : "?", l->fast ? "100" : "10", l->full ? "full" : "half");
    sv->status->link = true;
    dhcp_server = sv;
    bool leased = kui_w5500_session_dhcp(&sv->net, true, NULL, sv->log, sv->cancel, dhcp_stage);
    dhcp_server = NULL;
    if(!leased) { fail(sv, sv->net.problem); return false; }
    memcpy(sv->status->ip, sv->net.config.ip, 4);
    for(unsigned c = 0; c < CONTROL_SOCKETS; ++c)
        if(!listen_control(sv, c)) { fail(sv, "The W5500 could not listen for connections"); return false; }
    return true;
}
void kui_ftp_run(const struct kui_w5500_port *port, const struct kui_ftp_options *options,
                 struct kui_ftp_status *out, kui_log_fn log, kui_cancel_fn cancel, kui_ftp_publish_fn publish_fn) {
    if(!out) return;
    memset(out, 0, sizeof(*out));
    out->state = KUI_FTP_STARTING;
    struct server *sv = calloc(1, sizeof(*sv));
    if(!sv || !port || !port->bus) {
        snprintf(out->message, sizeof(out->message), "%s", sv ? "No SCI port for a W5500" : "Insufficient memory for the FTP server");
        out->state = KUI_FTP_FAILED;
        if(publish_fn) publish_fn(out);
        free(sv);
        return;
    }
    sv->net.port = port;
    sv->status = out;
    sv->log = log;
    sv->cancel = cancel;
    sv->publish = publish_fn;
    sv->control_port = options && options->control_port ? options->control_port : KUI_FTP_PORT;
    sv->passive_first = options && options->passive_first ? options->passive_first : KUI_FTP_PASSIVE_FIRST;
    sv->passive_count = options && options->passive_count ? options->passive_count : KUI_FTP_PASSIVE_COUNT;
    out->port = sv->control_port;
    for(unsigned j = 0; j < DATA_SOCKETS; ++j) sv->data_owner[j] = FREE;
    for(unsigned c = 0; c < CONTROL_SOCKETS; ++c) sv->control_owner[c] = FREE;
    const char *problem = NULL;
    if(!find_chip(sv)) problem = sv->status->message;
    else {
        stage(sv, "Opening the SD card");
        if(!(sv->connected = kui_sd_connect())) problem = "SD card unavailable";
        else if(!(sv->mounted = kui_mount(&sv->fs, log))) problem = "Cannot mount SD card";
        else problem = password(sv, options ? options->seed : 0);
        if(problem) fail(sv, problem);
    }
    if(!problem && start_network(sv)) {
        snprintf(out->password, sizeof(out->password), "%s", sv->password);
        out->state = KUI_FTP_READY;
        char where[8] = "";
        if(sv->control_port != KUI_FTP_PORT) snprintf(where, sizeof(where), ":%u", sv->control_port);
        snprintf(out->message, sizeof(out->message), "Ready: ftp://%u.%u.%u.%u%s", out->ip[0], out->ip[1], out->ip[2],
            out->ip[3], where);
        if(log) log("FTP server: %s", out->message);
        publish(sv, now_ms(sv), true);
        while(!sv->stop_reason) {
            if(cancel && cancel()) { sv->stop_reason = "The FTP server was stopped on the Dreamcast"; break; }
            if(sv->net.chip.failed) { sv->stop_reason = "The W5500 stopped answering"; break; }
            uint64_t now = now_ms(sv);
            bool work = controls(sv, now);
            data_poll(sv, now);
            for(unsigned i = 0; i < KUI_FTP_SESSIONS; ++i)
                if(sv->sessions[i].active) work |= service(sv, &sv->sessions[i], now);
            if(now - sv->link_ms > 1000u) {
                struct kui_w5500_link link;
                sv->link_ms = now;
                if(kui_w5500_link(&sv->net.chip, &link) && link.up != out->link) {
                    out->link = link.up;
                    event(sv, link.up ? "Network cable link back" : "Network cable link lost");
                }
                lease(sv, now);
            }
            publish(sv, now, false);
            port->bus->pause(port->bus->ctx, work ? 0 : 1);
        }
        bool asked = cancel && cancel();
        stop_all(sv, asked ? "The FTP server on the Dreamcast is stopping" : sv->stop_reason);
        out->state = asked ? KUI_FTP_STOPPED : KUI_FTP_FAILED;
        snprintf(out->message, sizeof(out->message), "%s", sv->stop_reason);
        if(log) log("FTP server: %s", sv->stop_reason);
    }
    for(unsigned i = 0; i < KUI_FTP_SESSIONS; ++i) free(sv->sessions[i].buffer);
    kui_w5500_session_end(&sv->net);
    memset(out->clients, 0, sizeof(out->clients));
    out->link = false;
    if(sv->mounted && f_mount(NULL, "0:", 0) != FR_OK && out->state != KUI_FTP_FAILED) {
        out->state = KUI_FTP_FAILED;
        snprintf(out->message, sizeof(out->message), "Cannot release SD filesystem");
    }
    if(sv->connected) kui_sd_disconnect();
    if(publish_fn) publish_fn(out);
    free(sv);
}
