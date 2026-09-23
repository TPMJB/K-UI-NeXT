/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/recovery_scan.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char source[KUI_SCAN_MANIFEST_LIMIT+1u],changed[KUI_SCAN_MANIFEST_LIMIT+1u];
static struct kui_scan_manifest parsed;
static void reject(const char *from,const char *to) {
    const char *at=strstr(source,from);assert(at);
    size_t prefix=(size_t)(at-source),suffix=strlen(at+strlen(from));
    assert(prefix+strlen(to)+suffix<sizeof(changed));
    memcpy(changed,source,prefix);memcpy(changed+prefix,to,strlen(to));
    memcpy(changed+prefix+strlen(to),at+strlen(from),suffix+1);
    assert(!kui_recovery_manifest_parse(changed,strlen(changed),&parsed));
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    size_t size=fread(source,1,sizeof(source)-1,file);assert(!ferror(file) && !fclose(file));
    assert(kui_recovery_manifest_parse(source,size,&parsed));
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
    puts("PASS Advanced CRC manifest bounds, duplicate keys, unsafe paths, inconsistent tracks and truncation");
    return 0;
}
