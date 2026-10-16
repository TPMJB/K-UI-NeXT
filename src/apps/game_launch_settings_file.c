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
static bool publish_table(struct workspace *w,unsigned target,uint64_t sequence,bool exclusive,kui_log_fn log);
/* Missing and malformed legacy files differ from I/O failure. Only malformed
 * or missing primary JSON may recover through the old read-only .bak file. */
static int legacy_read(struct workspace *w,const char *path,unsigned *skipped,kui_log_fn log) {
    FIL file;FRESULT r=f_open(&file,path,FA_READ);
    if(r==FR_NO_FILE || r==FR_NO_PATH) return 0;
    if(r!=FR_OK) {if(log) log("Cannot read legacy launch presets %s: FatFs=%u",path+2,(unsigned)r);return -2;}
    FSIZE_t size=f_size(&file);char *json=NULL;UINT got=0;int result=-1;
    if(size && size<=KUI_GAME_LAUNCH_LEGACY_JSON_MAX) {
        json=malloc((size_t)size);
        if(!json) result=-2;
        else {
            r=f_read(&file,json,(UINT)size,&got);
            if(r!=FR_OK || got!=(UINT)size) result=-2;
            else if(kui_game_launch_settings_legacy_decode(w->table,skipped,json,(size_t)size)) result=1;
        }
    }
    if(f_close(&file)!=FR_OK) result=-2;
    free(json);
    if(result<0 && log) log("Legacy launch presets %s: %s; original retained",path+2,
        result==-2?"read/allocation/close failed":"invalid, ambiguous or exceeds migration capacity");
    return result;
}
static bool legacy_import(struct workspace *w,struct slot slots[2],int *current,kui_log_fn log) {
    unsigned skipped=0;
    int result=legacy_read(w,KUI_GAME_LAUNCH_LEGACY_PATH,&skipped,log);
    if(result==-2) return false;
    if(result!=1) {
        int backup=legacy_read(w,KUI_GAME_LAUNCH_LEGACY_BACKUP_PATH,&skipped,log);
        if(backup==-2 || (backup!=1 && (result<0 || backup<0))) return false;
        if(backup!=1) {kui_game_launch_table_clear(w->table);return true;}
        if(log) log("Recovered legacy launch presets from game-launch.json.bak");
    }
    if(skipped && log) log("Skipped %u legacy Release-loader comparison presets: unsupported; JSON retained",skipped);
    /* Exclusive creation cannot overwrite a binary preset appearing after the
     * absence check. Failed publication leaves original JSON available. */
    if(!publish_table(w,0,1,true,log)) {
        if(log) log("Legacy launch migration not verified; JSON retained; no presets applied");
        return false;
    }
    if(!read_slot(0,&slots[0],w,log) || !slots[0].valid) return false;
    *current=0;
    if(log) log("Imported %u legacy launch presets; original JSON retained",w->table->count);
    return true;
}
static bool load_table(struct workspace *w,struct slot slots[2],int *current,kui_log_fn log) {
    if(!read_slot(0,&slots[0],w,log) || !read_slot(1,&slots[1],w,log)) return false;
    *current=latest(slots);
    if(*current<0) {
        kui_game_launch_table_clear(w->table);
        if(!slots[0].exists && !slots[1].exists) return legacy_import(w,slots,current,log);
        return true;
    }
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
static bool publish_table(struct workspace *w,unsigned target,uint64_t sequence,bool exclusive,kui_log_fn log) {
    size_t bytes=0;
    bool ok=kui_game_launch_settings_encode(w->record,KUI_GAME_LAUNCH_SETTINGS_RECORD_MAX,&bytes,w->table,sequence) && make_parents(log);
    if(!ok) return false;
    FIL file;FRESULT r=f_open(&file,paths[target],FA_WRITE|(exclusive?FA_CREATE_NEW:FA_CREATE_ALWAYS));
    if(r!=FR_OK) {if(log) log("Cannot save game launch settings: FatFs=%u",(unsigned)r);return false;}
    UINT written=0;r=f_write(&file,w->record,(UINT)bytes,&written);ok=r==FR_OK && written==bytes;
    if(ok) {r=f_sync(&file);ok=r==FR_OK;}
    FRESULT closed=f_close(&file);
    if(!ok || closed!=FR_OK) {
        if(log) log("Game launch settings save failed: FatFs=%u/%u bytes=%u; prior slot retained",(unsigned)r,(unsigned)closed,(unsigned)written);
        return false;
    }
    ok=verify_saved(target,w->record,bytes,log);
    if(ok && log) log("Saved game launch settings sequence=%" PRIu64,sequence);
    return ok;
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
    unsigned target=current<0?0:(unsigned)(1-current);
    ok=publish_table(&w,target,sequence,false,log);
done:
    workspace_close(&w);return ok;
}
