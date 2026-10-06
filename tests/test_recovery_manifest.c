/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/recovery_scan.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char source[KUI_SCAN_MANIFEST_LIMIT+1u],changed[KUI_SCAN_MANIFEST_LIMIT+1u];
static char compressed[KUI_SCAN_MANIFEST_LIMIT+1u];
static struct kui_scan_manifest parsed;
static struct kui_scan_gdi gdi;
static void gdi_cases(void) {
    const char *valid="3\r\n1 0 4 2352 \"Track One.bin\" 0\r\n2 155 0 2352 track02.raw 0\r\n3 45000 4 2352 track03.bin 0\r\n";
    assert(kui_recovery_gdi_parse(valid,strlen(valid),&gdi));
    assert(gdi.plan.count==3 && gdi.plan.tracks[2].start==45150 && !strcmp(gdi.files[0],"Track One.bin"));
    assert(!kui_recovery_gdi_parse(NULL,10,&gdi));
    assert(!kui_recovery_gdi_parse(valid,strlen(valid),NULL));
    assert(!kui_recovery_gdi_parse(valid,KUI_SCAN_MANIFEST_LIMIT+1,&gdi));
    const char *bad[]={"0\n","100\n","1\n", "1\n1 0 4 2352 ../track.bin 0\n",
        "1\n1 0 4 2352 /track.bin 0\n", "1\n1 0 4 2352 track.bin 1\n",
        "1\n1 0 4 2048 track.bin 0\n", "1\n2 0 4 2352 track.bin 0\n",
        "1\n1 0 5 2352 track.bin 0\n", "1\n1 0 4 2352 \"track.bin 0\n",
        "1\n1 0 4 2352 \"track.bin\"garbage 0\n", "1\n1 0 4 2352 \"track.bin \" 0\n",
        "1\n1 42949672960 4 2352 track.bin 0\n", "1\n1 719999 4 2352 track.bin 0\n",
        "1\n1 0 4 2352 track.bin 0 trailing\n", "1\n1 0 4 2352 track.bin 0\n\n",
        "2\n1 0 4 2352 track.bin 0\n2 45000 4 2352 TRACK.BIN 0\n",
        "2\n1 45000 4 2352 track.bin 0\n2 0 4 2352 second.bin 0\n"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) assert(!kui_recovery_gdi_parse(bad[i],strlen(bad[i]),&gdi));
    size_t bytes=strlen(valid);
    for(size_t i=0;i<bytes;++i) {
        char broken[256];memcpy(broken,valid,bytes);broken[i]=0;
        assert(!kui_recovery_gdi_parse(broken,bytes,&gdi));
    }
    puts("PASS imported GDI quotes, CRLF, bounds, path escape, aliases, offsets and format rejection");
}
static void reject_from(const char *document,const char *from,const char *to) {
    const char *at=strstr(document,from);assert(at);
    size_t prefix=(size_t)(at-document),suffix=strlen(at+strlen(from));
    assert(prefix+strlen(to)+suffix<sizeof(changed));
    memcpy(changed,document,prefix);memcpy(changed+prefix,to,strlen(to));
    memcpy(changed+prefix+strlen(to),at+strlen(from),suffix+1);
    assert(!kui_recovery_manifest_parse(changed,strlen(changed),&parsed));
}
static void reject(const char *from,const char *to) {reject_from(source,from,to);}
static void compressed_cases(void) {
    const char *const kinds[]={"cso","zso","chd"};
    const unsigned modes[]={1u,0u,2u};
    for(unsigned kind=0;kind<3u;kind++) {
        unsigned logical=kind==2u?14u*2448u:5u*2048u;
        int prefix=snprintf(compressed,sizeof(compressed),
            "{\"output_format\":\"%s\",\"output_file\":\"disc.%s\","
            "\"gdi_file\":\".capture.gdi\",\"output_bytes\":1234,"
            "\"output_crc32\":\"12345678\","
            "\"output_sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
            "\"output_logical_bytes\":%u,\"output_data_track\":%u,",
            kinds[kind],kinds[kind],logical,kind==2u?0u:3u);
        assert(prefix>0 && (size_t)prefix<sizeof(compressed));
        size_t used=(size_t)prefix;const char *at=source+1;
        for(unsigned i=0;i<3u;i++) {
            const char *crc=strstr(at,"\"crc32\": \"");assert(crc);
            size_t span=(size_t)(crc-at);assert(used+span<sizeof(compressed));
            memcpy(compressed+used,at,span);used+=span;
            int n=snprintf(compressed+used,sizeof(compressed)-used,"\"sector_mode\":%u,",modes[i]);
            assert(n>0 && (size_t)n<sizeof(compressed)-used);used+=(size_t)n;
            span=strlen("\"crc32\": \"");assert(used+span<sizeof(compressed));
            memcpy(compressed+used,crc,span);used+=span;at=crc+span;
        }
        assert(used+strlen(at)<sizeof(compressed));strcpy(compressed+used,at);
        assert(kui_recovery_manifest_parse(compressed,strlen(compressed),&parsed));
        assert(parsed.format==(enum kui_capture_format)(KUI_CAPTURE_FORMAT_CSO+kind) &&
               parsed.output_bytes==1234u && parsed.logical_bytes==logical &&
               parsed.data_track==(kind==2u?0u:3u) && parsed.track[2].sector_mode==2u);
        char field[96];snprintf(field,sizeof(field),"\"output_file\":\"disc.%s\"",kinds[kind]);
        reject_from(compressed,field,"\"output_file\":\"../disc.cso\"");
        reject_from(compressed,field,"\"output_file\":\"disc.iso\"");
        reject_from(compressed,"\"gdi_file\":\".capture.gdi\"","\"gdi_file\":\"disc.gdi\"");
        reject_from(compressed,"\"output_bytes\":1234","\"output_bytes\":0");
        reject_from(compressed,"\"output_crc32\":\"12345678\"","\"output_crc32\":\"1234567x\"");
        reject_from(compressed,
            "\"output_sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\",","");
        reject_from(compressed,"\"sector_mode\":0","\"sector_mode\":1");
        reject_from(compressed,"\"sector_mode\":2","\"sector_mode\":0");
        snprintf(field,sizeof(field),"\"output_logical_bytes\":%u",logical);
        reject_from(compressed,field,"\"output_logical_bytes\":1");
        snprintf(field,sizeof(field),"\"output_data_track\":%u",kind==2u?0u:3u);
        reject_from(compressed,field,"\"output_data_track\":2");
        snprintf(field,sizeof(field),"\"output_format\":\"%s\"",kinds[kind]);
        reject_from(compressed,field,"\"output_format\":\"unknown\"");
        reject_from(compressed,field,"\"output_format\":\"cso\",\"output_format\":\"cso\"");
        reject_from(compressed,"\"output_bytes\":1234,","\"output_bytes\":1234,\"cue_file\":\"disc.cue\",");
    }
    puts("PASS CSO/ZSO/CHD manifest paths, required hashes, modes, geometry and ambiguity rejection");
}
int main(int argc,char **argv) {
    gdi_cases();
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    size_t size=fread(source,1,sizeof(source)-1,file);assert(!ferror(file) && !fclose(file));
    assert(kui_recovery_manifest_parse(source,size,&parsed));
    if(parsed.format==KUI_CAPTURE_FORMAT_BIN_CUE) {
        assert(!strcmp(parsed.gdi,KUI_CAPTURE_INTERNAL_GDI) && strstr(parsed.cue,".cue"));
        assert(parsed.plan.count==6u && parsed.track[0].mode_recorded &&
               (parsed.track[0].sector_mode==1u || parsed.track[0].sector_mode==2u));
        reject("\"output_format\":\"bin_cue\"","\"output_format\":\"unknown\"");
        reject("\"cue_file\":\"disc.cue\"","\"cue_file\":\"../disc.cue\"");
        reject("\"gdi_file\":\".capture.gdi\"","\"gdi_file\":\".other.gdi\"");
        reject("\"sector_mode\":0","\"sector_mode\":1");
        reject(strstr(source,"\"sector_mode\":2")?"\"sector_mode\":2":"\"sector_mode\":1","\"sector_mode\":0");
        puts("PASS BIN/CUE manifest explicit primary/internal paths and sector modes");
        return 0;
    }
    assert(parsed.crc_only && parsed.plan.count==3 && parsed.plan.bytes==14u*KUI_RAW_BYTES);
    assert(parsed.plan.tracks[0].start==150 && parsed.plan.tracks[2].start==45150);
    assert(!strcmp(parsed.gdi,"disc.gdi"));
    assert(!kui_recovery_manifest_parse(NULL,size,&parsed));
    assert(!kui_recovery_manifest_parse(source,size,NULL));
    assert(!kui_recovery_manifest_parse(source,0,&parsed));
    assert(!kui_recovery_manifest_parse(source,KUI_SCAN_MANIFEST_LIMIT+1u,&parsed));
    /* Only the final newline can be omitted; every truncation inside JSON is refused. */
    for(size_t cut=0;cut+2<size;++cut) assert(!kui_recovery_manifest_parse(source,cut,&parsed));
    reject("\"schema\": 2","\"schema\": 3");
    reject("\"schema\": 2","\"schema\": 2, \"schema\": 2");
    reject("\"schema\": 2","\"schema\": 2, \"reference\": \"full match\"");
    reject("\"schema\": 2","\"schema\": 2, \"gdi_file\": \"../disc.gdi\"");
    reject("\"complete\": true","\"complete\": false");
    reject("\"complete\": true","\"complete\": truex");
    reject("\"sector_bytes\": 2352","\"sector_bytes\": 2048");
    reject("\"sector_bytes\": 2352","\"sector_bytes\": 02352");
    reject("\"sector_bytes\": 2352","\"sector_bytes\": 18446744073709551616");
    reject("\"start_fad\": 150","\"start_fad\": 149");
    reject("\"start_fad\": 45150","\"start_fad\": 45151");
    reject("\"control\": 4","\"control\": 5");
    reject("\"file\": \"track01.bin\"","\"file\": \"../track01.bin\"");
    reject("\"file\": \"track01.bin\"","\"file\": \"track02.bin\"");
    reject("\"bytes\": 11760","\"bytes\": 11761");
    reject("\"end_fad\": 155","\"end_fad\": 154");
    reject("\"toc_end_fad\": 305","\"toc_end_fad\": 306");
    reject("\"excluded_tail_sectors\": 150","\"excluded_tail_sectors\": 0");
    reject("gdi-raw2352-typegap150-v1","unknown-v1");
    reject("\"audio\":","\"unrecognized\":");
    reject("\"title\":","\"title\\u0000\":");
    reject("\"crc32\"\n","\"sha256\"\n");
    memcpy(changed,source,size);changed[size]='x';assert(!kui_recovery_manifest_parse(changed,size+1,&parsed));
    memcpy(changed,source,size);changed[0]=0;assert(!kui_recovery_manifest_parse(changed,size,&parsed));
    compressed_cases();
    puts("PASS Advanced CRC manifest bounds, duplicate keys, unsafe paths, inconsistent tracks and truncation");
    return 0;
}
