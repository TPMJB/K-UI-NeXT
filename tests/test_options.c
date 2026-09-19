/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/options.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The parser only ever sees text and a log function, so the whole test is
 * strings in, struct out. Log lines are counted to check that unknown keys
 * and bad values are reported, not swallowed. */
static unsigned logged;
static char last[160];
static void log_line(const char *format, ...) {
    va_list args; va_start(args, format);
    vsnprintf(last, sizeof(last), format, args);
    va_end(args); ++logged;
}
static bool parse(struct kui_options *o, const char *text) {
    return kui_options_parse(o, text, strlen(text), log_line);
}

int main(void) {
    struct kui_options o, d;
    kui_options_default(&d);
    assert(d.chunk_count == 3 && d.chunks[0] == 32 && d.chunks[2] == 512);
    assert(d.sd_mib == 8 && d.hash_mib == 8 && d.expand);
    assert(d.optical_fad == 45150 && d.optical_sectors == 4096 && d.yield_us == 2000);
    assert(!d.note[0]);

    /* Empty and comment-only files leave defaults untouched. */
    o = d; assert(parse(&o, "")); assert(!memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "# only a comment\n\n   \n")); assert(!memcmp(&o, &d, sizeof(o)));

    /* A full file, with spaces, CRLF endings and a trailing line without \n. */
    o = d; logged = 0;
    assert(parse(&o,
        "# card A\r\n"
        "chunks = 64, 256\r\n"
        "sd_mib=16\r\n"
        "expand=off\r\n"
        "hash_mib=4\n"
        "optical_fad=70040\n"
        "optical_sectors=2048\n"
        "yield_us=1000\n"
        "note=SanDisk 32GB, cold drive"));
    assert(logged == 0);
    assert(o.chunk_count == 2 && o.chunks[0] == 64 && o.chunks[1] == 256);
    assert(o.sd_mib == 16 && !o.expand && o.hash_mib == 4);
    assert(o.optical_fad == 70040 && o.optical_sectors == 2048 && o.yield_us == 1000);
    assert(!strcmp(o.note, "SanDisk 32GB, cold drive"));

    /* Every accepted spelling of a boolean. */
    o = d; assert(parse(&o, "expand=0") && !o.expand);
    o = d; assert(parse(&o, "expand=yes") && o.expand);
    o = d; assert(parse(&o, "expand=false") && !o.expand);
    o = d; assert(parse(&o, "expand=maybe") == false && !memcmp(&o, &d, sizeof(o)));

    /* Unknown keys are logged and ignored; the rest of the file still applies. */
    o = d; logged = 0;
    assert(parse(&o, "colour=blue\nsd_mib=2\n"));
    assert(logged == 1 && strstr(last, "unknown key") && o.sd_mib == 2);

    /* Any bad value rejects the whole file: nothing partial is ever applied. */
    o = d; logged = 0;
    assert(!parse(&o, "sd_mib=2\nchunks=32,9999\n"));
    assert(logged == 1 && strstr(last, "line 2") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "chunks=\n") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "chunks=32,\n") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "chunks=1,2,3,4,5,6,7,8,9\n") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "yield_us=99\n") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "optical_fad=149\n") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sd_mib=16x\n") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "just words\n") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "=5\n") && !memcmp(&o, &d, sizeof(o)));

    /* Notes longer than the field are cut, never overflowed. */
    char longnote[200] = "note=";
    memset(longnote + 5, 'x', 150); longnote[155] = 0;
    o = d; assert(parse(&o, longnote) && strlen(o.note) == KUI_OPT_NOTE_MAX - 1);

    /* The echo prints the list in file order. */
    o = d; logged = 0; kui_options_log(&o, log_line);
    assert(logged == 3 && strstr(last, "note=(none)"));

    puts("PASS options: defaults, full file, booleans, unknown keys, rejection, note bounds");
    return 0;
}
