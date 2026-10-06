/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/capture.h"
#include "kui/game_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void digest(const void *p,size_t n,size_t chunk,const char *want) {
    struct kui_sha256 s;kui_sha256_init(&s);
    for(size_t pos=0;pos<n;) {
        size_t count=n-pos<chunk?n-pos:chunk;kui_sha256_update(&s,(const uint8_t *)p+pos,count);pos+=count;
    }
    uint8_t d[32];char h[65];kui_sha256_digest(&s,d);kui_hex(d,32,h);assert(!strcmp(h,want));
    kui_sha256_digest(&s,d);kui_hex(d,32,h);assert(!strcmp(h,want));
}
static uint32_t edc_reference(const uint8_t *p,size_t n) {
    uint32_t crc=0;
    for(size_t i=0;i<n;i++) {
        crc^=p[i];for(unsigned j=0;j<8;j++) crc=crc&1?(crc>>1)^0xd8018001u:crc>>1;
    }
    return crc;
}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8));}
static void seal(uint8_t *record) {put32(record+4092,kui_crc32(0,record,4092));}
static struct {const struct kui_capture_plan *plan;const char *cue;size_t size;} cue_fixture;
static enum kui_game_result cue_stat(void *ctx,const char *name,uint64_t *bytes) {
    (void)ctx;
    if(!strcmp(name,"disc.cue")) {*bytes=cue_fixture.size;return KUI_GAME_OK;}
    for(unsigned i=0;i<cue_fixture.plan->count;i++) {
        char expected[32];const struct kui_capture_track *t=&cue_fixture.plan->tracks[i];
        snprintf(expected,sizeof(expected),"track%02u.%s",i+1u,t->control==4u?"bin":"raw");
        if(!strcmp(name,expected)) {*bytes=(uint64_t)(t->end-t->start)*2352u;return KUI_GAME_OK;}
    }
    return KUI_GAME_NOT_FOUND;
}
static enum kui_game_result cue_read(void *ctx,const char *name,uint64_t offset,void *out,size_t size) {
    (void)ctx;
    if(strcmp(name,"disc.cue") || offset>cue_fixture.size || size>cue_fixture.size-offset) return KUI_GAME_RANGE;
    memcpy(out,cue_fixture.cue+(size_t)offset,size);return KUI_GAME_OK;
}
int main(void) {
    digest("",0,1,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    digest("abc",3,1,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char *long_text="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    for(size_t c=1;c<=70;c++) digest(long_text,strlen(long_text),c,"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    static char million[1000000];memset(million,'a',sizeof(million));
    digest(million,sizeof(million),1003,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    uint8_t raw[2352]={0};memset(raw+1,255,10);raw[15]=1;
    for(unsigned i=16;i<2064;i++) raw[i]=(uint8_t)i;
    put32(raw+2064,edc_reference(raw,2064));assert(kui_sector_edc_valid(raw));
    raw[31]^=1;assert(!kui_sector_edc_valid(raw));raw[31]^=1;
    memset(raw+16,0,8);raw[15]=2;put32(raw+2072,edc_reference(raw+16,2056));assert(kui_sector_edc_valid(raw));
    raw[18]=raw[22]=0x20;assert(!kui_sector_edc_valid(raw));
    assert(kui_cd_edc((const uint8_t *)million,sizeof(million))==edc_reference((const uint8_t *)million,sizeof(million)));
    struct kui_toc sessions[2]={ {.tracks={{1,4,150,650},{2,0,650,950}},.count=2},
        {.tracks={{3,4,45150,50000},{4,0,50000,50400},{5,0,50400,51000},{6,4,51000,51813}},.count=4} };
    struct kui_capture_plan plan;assert(kui_plan_tracks(sessions,&plan));
    assert(plan.count==6 && plan.tracks[0].end==500 && plan.tracks[1].end==950);
    assert(plan.tracks[2].end==49850 && plan.tracks[3].end==50400 && plan.tracks[4].end==50850 && plan.tracks[5].end==51813);
    sessions[1].tracks[1].number=9;assert(!kui_plan_tracks(sessions,&plan));sessions[1].tracks[1].number=4;
    sessions[0].tracks[0].end=200;assert(!kui_plan_tracks(sessions,&plan));sessions[0].tracks[0].end=650;
    assert(kui_plan_tracks(sessions,&plan));
    struct kui_checkpoint a={.sequence=12,.count=6,.build="0123456789ab"},b;
    uint8_t record[4096];a.identity[0]=19;
    kui_checkpoint_encode(&a,record);assert(kui_checkpoint_decode(record,&plan,a.identity,&b));
    a.track[0].sectors=350;a.track[0].crc32=123;memset(a.track[0].sha256,2,32);
    a.track[1].sectors=16;kui_checkpoint_encode(&a,record);
    assert(kui_checkpoint_decode(record,&plan,a.identity,&b) && b.track[1].sectors==16);
    record[120]^=1;assert(!kui_checkpoint_decode(record,&plan,a.identity,&b));
    kui_checkpoint_encode(&a,record);record[24]^=1;seal(record);assert(!kui_checkpoint_decode(record,&plan,a.identity,&b));
    kui_checkpoint_encode(&a,record);put32(record+96+2*40,1);seal(record);assert(!kui_checkpoint_decode(record,&plan,a.identity,&b));
    kui_checkpoint_encode(&a,record);put32(record+96,351);seal(record);assert(!kui_checkpoint_decode(record,&plan,a.identity,&b));
    kui_checkpoint_encode(&a,record);record[90]=1;seal(record);assert(!kui_checkpoint_decode(record,&plan,a.identity,&b));
    /* CRC-only jobs: the flag round-trips, records carry no SHA, and anything that
     * contradicts it or sets an unknown flag is refused. Records written before
     * the flag existed have zero there and still mean "SHA-256 recorded". */
    struct kui_checkpoint c=a;c.crc_only=true;memset(c.track[0].sha256,0,32);
    kui_checkpoint_encode(&c,record);
    assert(kui_checkpoint_decode(record,&plan,c.identity,&b) && b.crc_only && b.track[0].crc32==123);
    kui_checkpoint_encode(&a,record);
    assert(kui_checkpoint_decode(record,&plan,a.identity,&b) && !b.crc_only && b.track[0].sha256[0]==2);
    kui_checkpoint_encode(&c,record);record[96+8]=1;seal(record);assert(!kui_checkpoint_decode(record,&plan,c.identity,&b));
    kui_checkpoint_encode(&c,record);put32(record+76,10);seal(record);assert(!kui_checkpoint_decode(record,&plan,c.identity,&b));
    kui_checkpoint_encode(&c,record);put32(record+76,16);seal(record);assert(!kui_checkpoint_decode(record,&plan,c.identity,&b));
    kui_checkpoint_encode(&c,record);record[80]=1;seal(record);assert(!kui_checkpoint_decode(record,&plan,c.identity,&b));
    kui_checkpoint_encode(&a,record);put32(record+76,1);seal(record);   /* SHA present but flagged CRC-only */
    assert(!kui_checkpoint_decode(record,&plan,a.identity,&b));
    c.format=KUI_CAPTURE_FORMAT_BIN_CUE;c.track[0].sector_mode=2u;
    kui_checkpoint_encode(&c,record);
    assert(kui_checkpoint_decode(record,&plan,c.identity,&b) && b.format==KUI_CAPTURE_FORMAT_BIN_CUE &&
           b.track[0].sector_mode==2u && b.track[1].sector_mode==0u);
    record[80]|=2u;seal(record);assert(!kui_checkpoint_decode(record,&plan,c.identity,&b)); /* Audio mode bit. */
    kui_checkpoint_encode(&c,record);record[80]|=4u;seal(record);
    assert(!kui_checkpoint_decode(record,&plan,c.identity,&b)); /* Unsaved data mode bit. */
    kui_checkpoint_encode(&c,record);record[92]|=0x80u;seal(record);
    assert(!kui_checkpoint_decode(record,&plan,c.identity,&b));
    kui_checkpoint_encode(&c,record);record[93]=1u;seal(record);
    assert(!kui_checkpoint_decode(record,&plan,c.identity,&b));
    for(enum kui_capture_format format=KUI_CAPTURE_FORMAT_BIN_CUE;format<KUI_CAPTURE_FORMAT_COUNT;format++) {
        c.format=format;kui_checkpoint_encode(&c,record);
        assert(kui_checkpoint_decode(record,&plan,c.identity,&b) && b.format==format && b.track[0].sector_mode==2u);
    }
    uint8_t modes[99]={1u,0u,2u,0u,0u,1u};char cue[16384];size_t cue_size=0;
    assert(kui_capture_cue_encode(&plan,modes,cue,sizeof(cue),&cue_size) && cue_size==strlen(cue));
    assert(strstr(cue,"REM SINGLE-DENSITY AREA\nFILE \"track01.bin\" BINARY\n  TRACK 01 MODE1/2352\n"));
    assert(strstr(cue,"REM HIGH-DENSITY AREA\nFILE \"track03.bin\" BINARY\n  TRACK 03 MODE2/2352\n"));
    unsigned gaps=0;for(const char *at=cue;(at=strstr(at,"PREGAP 00:02:00"));at++) ++gaps;
    assert(gaps==3u && !strstr(cue,"INDEX 00"));
    cue_fixture.plan=&plan;cue_fixture.cue=cue;cue_fixture.size=cue_size;
    const struct kui_game_file_ops cue_ops={NULL,cue_stat,cue_read};struct kui_game_image cue_image;
    assert(kui_game_image_open_named("disc.cue",&cue_ops,&cue_image)==KUI_GAME_OK);
    assert(!cue_image.cd_image && cue_image.data_lba==45000u && cue_image.count==plan.count);
    for(unsigned i=0;i<plan.count;i++) {
        assert(cue_image.tracks[i].start_lba==plan.tracks[i].start-150u &&
               cue_image.tracks[i].end_lba==plan.tracks[i].end-150u && !cue_image.tracks[i].file_offset &&
               cue_image.tracks[i].sector_mode==modes[i]);
    }
    assert(!kui_capture_cue_encode(&plan,modes,cue,32u,&cue_size));
    modes[2]=0u;assert(!kui_capture_cue_encode(&plan,modes,cue,sizeof(cue),&cue_size));
    /* --- kui_bench_fad_note: is the bench measuring what was asked for? ----------- */
    {
        struct kui_toc s2[2];
        memset(s2,0,sizeof(s2));
        /* Sword of the Berserk's shape: data, audio, then a high-density data track. */
        s2[0].count=2;
        s2[0].tracks[0]=(struct kui_track){1,4,150,1076};
        s2[0].tracks[1]=(struct kui_track){2,0,1076,1602};
        s2[1].count=1;
        s2[1].tracks[0]=(struct kui_track){3,4,45150,549300};
        assert(!kui_bench_fad_note(s2,45150,false));   /* data track, capture_type=data */
        assert(!kui_bench_fad_note(s2,63000,false));   /* inside the same data track */
        assert(!kui_bench_fad_note(s2,1076,true));     /* audio track, capture_type=audio */
        assert(!kui_bench_fad_note(s2,1601,true));     /* its last sector */
        /* What happened on 2026-09-20: t5c ships capture_fad=63000 with capture_type=audio,
         * which on this disc is a data track, so the run measured the audio code path on
         * data sectors and said nothing about it. */
        const char *note=kui_bench_fad_note(s2,63000,true);
        assert(note && strstr(note,"DATA track") && strstr(note,"capture_type=audio"));
        note=kui_bench_fad_note(s2,1076,false);
        assert(note && strstr(note,"AUDIO track") && strstr(note,"EDC"));
        /* start is inclusive, end exclusive; anything outside every track is named. */
        assert(strstr(kui_bench_fad_note(s2,1602,true),"not inside any track"));
        assert(strstr(kui_bench_fad_note(s2,149,false),"not inside any track"));
        assert(strstr(kui_bench_fad_note(s2,549300,false),"not inside any track"));
        assert(strstr(kui_bench_fad_note(s2,20000,false),"not inside any track"));
        assert(!kui_bench_fad_note(NULL,45150,false));
        /* An empty or overstated TOC must not be walked past what it declares. */
        memset(s2,0,sizeof(s2));
        assert(strstr(kui_bench_fad_note(s2,45150,false),"not inside any track"));
        s2[0].count=250;
        assert(strstr(kui_bench_fad_note(s2,45150,false),"not inside any track"));
    }
    puts("PASS SHA-256 vectors/chunks, CD EDC, GDI gap/address plan, checkpoint identity/bounds/order, CRC-only flag, bench fad/type note");
    return 0;
}
