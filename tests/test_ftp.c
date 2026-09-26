/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/ftp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void commands(void) {
    char verb[8];
    const char *arg;
    assert(kui_ftp_parse("USER kui", verb, &arg) && !strcmp(verb, "USER") && !strcmp(arg, "kui"));
    assert(kui_ftp_parse("pwd", verb, &arg) && !strcmp(verb, "PWD") && !strcmp(arg, ""));
    assert(kui_ftp_parse("RETR  two spaces", verb, &arg) && !strcmp(arg, " two spaces"));
    assert(kui_ftp_parse("Stor name with spaces.bin", verb, &arg) && !strcmp(verb, "STOR") &&
           !strcmp(arg, "name with spaces.bin"));
    assert(kui_ftp_parse("CDUP ", verb, &arg) && !strcmp(verb, "CDUP") && !strcmp(arg, ""));
    assert(!kui_ftp_parse("", verb, &arg) && !verb[0]);
    assert(!kui_ftp_parse(" USER", verb, &arg));
    assert(!kui_ftp_parse("TOOLONGVERB x", verb, &arg) && !verb[0]);
    assert(!kui_ftp_parse("US3R x", verb, &arg));
    assert(!kui_ftp_parse(NULL, verb, &arg) && !kui_ftp_parse("NOOP", NULL, &arg));
    puts("PASS FTP commands");
}
static void resolve(const char *folder, const char *argument, const char *wanted) {
    char out[KUI_FILES_PATH_CAP];
    bool ok = kui_ftp_resolve(out, folder, argument);
    if(wanted ? !ok || strcmp(out, wanted) : ok) {
        fprintf(stderr, "resolve(%s, %s): %s '%s', wanted %s\n", folder, argument, ok ? "ok" : "refused", out,
            wanted ? wanted : "refusal");
        assert(0);
    }
    if(!wanted) assert(!out[0]);
}
static void paths(void) {
    resolve("/", "", "/");
    resolve("/Games", "", "/Games");
    resolve("/Games", ".", "/Games");
    resolve("/Games", "..", "/");
    resolve("/", "..", "/");
    resolve("/", "../../..", "/");
    resolve("/Games", "Crazy Taxi", "/Games/Crazy Taxi");
    resolve("/Games", "Crazy Taxi/disc.gdi", "/Games/Crazy Taxi/disc.gdi");
    resolve("/Games", "/KUI/covers", "/KUI/covers");
    resolve("/Games", "//KUI///covers/", "/KUI/covers");
    resolve("/Games/a", "../b/./c", "/Games/b/c");
    resolve("/Games/a", "../../..", "/");
    resolve("/", "caf\xc3\xa9", "/caf\xc3\xa9");
    resolve("/", "a b", "/a b");
    resolve("/", "a:b", NULL);
    resolve("/", "a\\b", NULL);
    resolve("/", "con", NULL);
    resolve("/", "trailing.", NULL);
    resolve("/", "trailing ", NULL);
    resolve("/", "tab\tname", NULL);
    resolve("/", "bad\xc3", NULL);
    resolve("/", "q?", NULL);
    resolve("Games", "x", NULL);
    resolve("/Games/", "x", NULL);
    char name[KUI_FILES_NAME_CAP + 1];
    memset(name, 'n', sizeof(name) - 1u);
    name[sizeof(name) - 1u] = 0;
    resolve("/", name, NULL);
    name[KUI_FILES_NAME_CAP - 1u] = 0;
    char wanted[KUI_FILES_NAME_CAP + 2];
    snprintf(wanted, sizeof(wanted), "/%s", name);
    resolve("/", name, wanted);
    /* A path just short of the limit, then one name too many. */
    char deep[KUI_FILES_PATH_CAP];
    strcpy(deep, "/");
    while(strlen(deep) + 11u < KUI_FILES_PATH_CAP - 1u) strcat(deep, deep[1] ? "/abcdefghi" : "abcdefghij");
    assert(kui_files_path_valid(deep));
    char parent[KUI_FILES_PATH_CAP];
    assert(kui_files_parent(parent, deep));
    resolve(deep, "..", parent);
    resolve(deep, "abcdefghijklmnop", NULL);
    resolve(parent, "../x/../abcdefghi", parent); /* parent ends in abcdefghi */
    puts("PASS FTP paths");
}
static void addresses(void) {
    uint8_t ip[4];
    uint16_t port;
    bool v6;
    assert(kui_ftp_parse_port("192,168,1,20,195,80", ip, &port) && ip[0] == 192 && ip[3] == 20 && port == 50000);
    assert(!kui_ftp_parse_port("192,168,1,20,195", ip, &port));
    assert(!kui_ftp_parse_port("192,168,1,256,1,1", ip, &port));
    assert(!kui_ftp_parse_port("192,168,1,20,195,80,1", ip, &port));
    assert(!kui_ftp_parse_port("192,168,1,20,195,80 ", ip, &port));
    assert(!kui_ftp_parse_port("a,b,c,d,e,f", ip, &port) && !kui_ftp_parse_port("", ip, &port));
    assert(kui_ftp_parse_eprt("|1|10.0.0.9|6446|", ip, &port, &v6) && !v6 && ip[0] == 10 && ip[3] == 9 && port == 6446);
    assert(kui_ftp_parse_eprt("!1!10.0.0.9!6446!", ip, &port, &v6));
    assert(!kui_ftp_parse_eprt("|2|::1|6446|", ip, &port, &v6) && v6);
    assert(!kui_ftp_parse_eprt("|1|10.0.0|6446|", ip, &port, &v6) && !v6);
    assert(!kui_ftp_parse_eprt("|1|10.0.0.9|65536|", ip, &port, &v6));
    assert(!kui_ftp_parse_eprt("|1|10.0.0.9|0|", ip, &port, &v6));
    assert(!kui_ftp_parse_eprt("|1|10.0.0.9|6446", ip, &port, &v6));
    assert(!kui_ftp_parse_eprt("1|10.0.0.9|6446|", ip, &port, &v6));
    puts("PASS FTP PORT and EPRT");
}
static void listings(void) {
    uint16_t date = (uint16_t)((45u << 9) | (9u << 5) | 26u), time = (uint16_t)((13u << 11) | (5u << 5) | 21u);
    uint16_t this_year = (uint16_t)((45u << 9) | (1u << 5) | 1u), last_year = (uint16_t)((44u << 9) | (1u << 5) | 1u);
    char line[1024], stamp[15];
    assert(kui_ftp_timestamp(stamp, date, time) && !strcmp(stamp, "20250926130542"));
    assert(!kui_ftp_timestamp(stamp, 0, 0) && !strcmp(stamp, "19800101000000"));
    assert(!kui_ftp_timestamp(stamp, (uint16_t)((45u << 9) | (13u << 5) | 1u), 0));
    size_t n = kui_ftp_list_line(line, sizeof(line), "track01.bin", 123456789, date, time, false, false, this_year);
    assert(n == strlen(line) && !strcmp(line, "-rw-r--r-- 1 kui kui     123456789 Sep 26 13:05 track01.bin\r\n"));
    kui_ftp_list_line(line, sizeof(line), "Games", 0, date, time, true, false, last_year);
    assert(!strcmp(line, "drwxr-xr-x 1 kui kui             0 Sep 26  2025 Games\r\n"));
    kui_ftp_list_line(line, sizeof(line), "runtime.kui", 5, 0, 0, false, true, this_year);
    assert(!strcmp(line, "-r--r--r-- 1 kui kui             5 Jan  1  1980 runtime.kui\r\n"));
    kui_ftp_list_line(line, sizeof(line), "big", UINT64_C(5000000000), date, time, false, false, this_year);
    assert(strstr(line, " 5000000000 Sep 26 13:05 big\r\n"));
    assert(!kui_ftp_list_line(line, 40, "track01.bin", 1, date, time, false, false, this_year));
    assert(!kui_ftp_list_line(line, sizeof(line), "", 1, date, time, false, false, this_year));
    n = kui_ftp_mlsd_line(line, sizeof(line), "track01.bin", 123, date, time, false);
    assert(n == strlen(line) && !strcmp(line, "type=file;size=123;modify=20250926130542; track01.bin\r\n"));
    kui_ftp_mlsd_line(line, sizeof(line), "Crazy Taxi", 0, date, time, true);
    assert(!strcmp(line, "type=dir;modify=20250926130542; Crazy Taxi\r\n"));
    assert(!kui_ftp_mlsd_line(line, 20, "Crazy Taxi", 0, date, time, true));
    assert(!strcmp(kui_ftp_list_path("-la"), "") && !strcmp(kui_ftp_list_path("-la /Games"), "/Games"));
    assert(!strcmp(kui_ftp_list_path("-a -l Crazy Taxi"), "Crazy Taxi") && !strcmp(kui_ftp_list_path("Games"), "Games"));
    assert(!strcmp(kui_ftp_list_path(""), "") && !strcmp(kui_ftp_list_path(NULL), ""));
    puts("PASS FTP listing lines");
}
int main(void) {
    commands();
    paths();
    addresses();
    listings();
    puts("PASS FTP protocol");
    return 0;
}
