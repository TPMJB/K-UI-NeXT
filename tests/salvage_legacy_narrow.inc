/* SPDX-License-Identifier: GPL-3.0-only */
/* Seed pre-fast-pass evidence independently of the current salvage producer.
 * Both legacy versions commit two narrow holes and stop after the first
 * 32 sectors of the high-density track, before READY or any repair candidate. */
static void seed_legacy_narrow(unsigned version,char job[KUI_DEST_JOB_CAP]) {
    assert((version==1 || version==2) && plan.profile==KUI_CAPTURE_PROFILE_GD && plan.count==3);
    assert(plan.tracks[0].start==150 && plan.tracks[0].end==190 && plan.tracks[0].control==4 &&
        plan.tracks[1].start==340 && plan.tracks[1].end==344 && plan.tracks[1].control==0 &&
        plan.tracks[2].start==45150 && plan.tracks[2].end>=45190 && plan.tracks[2].control==4);
    uint8_t header[4096]={0},journal[8*512]={0},track[40*KUI_RAW_BYTES],digest[32];
    char path[KUI_DEST_PATH_CAP];
    memcpy(header,version==1?"KUISALV1":"KUISALV2",8);
    put32(header+8,version);put32(header+12,sizeof(header));put32(header+16,plan.count);
    put32(header+20,1);put32(header+24,45150);put32(header+28,plan.tracks[2].end-1);
    for(unsigned a=0;a<2;++a) {
        struct kui_sha256 hash;kui_sha256_init(&hash);
        unsigned sector=a?plan.tracks[2].end-plan.tracks[2].start-1:0;
        kui_sha256_update(&hash,original[2]+sector*KUI_RAW_BYTES,KUI_RAW_BYTES);
        kui_sha256_digest(&hash,header+32+a*32);
    }
    for(unsigned i=0;i<plan.count;++i) {
        const struct kui_capture_track *t=&plan.tracks[i];uint8_t *entry=header+96+i*24;
        put32(entry,t->number);put32(entry+4,t->control);put32(entry+8,t->session);
        put32(entry+12,t->start);put32(entry+16,t->end);put32(entry+20,t->toc_end);
    }
    put32(header+4092,kui_crc32(0,header,4092));
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,header,sizeof(header));
    kui_sha256_digest(&hash,digest);

    remount();
    const char *parents[]={"0:/KUI","0:/KUI/salvage"};
    for(unsigned i=0;i<2;++i) {FRESULT r=f_mkdir(parents[i]);assert(r==FR_OK || r==FR_EXIST);}
    snprintf(job,KUI_DEST_JOB_CAP,"/KUI/salvage/job-0001");
    snprintf(path,sizeof(path),"0:%s",job);assert(f_mkdir(path)==FR_OK);
    snprintf(path,sizeof(path),"0:%s/header.bin",job);write_file(path,header,sizeof(header));

    memcpy(track,original[0],sizeof(track));memset(track+2*KUI_RAW_BYTES,0,KUI_RAW_BYTES);
    uint32_t data_prefix_crc=kui_crc32(0,track,32*KUI_RAW_BYTES);
    uint32_t data_crc=kui_crc32(0,track,sizeof(track));
    snprintf(path,sizeof(path),"0:%s/track01.bin",job);write_file(path,track,sizeof(track));
    memcpy(track,original[1],4*KUI_RAW_BYTES);memset(track+2*KUI_RAW_BYTES,0,KUI_RAW_BYTES);
    uint32_t audio_crc=kui_crc32(0,track,4*KUI_RAW_BYTES);
    snprintf(path,sizeof(path),"0:%s/track02.raw",job);write_file(path,track,4*KUI_RAW_BYTES);
    uint32_t high_prefix_crc=kui_crc32(0,original[2],32*KUI_RAW_BYTES);
    snprintf(path,sizeof(path),"0:%s/track03.bin",job);write_file(path,original[2],32*KUI_RAW_BYTES);

    /* type, track, sector, count, cumulative track CRC. Sequence is 1..8;
     * all reserved fields and the high half of each sequence remain zero. */
    uint32_t events[8][5]={
        {1,0,2,0,0},
        {2,0,0,32,data_prefix_crc},
        {2,0,32,8,data_crc},
        {3,0,0,0,data_crc},
        {1,1,2,0,0},
        {2,1,0,4,audio_crc},
        {3,1,0,0,audio_crc},
        {2,2,0,32,high_prefix_crc},
    };
    for(unsigned i=0;i<8;++i) {
        uint8_t *event=journal+i*512;memcpy(event,"KUISJNL1",8);put32(event+8,i+1);
        for(unsigned field=0;field<5;++field)put32(event+16+field*4,events[i][field]);
        memcpy(event+40,digest,sizeof(digest));put32(event+508,kui_crc32(0,event,508));
    }
    snprintf(path,sizeof(path),"0:%s/journal.bin",job);write_file(path,journal,sizeof(journal));
    assert(f_mount(NULL,"0:",0)==FR_OK);
}

/* Collection reached the end of track 0, but its BASELINE record was not
 * published. Resume must finish that boundary before selecting track 1's
 * first uncommitted range; no later track file exists yet. */
static void seed_legacy_track_boundary(char job[KUI_DEST_JOB_CAP]) {
    seed_legacy_narrow(2,job);remount();
    char path[KUI_DEST_PATH_CAP];FIL journal;
    snprintf(path,sizeof(path),"0:%s/journal.bin",job);
    assert(f_open(&journal,path,FA_READ|FA_WRITE)==FR_OK);
    assert(f_lseek(&journal,3u*512u)==FR_OK && f_tell(&journal)==3u*512u &&
        f_truncate(&journal)==FR_OK && f_sync(&journal)==FR_OK && f_close(&journal)==FR_OK);
    snprintf(path,sizeof(path),"0:%s/track02.raw",job);assert(f_unlink(path)==FR_OK);
    snprintf(path,sizeof(path),"0:%s/track03.bin",job);assert(f_unlink(path)==FR_OK);
    assert(f_mount(NULL,"0:",0)==FR_OK);
}
