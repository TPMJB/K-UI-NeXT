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
static char last[160], transcript[4096];
static void log_line(const char *format, ...) {
    va_list args; va_start(args, format);
    vsnprintf(last, sizeof(last), format, args);
    va_end(args); ++logged;
    size_t used = strlen(transcript);   /* every line, so echoes can be searched */
    snprintf(transcript + used, sizeof(transcript) - used, "%s\n", last);
}
static bool parse(struct kui_options *o, const char *text) {
    return kui_options_parse(o, text, strlen(text), log_line);
}

int main(int argc, char **argv) {
    struct kui_options o, d;
    kui_options_default(&d);
    assert(d.chunk_count == 3 && d.chunks[0] == 32 && d.chunks[2] == 512);
    assert(d.sd_mib == 8 && d.hash_mib == 8 && d.expand);
    assert(d.optical_fad == 45150 && d.optical_sectors == 4096 && d.yield_us == 2000);
    /* Defaults must equal what KOS's own sd_init() does, so reading bench.cfg
     * never depends on the setting bench.cfg is being read to discover. */
    assert(d.sd_if_count == 1 && d.sd_if[0] == 0);
    assert(d.sd_crc_count == 1 && d.sd_crc[0]);
    assert(!d.note[0]);
    /* Experiment keys default to the bench as it was: nothing new runs. */
    assert(d.ui_count == 1 && d.ui_hz[0] == 2);   /* measured: 2 Hz costs ~5%, full costs ~31% */
    assert(d.sections == (KUI_SEC_OPTICAL | KUI_SEC_HASH | KUI_SEC_SD));
    assert(d.sweep_chunk_count == 0 && d.sweep_fad_count == 0);
    assert(d.sweep_gap_count == 0 && d.sweep_service_count == 0);
    assert(d.sweep_sectors == 2048 && d.sweep_verify && d.sd_bytes_count == 0);
    /* The capture engine defaults to what it has always done. */
    assert(d.capture_hash_count == 1 && !d.capture_crc_only[0]);
    assert(d.end_readback_count == 1 && d.end_readback[0]);
    assert(d.resume_check_count == 1 && !d.resume_size[0]);
    assert(d.sample_readback_count == 1 && d.sample_readback[0] == 0);
    assert(d.capture_sectors == 4096 && d.capture_fad == 0 && !d.capture_audio);

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
        "sd_if=sci,scif\n"
        "sd_crc=off\n"
        "note=SanDisk 32GB, cold drive"));
    assert(logged == 0);
    assert(o.chunk_count == 2 && o.chunks[0] == 64 && o.chunks[1] == 256);
    assert(o.sd_mib == 16 && !o.expand && o.hash_mib == 4);
    assert(o.optical_fad == 70040 && o.optical_sectors == 2048 && o.yield_us == 1000);
    assert(o.sd_if_count == 2 && o.sd_if[0] == 1 && o.sd_if[1] == 0);
    assert(o.sd_crc_count == 1 && !o.sd_crc[0]);
    assert(!strcmp(o.note, "SanDisk 32GB, cold drive"));

    /* The transport is a word, and only these two words. */
    o = d; assert(parse(&o, "sd_if=scif") && o.sd_if_count == 1 && o.sd_if[0] == 0);
    o = d; assert(parse(&o, "sd_if=sci") && o.sd_if_count == 1 && o.sd_if[0] == 1);
    o = d; assert(parse(&o, "sd_if=scif,sci") && o.sd_if_count == 2);
    o = d; assert(parse(&o, "sd_crc=on,off") && o.sd_crc_count == 2 && o.sd_crc[0] && !o.sd_crc[1]);
    /* A repeated setting would produce two identically-labelled report lines. */
    o = d; assert(parse(&o, "sd_if=sci,sci") == false && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "sd_crc=on,on") == false && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "sd_if=scif,sci,scif") == false && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "sd_if=scif,") == false && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "sd_if=SCI") == false && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "sd_if=1") == false && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "sd_if=scifx") == false && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "sd_crc=off") && !o.sd_crc[0]);

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


    /* --- experiment keys ------------------------------------------------- */
    o = d; assert(parse(&o, "ui_hz=full,4,0"));
    assert(o.ui_count == 3 && o.ui_hz[0] == KUI_OPT_UI_FULL && o.ui_hz[1] == 4 && o.ui_hz[2] == 0);
    o = d; assert(parse(&o, "ui_hz = 30 , full") && o.ui_count == 2 && o.ui_hz[0] == 30);
    o = d; assert(!parse(&o, "ui_hz=31") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "ui_hz=full,full") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "ui_hz=") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "ui_hz=fast") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "ui_hz=Full") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "ui_hz=1,2,3,4,5") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "ui_hz=4,") && !memcmp(&o, &d, sizeof(o)));

    o = d; assert(parse(&o, "sections=sweep") && o.sections == KUI_SEC_SWEEP);
    o = d; assert(parse(&o, "sections=sd, hash") && o.sections == (KUI_SEC_SD | KUI_SEC_HASH));
    o = d; assert(parse(&o, "sections=optical,hash,sd,sweep") && o.sections == 15u);
    o = d; assert(!parse(&o, "sections=sd,sd") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sections=disc") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sections=") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sections=sd,") && !memcmp(&o, &d, sizeof(o)));

    o = d; assert(parse(&o, "sweep_chunks=8,16,32,64,128") && o.sweep_chunk_count == 5 &&
                  o.sweep_chunks[0] == 8 && o.sweep_chunks[4] == 128);
    o = d; assert(!parse(&o, "sweep_chunks=129") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sweep_chunks=0") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sweep_chunks=32,32") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sweep_chunks=1,2,3,4,5,6,7,8,9") && !memcmp(&o, &d, sizeof(o)));

    o = d; assert(parse(&o, "sweep_fads=45150,150000,400000") && o.sweep_fad_count == 3 &&
                  o.sweep_fads[2] == 400000);
    o = d; assert(!parse(&o, "sweep_fads=149") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sweep_fads=16777216") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sweep_fads=150,151,152,153,154") && !memcmp(&o, &d, sizeof(o)));

    o = d; assert(parse(&o, "sweep_gap_us=0,5000,15000,100000") && o.sweep_gap_count == 4);
    o = d; assert(!parse(&o, "sweep_gap_us=100001") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "sweep_service_us=0,500,50000") && o.sweep_service_count == 3);
    o = d; assert(!parse(&o, "sweep_service_us=50001") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "sweep_sectors=32") && o.sweep_sectors == 32);
    o = d; assert(!parse(&o, "sweep_sectors=31") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "sweep_verify=off") && !o.sweep_verify);

    /* SD write sizes are bytes: whole 512-byte blocks, 4 KiB .. the bench buffer. */
    o = d; assert(parse(&o, "sd_bytes=65536,131072,524288,1048576") && o.sd_bytes_count == 4);
    o = d; assert(parse(&o, "sd_bytes=4096") && o.sd_bytes[0] == 4096);
    o = d; assert(parse(&o, "sd_bytes=1204224") && o.sd_bytes[0] == 1204224u);   /* 512 sectors */
    o = d; assert(!parse(&o, "sd_bytes=1204736") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sd_bytes=2048") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sd_bytes=4100") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sd_bytes=131072,131072") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sd_bytes=1,2,3,4,5,6,7") && !memcmp(&o, &d, sizeof(o)));

    /* --- sweep_mode / sweep_spin ---------------------------------------------- */
    assert(d.sweep_mode_count == 1 && !d.sweep_dma[0] && d.sweep_spin_count == 1 && !d.sweep_spin[0]);
    o = d; assert(parse(&o, "sweep_mode=pio,dma") && o.sweep_mode_count == 2 && !o.sweep_dma[0] && o.sweep_dma[1]);
    o = d; assert(parse(&o, "sweep_mode = dma") && o.sweep_mode_count == 1 && o.sweep_dma[0]);
    o = d; assert(!parse(&o, "sweep_mode=dma,dma") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sweep_mode=pio,dma,pio") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sweep_mode=irq") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sweep_mode=") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "sweep_spin=off,on") && o.sweep_spin_count == 2 && !o.sweep_spin[0] && o.sweep_spin[1]);
    o = d; assert(parse(&o, "sweep_spin=on") && o.sweep_spin[0]);
    o = d; assert(!parse(&o, "sweep_spin=on,on") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sweep_spin=sometimes") && !memcmp(&o, &d, sizeof(o)));
    o = d; logged = 0; transcript[0] = 0;
    assert(parse(&o, "sweep_mode=pio,dma\nsweep_spin=off,on\n"));
    kui_options_log(&o, log_line);
    assert(strstr(transcript, "OPTIONS sweep_mode=pio,dma sweep_spin=off,on"));
    logged = 0; transcript[0] = 0; kui_options_log(&d, log_line);
    assert(strstr(transcript, "OPTIONS sweep_mode=pio sweep_spin=off"));
    /* ui_hz=full is still a valid, explicit choice, and is what says so in the report. */
    o = d; assert(parse(&o, "ui_hz=full") && o.ui_hz[0] == KUI_OPT_UI_FULL);

    /* --- capture engine keys ------------------------------------------------ */
    o = d; assert(parse(&o, "capture_hash=both,crc32") && o.capture_hash_count == 2 &&
                  !o.capture_crc_only[0] && o.capture_crc_only[1]);
    o = d; assert(parse(&o, "capture_hash = crc32") && o.capture_hash_count == 1 && o.capture_crc_only[0]);
    o = d; assert(!parse(&o, "capture_hash=sha256") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "capture_hash=both,both") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "capture_hash=both,crc32,both") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "capture_hash=") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "end_readback=on,off") && o.end_readback_count == 2 && o.end_readback[0] && !o.end_readback[1]);
    o = d; assert(parse(&o, "end_readback=off") && !o.end_readback[0]);
    o = d; assert(!parse(&o, "end_readback=maybe") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "resume_check=full,size") && o.resume_check_count == 2 && !o.resume_size[0] && o.resume_size[1]);
    o = d; assert(!parse(&o, "resume_check=sample") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "sample_readback=0,16,32,1024") && o.sample_readback_count == 4);
    o = d; assert(!parse(&o, "sample_readback=1025") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sample_readback=0,0") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "sample_readback=1,2,3,4,5") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "capture_sectors=64") && o.capture_sectors == 64);
    o = d; assert(parse(&o, "capture_sectors=262144") && o.capture_sectors == 262144);
    o = d; assert(!parse(&o, "capture_sectors=63") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "capture_sectors=262145") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "capture_fad=45150") && o.capture_fad == 45150);
    o = d; assert(parse(&o, "capture_fad=0") && o.capture_fad == 0);
    o = d; assert(!parse(&o, "capture_fad=149") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(!parse(&o, "capture_fad=16777216") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "capture_type=audio") && o.capture_audio);
    o = d; assert(parse(&o, "capture_type=data") && !o.capture_audio);
    o = d; assert(!parse(&o, "capture_type=mixed") && !memcmp(&o, &d, sizeof(o)));
    o = d; assert(parse(&o, "sections=capture") && o.sections == KUI_SEC_CAPTURE);
    o = d; assert(parse(&o, "sections=optical,hash,sd,sweep,capture") && o.sections == 31u);
    o = d; logged = 0; transcript[0] = 0;
    assert(parse(&o, "capture_hash=both,crc32\nend_readback=on,off\nresume_check=full,size\n"
        "sample_readback=0,32\ncapture_sectors=2048\ncapture_fad=300000\ncapture_type=audio\n"));
    kui_options_log(&o, log_line);
    assert(strstr(transcript, "OPTIONS capture_hash=both,crc32 end_readback=on,off resume_check=full,size"));
    assert(strstr(transcript, "OPTIONS sample_readback=0,32 capture_sectors=2048 capture_type=audio"));
    assert(strstr(transcript, "OPTIONS capture_fad=300000"));
    logged = 0; transcript[0] = 0; kui_options_log(&d, log_line);
    assert(strstr(transcript, "OPTIONS capture_hash=both end_readback=on resume_check=full"));
    assert(strstr(transcript, "OPTIONS sample_readback=0 capture_sectors=4096 capture_type=data"));
    assert(strstr(transcript, "OPTIONS capture_fad=(optical_fad)"));

    /* A whole experiment file, and the echo that documents it in the report. */
    o = d; logged = 0; transcript[0] = 0;
    assert(parse(&o,
        "ui_hz=full,1,0\nsections=sweep\nsweep_chunks=8,32,128\nsweep_fads=45150,300000\n"
        "sweep_gap_us=0,5000\nsweep_service_us=0,2000\nsweep_sectors=1024\nsweep_verify=off\n"
        "sd_bytes=131072,524288\n"));
    assert(logged == 0);
    kui_options_log(&o, log_line);
    assert(strstr(transcript, "OPTIONS ui_hz=full,1,0 sections=sweep"));
    assert(strstr(transcript, "OPTIONS sweep_chunks=8,32,128 sweep_sectors=1024 verify=off"));
    assert(strstr(transcript, "OPTIONS sweep_fads=45150,300000"));
    assert(strstr(transcript, "OPTIONS sweep_gap_us=0,5000 sweep_service_us=0,2000"));
    assert(strstr(transcript, "OPTIONS sd_bytes=131072,524288"));

    /* Defaults echo as "off/none" so a report never hides a sweep that ran. */
    logged = 0; transcript[0] = 0; kui_options_log(&d, log_line);
    assert(strstr(transcript, "OPTIONS ui_hz=2 sections=optical,hash,sd"));
    assert(strstr(transcript, "sweep_chunks=(sweep off)"));
    assert(strstr(transcript, "sweep_fads=(optical_fad)"));
    assert(strstr(transcript, "sd_bytes=(none)"));

    /* The echo prints the list in file order. */
    o = d; logged = 0; kui_options_log(&o, log_line);
    assert(logged == 13 && strstr(last, "note=(none)"));

    /* Every example config we ship (paths come from the Makefile) must parse
     * with no complaint, so a later change to a limit cannot silently break a
     * file a user is told to copy to their card. */
    unsigned shipped = 0;
    for(int i = 1; i < argc; ++i) {
        static char text[8192];
        FILE *file = fopen(argv[i], "rb");
        assert(file);
        size_t used = fread(text, 1, sizeof(text), file);
        assert(!ferror(file) && used < sizeof(text));
        fclose(file);
        o = d; logged = 0;
        if(!kui_options_parse(&o, text, used, log_line)) { fprintf(stderr, "%s: %s\n", argv[i], last); assert(!"shipped config rejected"); }
        assert(logged == 0);
        ++shipped;
    }
    printf("PASS options: defaults, full file, transport, booleans, unknown keys, rejection, note bounds, experiment keys, %u shipped configs\n", shipped);
    return 0;
}
