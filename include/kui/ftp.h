/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_FTP_H
#define KUI_FTP_H
#include "kui/apps.h"
#include "kui/files.h"
#include "kui/network_w5500.h"
#include <stddef.h>
#include <stdint.h>

/* K-UI's FTP server: the SD card over the network through a W5500 on the
 * SCI port, using the chip's own TCP sockets. RFC 959 with the usual
 * extensions (RFC 2389 FEAT, RFC 2428 EPSV/EPRT, RFC 3659 SIZE, MDTM,
 * REST and MLSD/MLST). Plain FTP only: the password and the files cross
 * the network unencrypted, so it is meant for a home network, and runs
 * only while its screen is open.
 *
 * Paths follow the File Manager's rules (kui/files.h): FAT-safe UTF-8
 * names under 128 bytes, paths under 384. Uploads are written beside
 * their target as KUI-ftp-<n>.kui-part and take their name only once the
 * client has sent all of it; K-UI's own start-up files cannot be replaced,
 * renamed or deleted. */
#define KUI_FTP_PORT 21u
#define KUI_FTP_PASSIVE_FIRST 50000u
#define KUI_FTP_PASSIVE_COUNT 1000u
/* Clients at once: FileZilla, for one, browses on one connection and
 * transfers on up to two more. */
#define KUI_FTP_SESSIONS 3u
#define KUI_FTP_LINE_CAP 512u
/* The password: letters, digits and symbols, 1 to 32 of them, in this file
 * on the card; made (8 random digits) when missing. */
#define KUI_FTP_PASSWORD_PATH "0:/KUI/ftp-password.txt"
#define KUI_FTP_PASSWORD_CAP 33u
#define KUI_FTP_EVENTS 4u

/* Tests choose free ports; the console uses the defaults (zero). */
struct kui_ftp_options {
    uint16_t control_port, passive_first, passive_count;
    uint32_t seed; /* mixed into a new password */
};
enum kui_ftp_state { KUI_FTP_STARTING, KUI_FTP_READY, KUI_FTP_STOPPED, KUI_FTP_FAILED };
struct kui_ftp_client {
    bool active, logged_in, sending, receiving;
    uint8_t ip[4];
    char name[KUI_FILES_NAME_CAP]; /* the file moving, if any */
    uint64_t done, total;          /* total 0: not known (uploads) */
    uint32_t rate;                 /* bytes per second */
};
struct kui_ftp_status {
    enum kui_ftp_state state;
    char message[128];
    char adapter[KUI_APP_LINE_CAP];
    char password[KUI_FTP_PASSWORD_CAP];
    uint8_t ip[4];
    uint16_t port;
    bool link;
    uint32_t lease_left; /* seconds; UINT32_MAX when it never ends */
    struct kui_ftp_client clients[KUI_FTP_SESSIONS];
    unsigned connections, files_in, files_out, failures;
    uint64_t bytes_in, bytes_out;
    /* Recent events, newest first. */
    char events[KUI_FTP_EVENTS][KUI_APP_LINE_CAP];
    unsigned event_count;
};
typedef void (*kui_ftp_publish_fn)(const struct kui_ftp_status *status);
/* Mounts the card, finds the W5500, takes a DHCP lease and serves until
 * cancel() says stop or the network is lost. The card must be reachable
 * through kui_sd_connect (on SCIF). Publishes its status as it changes. */
void kui_ftp_run(const struct kui_w5500_port *port, const struct kui_ftp_options *options,
                 struct kui_ftp_status *out, kui_log_fn log, kui_cancel_fn cancel, kui_ftp_publish_fn publish);

/* Protocol pieces, kept apart from the sockets and the card for tests. */
/* The command word, upper-cased (at most 7 letters), and its argument:
 * everything after the first space ("" when none). */
bool kui_ftp_parse(const char *line, char verb[8], const char **argument);
/* An FTP path, absolute or relative to `folder`, as a card path: "." and
 * ".." resolved (never above the root), repeated and trailing slashes
 * dropped, every name FAT-safe and the whole a valid File Manager path. */
bool kui_ftp_resolve(char out[KUI_FILES_PATH_CAP], const char *folder, const char *argument);
/* LIST and NLST take ls options ("-la") before an optional path. */
const char *kui_ftp_list_path(const char *argument);
/* A LIST or NLST path whose last name holds * or ? lists the folder before
 * it, filtered: folder gets the rest of the path ("" for the current
 * folder), pattern the last name. False when there is no pattern. */
bool kui_ftp_split_pattern(const char *path, char folder[KUI_FTP_LINE_CAP], char pattern[KUI_FILES_NAME_CAP]);
/* * any run of characters, ? any one; ASCII letters match either case,
 * as FAT names do. */
bool kui_ftp_glob(const char *pattern, const char *name);
/* PORT h1,h2,h3,h4,p1,p2 and EPRT |1|a.b.c.d|port| (ipv6 set for |2|). */
bool kui_ftp_parse_port(const char *argument, uint8_t ip[4], uint16_t *port);
bool kui_ftp_parse_eprt(const char *argument, uint8_t ip[4], uint16_t *port, bool *ipv6);
/* FAT date and time as YYYYMMDDHHMMSS; false for a missing or impossible
 * date (then 19800101000000). */
bool kui_ftp_timestamp(char out[15], uint16_t date, uint16_t time);
/* One line of a listing, CRLF included; the length, or 0 when it does not
 * fit. LIST uses ls -l's layout: the time within the year of `today` (a
 * FAT date), the year otherwise. */
size_t kui_ftp_list_line(char *out, size_t cap, const char *name, uint64_t bytes, uint16_t date, uint16_t time,
                         bool directory, bool read_only, uint16_t today);
size_t kui_ftp_mlsd_line(char *out, size_t cap, const char *name, uint64_t bytes, uint16_t date, uint16_t time,
                         bool directory);
#endif
