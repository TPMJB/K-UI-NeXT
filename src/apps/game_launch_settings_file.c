/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_launch_settings.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static const char *const paths[]={KUI_GAME_LAUNCH_SETTINGS_PATH_A,KUI_GAME_LAUNCH_SETTINGS_PATH_B};
struct slot {bool exists,valid;uint64_t sequence;size_t size;};
struct workspace {struct kui_game_launch_table *table;uint8_t *record;};
static bool workspace_open(struct workspace *w,kui_log_fn log) {
    w->table=malloc(sizeof(*w->table));w->record=malloc(KUI_GAME_LAUNCH_SETTINGS_RECORD_MAX);
    if(w->table && w->record) return true;
    free(w->table);free(w->record);w->table=NULL;w->record=NULL;
    if(log) log("Game launch settings unavailable: not enough memory");
    return false;
}
static void workspace_close(struct workspace *w) {free(w->table);free(w->record);}
static bool read_slot(unsigned index,struct slot *slot,struct workspace *w,kui_log_fn log) {
    memset(slot,0,sizeof(*slot));FIL file;FRESULT r=f_open(&file,paths[index],FA_READ);
    if(r==FR_NO_FILE || r==FR_NO_PATH) return true;
    if(r!=FR_OK) {if(log) log("Cannot read game launch settings %s: FatFs=%u",paths[index]+2,(unsigned)r);return false;}
    slot->exists=true;UINT got=0;
    bool sized=f_size(&file)>=36u && f_size(&file)<=KUI_GAME_LAUNCH_SETTINGS_RECORD_MAX;
    if(sized) {slot->size=(size_t)f_size(&file);r=f_read(&file,w->record,(UINT)slot->size,&got);}
    FRESULT closed=f_close(&file);
    if(r!=FR_OK || closed!=FR_OK || (sized && got!=slot->size)) {
        if(log) log("Cannot read/close game launch settings: FatFs=%u/%u bytes=%u",(unsigned)r,(unsigned)closed,(unsigned)got);
        return false;
    }
    if(sized) slot->valid=kui_game_launch_settings_decode(w->table,&slot->sequence,w->record,slot->size);
    if(!slot->valid && log) log("Ignoring invalid game launch settings: %s",paths[index]+2);
    return true;
}
static int latest(const struct slot s[2]) {
    if(!s[0].valid) return s[1].valid?1:-1;
    if(!s[1].valid) return 0;
    return s[1].sequence>s[0].sequence?1:0;
}
static bool load_table(struct workspace *w,struct slot slots[2],int *current,kui_log_fn log) {
    if(!read_slot(0,&slots[0],w,log) || !read_slot(1,&slots[1],w,log)) return false;
    *current=latest(slots);
    if(*current<0) {kui_game_launch_table_clear(w->table);return true;}
    /* The first pass only chooses an authoritative slot; reread it after both
     * records were checked, so memory contains exactly that collection. */
    struct slot again;
    if(!read_slot((unsigned)*current,&again,w,log) || !again.valid ||
       again.sequence!=slots[*current].sequence || again.size!=slots[*current].size) {
        if(log) log("Game launch settings changed during read; operation refused");
        return false;
    }
    return true;
}
bool kui_game_launch_settings_load(const char *image_path,struct kui_game_launch_settings *out,kui_log_fn log) {
    if(!out) return false;
    kui_game_launch_settings_default(out);char key[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];
    if(!kui_game_launch_settings_normalize(key,image_path)) return false;
    struct workspace w;if(!workspace_open(&w,log)) return false;
    struct slot slots[2];int current;bool ok=load_table(&w,slots,&current,log);
    if(ok) (void)kui_game_launch_table_find(w.table,key,out);
    else if(log) log("Game launch settings unavailable; defaults remain in memory");
    workspace_close(&w);return ok;
}
static bool make_parents(kui_log_fn log) {
    static const char *const parents[]={"0:/KUI","0:/KUI/apps","0:/KUI/apps/games"};
    for(unsigned i=0;i<sizeof(parents)/sizeof(parents[0]);i++) {
        FILINFO info;FRESULT r=f_stat(parents[i],&info);
        if(r==FR_OK && (info.fattrib&AM_DIR)) continue;
        if(r==FR_NO_FILE || r==FR_NO_PATH) {r=f_mkdir(parents[i]);if(r==FR_OK) continue;}
        if(log) log("Cannot create game launch settings folder %s: FatFs=%u",parents[i]+2,(unsigned)r);
        return false;
    }
    return true;
}
static bool verify_saved(unsigned target,const uint8_t *record,size_t bytes,kui_log_fn log) {
    FIL file;FRESULT r=f_open(&file,paths[target],FA_READ);
    if(r!=FR_OK) {if(log) log("Cannot reread game launch settings: FatFs=%u",(unsigned)r);return false;}
    bool ok=f_size(&file)==bytes;uint8_t chunk[256];
    for(size_t at=0;ok && at<bytes;) {
        UINT wanted=(UINT)(bytes-at>sizeof(chunk)?sizeof(chunk):bytes-at),got=0;
        r=f_read(&file,chunk,wanted,&got);
        ok=r==FR_OK && got==wanted && !memcmp(chunk,record+at,wanted);at+=wanted;
    }
    FRESULT closed=f_close(&file);
    if(!ok || closed!=FR_OK) {
        if(log) log("Game launch settings reread failed; prior slot retained");
        return false;
    }
    return true;
}
static bool settings_equal(const struct kui_game_launch_settings *a,const struct kui_game_launch_settings *b) {
    if(a->reader!=b->reader || a->cdda!=b->cdda || a->encoding!=b->encoding || a->codec!=b->codec) return false;
    for(unsigned i=0;i<2;i++) {
        const struct kui_game_ram_patch *left=&a->patches.entries[i],*right=&b->patches.entries[i];
        if(left->address!=right->address || left->value!=right->value || left->enabled!=right->enabled) return false;
    }
    return true;
}
bool kui_game_launch_settings_save(const char *image_path,const struct kui_game_launch_settings *settings,kui_log_fn log) {
    char key[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];
    if(!kui_game_launch_settings_valid(settings) || !kui_game_launch_settings_normalize(key,image_path)) return false;
    struct workspace w;if(!workspace_open(&w,log)) return false;
    struct slot slots[2];int current;bool ok=load_table(&w,slots,&current,log);
    if(!ok) goto done;
    struct kui_game_launch_settings old;
    if(kui_game_launch_table_find(w.table,key,&old) && settings_equal(&old,settings)) goto done;
    if(current>=0 && slots[current].sequence==UINT64_MAX) {
        if(log) log("Game launch settings sequence exhausted; save refused");
        ok=false;goto done;
    }
    if(!kui_game_launch_table_upsert(w.table,key,settings)) {
        if(log) log("Game launch settings collection full or invalid; prior settings retained");
        ok=false;goto done;
    }
    uint64_t sequence=current<0?1:slots[current].sequence+1;
    unsigned target=current<0?0:(unsigned)(1-current);size_t bytes=0;
    ok=kui_game_launch_settings_encode(w.record,KUI_GAME_LAUNCH_SETTINGS_RECORD_MAX,&bytes,w.table,sequence) && make_parents(log);
    if(!ok) goto done;
    FIL file;FRESULT r=f_open(&file,paths[target],FA_WRITE|FA_CREATE_ALWAYS);
    if(r!=FR_OK) {if(log) log("Cannot save game launch settings: FatFs=%u",(unsigned)r);ok=false;goto done;}
    UINT written=0;r=f_write(&file,w.record,(UINT)bytes,&written);ok=r==FR_OK && written==bytes;
    if(ok) {r=f_sync(&file);ok=r==FR_OK;}
    FRESULT closed=f_close(&file);
    if(!ok || closed!=FR_OK) {
        if(log) log("Game launch settings save failed: FatFs=%u/%u bytes=%u; prior slot retained",(unsigned)r,(unsigned)closed,(unsigned)written);
        ok=false;goto done;
    }
    ok=verify_saved(target,w.record,bytes,log);
    if(ok && log) log("Saved game launch settings sequence=%" PRIu64,sequence);
done:
    workspace_close(&w);return ok;
}
