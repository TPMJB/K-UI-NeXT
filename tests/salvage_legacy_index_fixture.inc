/* SPDX-License-Identifier: GPL-3.0-only */
/* A v1 job with 573 immutable per-sector patches reproduces the user's
 * directory shape. The fixture checks directory work and durable bytes. */
#define LEGACY_INDEX_REPAIRS 573u
static struct {
    bool active,injected,seen_scan,seen_targets,duplicate_pending;
    DIR *directory;
    FILINFO duplicate;
    unsigned opens,entries,closes,stats,reads;
    uint64_t scan_done,target_done,media_calls,media_blocks;
} legacy_index;
static bool legacy_index_case(void) {return !strncmp(test.test,"burst-legacy-",13);}
static bool legacy_index_named(const TCHAR *path) {
    const char *name=strrchr(path,'/');name=name?name+1:path;
    return !strncmp(name,"patch-",6) || !strncmp(name,"PATCH-",6);
}
static void legacy_index_media_read(size_t count) {
    if(legacy_index.active) {++legacy_index.media_calls;legacy_index.media_blocks+=count;}
}
static FRESULT legacy_index_stat(const TCHAR *path,FILINFO *info) {
    if(legacy_index.active && legacy_index_named(path))++legacy_index.stats;
    return __real_f_stat(path,info);
}
static FRESULT legacy_index_opendir(DIR *dir,const TCHAR *path) {
    if(!legacy_index.active)return __real_f_opendir(dir,path);
    ++legacy_index.opens;assert(!legacy_index.directory);
    if(!legacy_index.injected && !strcmp(test.test,"burst-legacy-scan-open-error")) {
        legacy_index.injected=true;return FR_DISK_ERR;
    }
    FRESULT result=__real_f_opendir(dir,path);
    if(result==FR_OK)legacy_index.directory=dir;
    return result;
}
static FRESULT legacy_index_readdir(DIR *dir,FILINFO *info) {
    if(!legacy_index.active || dir!=legacy_index.directory)return __real_f_readdir(dir,info);
    ++legacy_index.entries;
    if(!legacy_index.injected && !strcmp(test.test,"burst-legacy-scan-read-error") && legacy_index.entries==8) {
        legacy_index.injected=true;memset(info,0,sizeof(*info));return FR_DISK_ERR;
    }
    if(legacy_index.duplicate_pending) {
        legacy_index.duplicate_pending=false;legacy_index.injected=true;*info=legacy_index.duplicate;return FR_OK;
    }
    FRESULT result=__real_f_readdir(dir,info);
    if(result==FR_OK && info->fname[0] && legacy_index_named(info->fname) &&
       !legacy_index.injected && !strcmp(test.test,"burst-legacy-scan-duplicate")) {
        legacy_index.duplicate=*info;legacy_index.duplicate_pending=true;
    }
    return result;
}
static FRESULT legacy_index_closedir(DIR *dir) {
    bool matching=legacy_index.active && dir==legacy_index.directory;
    FRESULT result=__real_f_closedir(dir);
    if(matching) {
        ++legacy_index.closes;legacy_index.directory=NULL;
        if(!legacy_index.injected && !strcmp(test.test,"burst-legacy-scan-close-error")) {
            legacy_index.injected=true;return FR_DISK_ERR;
        }
    }
    return result;
}
static bool legacy_index_sync_error(void) {
    if(legacy_index.active && active_status && active_status->sd_phase==KUI_SALVAGE_SD_DIRECTORY &&
       legacy_index.closes==1 && !legacy_index.injected && !strcmp(test.test,"burst-legacy-scan-sync-error")) {
        legacy_index.injected=true;return true;
    }
    return false;
}
static bool legacy_index_write_error(const void *data,UINT bytes) {
    if(legacy_index.active && active_status && active_status->sd_phase==KUI_SALVAGE_SD_DIRECTORY &&
       bytes==32 && !memcmp(data,"KUITGT2",7) && !legacy_index.injected &&
       !strcmp(test.test,"burst-legacy-scan-index-error")) {
        legacy_index.injected=true;return true;
    }
    return false;
}
static void legacy_index_progress(const struct kui_salvage_status *status) {
    if(!legacy_index.active)return;
    if(status->sd_phase==KUI_SALVAGE_SD_DIRECTORY) {
        assert(!status->pass && !status->read_count && status->read_state==KUI_SALVAGE_READ_NONE);
        assert(status->targets==1024u && status->recovered==LEGACY_INDEX_REPAIRS && !status->sd_total);
        if(!legacy_index.seen_scan) {assert(!status->sd_done);legacy_index.reads=test.reads;legacy_index.seen_scan=true;}
        assert(status->sd_done>=legacy_index.scan_done && test.reads==legacy_index.reads);
        legacy_index.scan_done=status->sd_done;
        if(!strcmp(test.test,"burst-legacy-scan-stop") && status->sd_done==8)test.cancelled=true;
    } else if(status->sd_phase==KUI_SALVAGE_SD_TARGETS) {
        assert(legacy_index.seen_scan && legacy_index.closes==1 && !legacy_index.directory);
        assert(!status->pass && !status->read_count && test.reads==legacy_index.reads);
        assert(status->sd_total==1024u && status->sd_done>=legacy_index.target_done);
        assert(status->sd_done<=legacy_index.target_done+1u);
        assert(status->recovered==LEGACY_INDEX_REPAIRS ||
            (!strcmp(test.test,"burst-legacy-pending") && status->recovered==LEGACY_INDEX_REPAIRS+1u));
        legacy_index.seen_targets=true;legacy_index.target_done=status->sd_done;
    } else if(status->sd_phase==KUI_SALVAGE_SD_NONE && legacy_index.seen_targets && legacy_index.target_done==1024u) {
        test.cancelled=true; /* Isolate preflight before new optical recovery. */
    }
}
static void legacy_index_path(char path[512],const char *job,const char *name) {
    snprintf(path,512,"0:%s/%s",job,name);
}
static void legacy_index_patch(const char *job,unsigned index,const char *name,uint8_t patch[8192]) {
    remount();char path[512];uint8_t header[4096];
    legacy_index_path(path,job,"header.bin");assert(load(path,header,sizeof(header))==sizeof(header));
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,header,sizeof(header));
    memset(patch,0,8192);memcpy(patch,"KUISPAT1",8);put32(patch+8,index);put32(patch+12,2);put32(patch+16,index);
    put32(patch+20,1);kui_sha256_digest(&hash,patch+32);
    memcpy(patch+64+KUI_RAW_BYTES,original[2]+index*KUI_RAW_BYTES,KUI_RAW_BYTES);
    put32(patch+24,kui_crc32(0,patch+64,KUI_RAW_BYTES));put32(patch+28,kui_crc32(0,patch+64+KUI_RAW_BYTES,KUI_RAW_BYTES));
    put32(patch+8192-4,kui_crc32(0,patch,8192-4));
    legacy_index_path(path,job,name);write_file(path,patch,8192);
    assert(f_mount(NULL,"0:",0)==FR_OK);
}
static void run_legacy_index_fixture(const char *job,struct kui_capture_ops *ops,struct kui_salvage_options *options) {
    assert(burst.targets==1024u);legacy_header(job,true);
    struct kui_salvage_status status;test.stop_after=LEGACY_INDEX_REPAIRS;
    assert(run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status)==KUI_SALVAGE_STOPPED);
    assert(status.recovered==LEGACY_INDEX_REPAIRS && burst_inspect(job,false)==LEGACY_INDEX_REPAIRS);
    test.stop_after=0;test.cancelled=false;
    char path[512];uint8_t saved[8192],damaged[8192],pending[8192];
    saved_check_edit(job,"patch-0572.bin",0,saved,sizeof(saved),false);
    bool pending_case=!strcmp(test.test,"burst-legacy-pending");
    if(pending_case)legacy_index_patch(job,LEGACY_INDEX_REPAIRS,"PATCH-0573.BIN",pending);
    bool missing=!strcmp(test.test,"burst-legacy-missing"),corrupt=!strcmp(test.test,"burst-legacy-corrupt");
    bool directory=!strcmp(test.test,"burst-legacy-candidate-dir");
    remount();
    if(missing) {legacy_index_path(path,job,"patch-0572.bin");assert(f_unlink(path)==FR_OK);}
    if(corrupt) {
        memcpy(damaged,saved,sizeof(damaged));damaged[64+17]^=1;
        put32(damaged+24,kui_crc32(0,damaged+64,KUI_RAW_BYTES));
        put32(damaged+8192-4,kui_crc32(0,damaged,8192-4));
        legacy_index_path(path,job,"patch-0572.bin");write_file(path,damaged,sizeof(damaged));
    }
    if(directory) {legacy_index_path(path,job,"patch-0573.bin");assert(f_mkdir(path)==FR_OK);}
    if(!strcmp(test.test,"burst-legacy-noise")) {
        const char *names[]={"patch-0573.part","patch-00000.bin","patch-50000.bin","patch--001.bin"};
        for(unsigned n=0;n<sizeof(names)/sizeof(names[0]);++n) {legacy_index_path(path,job,names[n]);write_file(path,"unpublished",11);}
    }
    assert(f_mount(NULL,"0:",0)==FR_OK);
    legacy_index.active=true;unsigned batches=burst.batches,singles=burst.singles;
    enum kui_salvage_result result=run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status);
    bool stopped=!strcmp(test.test,"burst-legacy-scan-stop");
    bool faulted=strstr(test.test,"-error") || !strcmp(test.test,"burst-legacy-scan-duplicate");
    bool failed=missing || corrupt || directory || faulted;
    assert(result==(failed?KUI_SALVAGE_FAILED:KUI_SALVAGE_STOPPED) && !status.complete);
    assert(status.recovered==LEGACY_INDEX_REPAIRS+(pending_case?1u:0u));
    assert(burst.batches==batches && burst.singles==singles && !legacy_index.directory);
    assert(legacy_index.opens==1 && !legacy_index.stats);
    if(!strcmp(test.test,"burst-legacy-scan-open-error"))assert(!legacy_index.closes && legacy_index.injected);
    else assert(legacy_index.closes==1);
    if(faulted)assert(legacy_index.injected);
    if(stopped)assert(legacy_index.seen_scan && legacy_index.scan_done==8 && !legacy_index.seen_targets);
    if(!failed && !stopped)assert(legacy_index.seen_targets && legacy_index.target_done==1024u);
    printf("PASS legacy candidate index: 1024 targets, 573 saved repairs, %u directory scans, %u patch stat calls, %llu SD requests / %llu blocks\n",
        legacy_index.opens,legacy_index.stats,(unsigned long long)legacy_index.media_calls,(unsigned long long)legacy_index.media_blocks);
    legacy_index.active=false;test.cancelled=false;
    remount();
    if(missing || corrupt) {legacy_index_path(path,job,"patch-0572.bin");write_file(path,saved,sizeof(saved));}
    if(directory) {legacy_index_path(path,job,"patch-0573.bin");assert(f_unlink(path)==FR_OK);}
    if(pending_case) {
        legacy_index_path(path,job,"PATCH-0573.BIN");assert(f_unlink(path)==FR_OK);
        legacy_index_path(path,job,"patch-0573.bin");write_file(path,pending,sizeof(pending));
    }
    assert(f_mount(NULL,"0:",0)==FR_OK);
    assert(burst_inspect(job,false)==LEGACY_INDEX_REPAIRS+(pending_case?1u:0u));
    result=run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status);
    assert(result==KUI_SALVAGE_RESOLVED && status.complete && status.recovered==burst.targets && !status.remaining);
    test.capturing=false;compare_complete(job);
}
