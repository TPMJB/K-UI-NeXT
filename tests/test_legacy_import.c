/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/legacy_import.h"
#include "kui/raw_cue.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const struct kui_capture_plan gd={.count=3,.bytes=84u*KUI_RAW_BYTES,.tracks={
    {1,4,0,150,190,340},{2,0,0,340,344,344},{3,4,1,45150,45190,45190}}};
static const char *state="DreamShell GD Ripper state v1\ndisc_type 128\nuse_bin 1\ntracks 3\n1 150 40 4 2352 track01.bin\n2 340 4 0 2352 track02.raw\n3 45150 40 4 2352 track03.bin\n";
static const struct kui_capture_plan cd={.count=2,.profile=KUI_CAPTURE_PROFILE_CD_TOC,.bytes=8u*KUI_RAW_BYTES,.tracks={{1,0,0,150,154,154},{2,4,0,154,158,158}}};
static const char *cue="REM strict separate raw files\nFILE \"Audio.bin\" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\nFILE \"Data.bin\" BINARY\n  TRACK 02 MODE1/2352\n    INDEX 01 00:00:00\n";
static void changed(char *out,const char *source,const char *old,const char *replacement) {
    const char *at=strstr(source,old);assert(at);size_t prefix=(size_t)(at-source);memcpy(out,source,prefix);strcpy(out+prefix,replacement);strcat(out,at+strlen(old));
}
int main(void) {
    struct kui_legacy_job *job=malloc(sizeof(*job)),*prior=malloc(sizeof(*prior));assert(job && prior);
    assert(kui_legacy_state_parse(state,strlen(state),&gd,job));assert(job->declared[2]==40 && job->source.plan.profile==KUI_CAPTURE_PROFILE_GD);
    char altered[2048];
    const char *old[]={"use_bin 1","use_bin 1","tracks 3","45150 40","track03.bin","disc_type 128","2352 track03.bin","3 45150"};
    const char *replacement[]={"use_bin 0","use_bin 9","tracks 18446744073709551616","45150 41","../track03.bin","disc_type -128","2048 track03.bin","4 45150"};
    for(unsigned i=0;i<sizeof(old)/sizeof(*old);++i) {changed(altered,state,old[i],replacement[i]);memset(job,0xa5,sizeof(*job));*prior=*job;assert(!kui_legacy_state_parse(altered,strlen(altered),&gd,job));assert(!memcmp(job,prior,sizeof(*job)));}
    assert(!kui_legacy_state_parse(state,strlen(state)-1,&gd,job));strcpy(altered,state);strcat(altered,"garbage\n");assert(!kui_legacy_state_parse(altered,strlen(altered),&gd,job));
    strcpy(altered,state);altered[30]=0;assert(!kui_legacy_state_parse(altered,strlen(state),&gd,job));
    const char *cdstate="DreamShell GD Ripper state v1\ndisc_type 16\nuse_bin 1\ntracks 2\n1 150 4 0 2352 track01.raw\n2 154 2 4 2352 track02.bin\n";
    assert(kui_legacy_state_parse(cdstate,strlen(cdstate),&cd,job) && job->declared[1]==2 && job->source.plan.tracks[1].end==158);
    struct kui_scan_gdi parsed,before;assert(kui_raw_cue_parse(cue,strlen(cue),&cd,&parsed));assert(!strcmp(parsed.files[0],"Audio.bin") && !strcmp(parsed.files[1],"Data.bin"));
    changed(altered,cue,"MODE1/2352","MODE2/2352");assert(kui_raw_cue_parse(altered,strlen(altered),&cd,&parsed));
    const char *cueold[]={"Audio.bin","Data.bin","MODE1/2352","INDEX 01 00:00:00","TRACK 02","BINARY","INDEX 01","FILE \"Data.bin\""};
    const char *cuenew[]={"../Audio.bin","audio.BIN","MODE1/2048","INDEX 01 00:00:01","TRACK 03","WAVE","INDEX 00","FILE \"\""};
    for(unsigned i=0;i<sizeof(cueold)/sizeof(*cueold);++i) {changed(altered,cue,cueold[i],cuenew[i]);memset(&parsed,0xa5,sizeof(parsed));before=parsed;assert(!kui_raw_cue_parse(altered,strlen(altered),&cd,&parsed));assert(!memcmp(&parsed,&before,sizeof(parsed)));}
    changed(altered,cue,"Audio.bin","é.bin");assert(!kui_raw_cue_parse(altered,strlen(altered),&cd,&parsed));
    strcpy(altered,cue);strcat(altered,"FLAGS PRE\n");assert(!kui_raw_cue_parse(altered,strlen(altered),&cd,&parsed));
    assert(!kui_raw_cue_parse(cue,strlen(cue),&gd,&parsed));
    changed(altered,cue,"    INDEX 01 00:00:00","    PREGAP 00:02:00\n    INDEX 01 00:00:00");assert(!kui_raw_cue_parse(altered,strlen(altered),&cd,&parsed));
    free(job);free(prior);puts("PASS legacy raw state and strict BIN/CUE parsers");return 0;
}
