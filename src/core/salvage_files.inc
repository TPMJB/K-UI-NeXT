/* SPDX-License-Identifier: GPL-3.0-only */
/* One worker owns these handles. Completed track allocation is fixed while a
 * repair runs, so its optional FatFs CLMT remains valid. Packed candidates grow
 * and must never retain a CLMT: fast-seek mode cannot extend a file. Every
 * durability sync/readback still belongs to the publication caller. */
#define SALVAGE_TRACK_MAP_WORDS 8192u

static bool close_salvage_track(struct salvage *s) {
    bool ok=true;
    if(s->track_open) {
        s->track_open=false;
        ok=io_close(s,&s->track_file,true);
        s->track_file.cltbl=NULL;s->track_write=false;
    }
    free(s->track_map_heap);s->track_map_heap=NULL;
    return ok;
}

static bool close_salvage_files(struct salvage *s) {
    bool ok=close_salvage_track(s);
    if(s->packed_open) {
        s->packed_open=false;
        s->packed_write=false;
        if(!io_close(s,&s->packed_file,true))ok=false;
    }
    return ok;
}

static bool build_salvage_track_map(struct salvage *s) {
    DWORD *map=s->track_map_small;
    unsigned words=sizeof(s->track_map_small)/sizeof(s->track_map_small[0]);
    map[0]=words;s->track_file.cltbl=map;
    FRESULT r=f_lseek(&s->track_file,CREATE_LINKMAP);
    if(r==FR_NOT_ENOUGH_CORE) {
        DWORD required=map[0];
        s->track_file.cltbl=NULL;
        if(required<=words || required<4u || (required&1u))
            return fail(s,"Track allocation map length is invalid; repair stopped");
        if(required>SALVAGE_TRACK_MAP_WORDS) {
            s->ops->log("Salvage I/O: track %u map needs %lu words; using ordinary seeks above %u-word cap",
                s->open_track+1,(unsigned long)required,SALVAGE_TRACK_MAP_WORDS);
            return true;
        }
        s->track_map_heap=malloc((size_t)required*sizeof(DWORD));
        if(!s->track_map_heap) {
            s->ops->log("Salvage I/O: track %u allocation map unavailable; using ordinary seeks",s->open_track+1);
            return true;
        }
        words=(unsigned)required;map=s->track_map_heap;
        map[0]=words;s->track_file.cltbl=map;
        r=f_lseek(&s->track_file,CREATE_LINKMAP);
    }
    if(r!=FR_OK)
        return fail(s,"Cannot read stable track allocation map; repair stopped");
    if(map[0]<4u || map[0]>words || (map[0]&1u))
        return fail(s,"Track allocation map changed; repair stopped");
    s->ops->log("Salvage I/O: track %u fast access map ready (%lu words)",
        s->open_track+1,(unsigned long)map[0]);
    return true;
}

static bool salvage_track_map(struct salvage *s) {
    char previous[sizeof(s->status->message)];
    memcpy(previous,s->status->message,sizeof(previous));
    snprintf(s->status->message,sizeof(s->status->message),
        "Preparing saved track %u/%u for fast access",s->open_track+1,s->plan->count);
    update(s,true);
    if(stop(s))return false;
    bool ok=build_salvage_track_map(s);
    if(ok) {memcpy(s->status->message,previous,sizeof(previous));update(s,true);}
    return ok;
}

static FIL *cached_track(struct salvage *s,unsigned track,bool for_write) {
    if(s->failed || stop(s))return NULL;
    if(!s->ready || track>=s->plan->count) {
        fail(s,"Track cache requested before collection completed");return NULL;
    }
    if(s->track_open && s->open_track==track && (!for_write || s->track_write))return &s->track_file;
    if(!close_salvage_track(s) || stop(s))return NULL;
    track_path(s,track);
    if(f_open(&s->track_file,s->path,FA_READ|(for_write?FA_WRITE:0))!=FR_OK) {
        fail(s,"Cannot open target track for cached repair");return NULL;
    }
    s->track_open=true;s->track_write=for_write;s->open_track=track;
    uint64_t expected=(uint64_t)(s->plan->tracks[track].end-s->plan->tracks[track].start)*KUI_RAW_BYTES;
    if(f_size(&s->track_file)!=expected) {
        fail(s,"Saved track length changed before cached repair");return NULL;
    }
    if(!salvage_track_map(s))return NULL;
    return stop(s)?NULL:&s->track_file;
}

static FIL *cached_packed(struct salvage *s,bool create) {
    if(s->failed || stop(s))return NULL;
    if(s->packed_open && (!create || s->packed_write))return &s->packed_file;
    if(s->packed_open) {
        s->packed_open=false;s->packed_write=false;
        if(!io_close(s,&s->packed_file,true) || stop(s))return NULL;
    }
    path(s,"patches.bin");
    if(f_open(&s->packed_file,s->path,FA_READ|(create?FA_WRITE|FA_OPEN_ALWAYS:FA_OPEN_EXISTING))!=FR_OK) {
        fail(s,create?"Cannot append packed candidate":"Published packed candidates are missing or unreadable");return NULL;
    }
    s->packed_open=true;s->packed_write=create;
    return &s->packed_file;
}
