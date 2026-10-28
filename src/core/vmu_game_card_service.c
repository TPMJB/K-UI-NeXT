/* SPDX-License-Identifier: GPL-3.0-only */
/* Selects or creates a game's own /KUI/vmu-test/pNNNN pair before launch.
 * FatFs creates files; the established raw ownership scan builds the P2
 * profile; the stage still admits everything again at boot. */
#include "kui/vmu_game_card_service.h"
#include "kui/vmu_persist_profile.h"
#include "diskio.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *const card_leaves[3]={"card.vmd","card.journal","identity.bin"};
static const uint32_t card_file_bytes[3]={131072u,1024u,16u};

static int card_refuse(struct kui_vmu_game_card_choice *choice,const char *format,...) {
    va_list args;va_start(args,format);
    vsnprintf(choice->problem,sizeof(choice->problem),format,args);va_end(args);
    return KUI_VMU_GAME_CARD_FAILED;
}
static bool card_stopped(kui_cancel_fn cancel) {return cancel && cancel();}
static bool card_path(char out[96],unsigned pair,const char *leaf) {
    int n=leaf?snprintf(out,96,KUI_VMU_GAME_CARD_ROOT "/p%04u/%s",pair,leaf):
        snprintf(out,96,KUI_VMU_GAME_CARD_ROOT "/p%04u",pair);
    return n>0 && n<96;
}
static bool card_read_exact(const char *path,void *out,size_t bytes) {
    FIL file;UINT got=0;
    if(f_open(&file,path,FA_READ)!=FR_OK) return false;
    bool ok=f_size(&file)==bytes && f_read(&file,out,(UINT)bytes,&got)==FR_OK && got==bytes;
    return f_close(&file)==FR_OK && ok;
}
static bool card_write_new(const char *path,const void *data,size_t bytes,uint8_t *check) {
    FIL file;UINT done=0;
    if(f_open(&file,path,FA_CREATE_NEW|FA_WRITE)!=FR_OK) return false;
    bool ok=f_write(&file,data,(UINT)bytes,&done)==FR_OK && done==bytes;
    ok=f_sync(&file)==FR_OK && ok;
    ok=f_close(&file)==FR_OK && ok;
    return ok && card_read_exact(path,check,bytes) && !memcmp(check,data,bytes);
}
/* Staged under part, then renamed over final; readers never see a partial
 * final file. An earlier final file is replaced only after part verifies. */
static bool card_publish(const char *final,const char *part,const void *data,size_t bytes,uint8_t *check) {
    FILINFO info;FRESULT stale=f_stat(part,&info);
    if(stale==FR_OK && ((info.fattrib&AM_DIR) || f_unlink(part)!=FR_OK)) return false;
    if(stale!=FR_OK && stale!=FR_NO_FILE) return false;
    if(!card_write_new(part,data,bytes,check)) {(void)f_unlink(part);return false;}
    FRESULT present=f_stat(final,&info);
    bool ok=present==FR_NO_FILE || (present==FR_OK && !(info.fattrib&AM_DIR) && f_unlink(final)==FR_OK);
    if(!ok || f_rename(part,final)!=FR_OK) {(void)f_unlink(part);return false;}
    return card_read_exact(final,check,bytes) && !memcmp(check,data,bytes);
}
static int card_image_read(void *context,uint16_t block,void *out) {
    FIL *file=context;UINT got=0;
    return block<256u && f_lseek(file,(FSIZE_t)block*512u)==FR_OK &&
        f_read(file,out,512u,&got)==FR_OK && got==512u?0:-1;
}
static bool card_write_blank(const char *path,struct kui_vmu_game_card_work *w) {
    FIL file;UINT done=0;bool ok=true;
    if(f_open(&file,path,FA_CREATE_NEW|FA_WRITE)!=FR_OK) return false;
    for(unsigned block=0;ok && block<256u;block++)
        ok=!kui_vmu_image_blank_block((uint16_t)block,w->sector) &&
            f_write(&file,w->sector,512u,&done)==FR_OK && done==512u;
    ok=f_sync(&file)==FR_OK && ok;
    ok=f_close(&file)==FR_OK && ok;
    if(!ok || f_open(&file,path,FA_READ)!=FR_OK) return false;
    struct kui_vmu_image_summary summary;
    const struct kui_vmu_image_io io={&file,card_image_read,NULL,NULL};
    ok=f_size(&file)==131072u && !kui_vmu_image_validate(&io,131072u,&w->image,&summary) &&
        !summary.files && summary.free_blocks==200u;
    return f_close(&file)==FR_OK && ok;
}
/* Cluster runs of one file through FatFs fast seek. FatFs reports a free
 * cluster inside a FAT chain as an internal error; exFAT allocation lives in
 * the bitmap, which card_allocated checks separately. Returns runs, or -1. */
static int card_runs(struct kui_vmu_game_card_work *w,const char *path,uint32_t bytes,FATFS **fs) {
    FIL file;
    if(f_open(&file,path,FA_READ)!=FR_OK) return -1;
    bool ok=f_size(&file)==bytes && file.obj.sclust;
    if(ok) {
        memset(w->link_map,0,sizeof(w->link_map));
        w->link_map[0]=(uint32_t)(sizeof(w->link_map)/sizeof(w->link_map[0]));
        file.cltbl=(DWORD *)w->link_map;
        ok=f_lseek(&file,CREATE_LINKMAP)==FR_OK && w->link_map[0]>=4u && !(w->link_map[0]&1u);
    }
    if(fs) *fs=file.obj.fs;
    file.cltbl=NULL;
    if(f_close(&file)!=FR_OK || !ok) return -1;
    return (int)((w->link_map[0]-2u)/2u);
}
static bool card_allocated(struct kui_vmu_game_card_work *w,FATFS *fs,unsigned runs) {
    if(fs->fs_type!=FS_EXFAT) return true;
    LBA_t loaded=(LBA_t)-1;
    for(unsigned i=0;i<runs;i++) {
        uint32_t count=w->link_map[1u+2u*i],first=w->link_map[2u+2u*i];
        if(!count || first<2u || first>=fs->n_fatent || count>fs->n_fatent-first) return false;
        for(uint32_t c=first;c<first+count;c++) {
            uint32_t bit=c-2u;LBA_t sector=fs->bitbase+bit/4096u;
            if(sector!=loaded) {
                if(disk_read(fs->pdrv,w->sector,sector,1u)!=RES_OK) return false;
                loaded=sector;
            }
            if(!(w->sector[(bit%4096u)/8u]&(1u<<(bit%8u)))) return false;
        }
    }
    return true;
}
/* Same files, same clusters, same volume geometry as a saved profile. */
static bool card_profile_current(struct kui_vmu_game_card_work *w,unsigned pair,
    uint64_t first,uint64_t end,const uint8_t identity[16]) {
    char expected[96]={0};
    snprintf(expected,sizeof(expected),"/KUI/vmu-test/p%04u",pair);
    char name[48];snprintf(name,sizeof(name),"retail-persistent-profile-%04u.bin",pair);
    if(!card_path(w->path,pair,name) || !card_read_exact(w->path,w->check,sizeof(w->check)) ||
       kui_vmu_persist_profile_decode(&w->cached,w->check,sizeof(w->check)) ||
       memcmp(w->cached.pair_path,expected,sizeof(expected)) ||
       memcmp(w->cached.image_id,identity,16u) || kui_vmu_game_card_template(w->cached.cid) ||
       w->cached.partition_first!=first || w->cached.partition_end!=end) return false;
    const struct kui_vmu_persist_geometry *g=&w->cached.geometry;
    for(unsigned role=0;role<3u;role++) {
        FATFS *fs=NULL;
        if(!card_path(w->path,pair,card_leaves[role])) return false;
        int runs=card_runs(w,w->path,card_file_bytes[role],&fs);
        const struct kui_vmu_persist_file *f=&w->cached.files[role];
        if(runs<1 || !fs || g->fs_type!=(fs->fs_type==FS_EXFAT?KUI_VMU_PERSIST_EXFAT:KUI_VMU_PERSIST_FAT32) ||
           g->sectors_per_cluster!=fs->csize || g->cluster_count!=fs->n_fatent-2u ||
           g->data_first_lba!=first+fs->database || (uint32_t)runs!=f->allocated_count) return false;
        uint32_t logical=0;
        for(int i=0;i<runs;i++) {
            uint32_t count=w->link_map[1u+2u*(unsigned)i],cluster=w->link_map[2u+2u*(unsigned)i];
            const struct kui_vmu_persist_extent *e=&f->allocated[i];
            if(e->logical_block!=logical || e->blocks!=count*g->sectors_per_cluster ||
               e->lba!=g->data_first_lba+(uint64_t)(cluster-2u)*g->sectors_per_cluster) return false;
            logical+=e->blocks;
        }
    }
    return true;
}
static int card_raw_role(void *context,unsigned role,uint32_t logical,uint8_t out[512]) {
    struct kui_vmu_game_card_work *w=context;uint32_t lba;
    if(kui_vmu_persist_profile_map_lba(&w->profile,role,logical,&lba)) return -1;
    return w->raw.read_sector(w->raw.context,lba,out);
}
/* The import tool's preparation: unmount, scan ownership, hash a snapshot
 * with a clear journal marker, remount. Nothing is written in between. */
static int card_collect(struct kui_vmu_game_card_work *w,unsigned pair,uint64_t first,uint64_t end,
    const uint8_t identity[16],const struct kui_vmu_fs_range *forbidden,size_t forbidden_count,
    const struct kui_vmu_game_card_platform *platform,struct kui_vmu_game_card_choice *choice,kui_log_fn log) {
    char pair_path[20];
    snprintf(pair_path,sizeof(pair_path),"/KUI/vmu-test/p%04u",pair);
    log("Virtual card: checking p%04u ownership; keep power on",pair);
    memset(&w->raw,0,sizeof(w->raw));
    if(!platform->raw_begin(platform->context,&w->raw) || !w->raw.read_sector) {
        bool remounted=platform->raw_end(platform->context);
        return card_refuse(choice,remounted?"card identity or raw reads unavailable":
            "card identity unavailable and the volume did not remount");
    }
    struct kui_vmu_fs_input input={.read_sector=w->raw.read_sector,.context=w->raw.context,
        .card_sectors=w->raw.card_sectors,.partition_first=first,.partition_end=end,
        .high_capacity=w->raw.high_capacity?1u:0u,.pair_path=pair_path,
        .forbidden=forbidden,.forbidden_count=forbidden_count};
    memcpy(input.cid,w->raw.cid,16u);memcpy(input.image_id,identity,16u);
    int scan=kui_vmu_fs_collect(&input,&w->profile,&w->filesystem);
    int snapshot=scan?0:kui_vmu_persist_profile_collect_snapshot(&w->profile,card_raw_role,w);
    int encoded=scan || snapshot?0:kui_vmu_persist_profile_encode(&w->profile,w->wire);
    bool remounted=platform->raw_end(platform->context);
    if(!remounted) return card_refuse(choice,"the volume did not remount after the ownership scan");
    if(scan) return card_refuse(choice,"p%04u ownership scan refused: %d detail %lu",pair,scan,
        (unsigned long)w->filesystem.stats.detail);
    if(snapshot==KUI_VMU_PERSIST_PROFILE_MARKER)
        return card_refuse(choice,"p%04u moved while its journal holds a save; restore its files",pair);
    if(snapshot || encoded) return card_refuse(choice,"p%04u profile snapshot refused: %d",pair,snapshot?snapshot:encoded);
    choice->collected=true;
    return KUI_VMU_GAME_CARD_READY;
}
static int card_save_profile(struct kui_vmu_game_card_work *w,unsigned pair,uint64_t first,uint64_t end,
    const uint8_t identity[16],struct kui_vmu_game_card_choice *choice) {
    char name[48],part[96];
    snprintf(name,sizeof(name),"retail-persistent-profile-%04u.bin",pair);
    if(!card_path(w->path,pair,name) || !card_path(part,pair,"profile.part") ||
       !card_publish(w->path,part,w->wire,sizeof(w->wire),w->check))
        return card_refuse(choice,"cannot save the p%04u profile",pair);
    if(!card_profile_current(w,pair,first,end,identity) || memcmp(w->check,w->wire,sizeof(w->wire)))
        return card_refuse(choice,"p%04u changed while its profile was saved",pair);
    return KUI_VMU_GAME_CARD_READY;
}
/* Every existing pair's files must still read as allocated before FatFs is
 * allowed to allocate new clusters, so a damaged allocation table cannot
 * hand an existing card's data to the new one. */
static int card_existing_allocated(struct kui_vmu_game_card_work *w,struct kui_vmu_game_card_choice *choice) {
    for(unsigned i=0;i<w->pair_count;i++) for(unsigned role=0;role<3u;role++) {
        FILINFO info;FATFS *fs=NULL;
        if(!card_path(w->path,w->pairs[i],card_leaves[role])) return card_refuse(choice,"card path");
        FRESULT present=f_stat(w->path,&info);
        if(present==FR_NO_FILE) continue;
        if(present!=FR_OK) return card_refuse(choice,"cannot inspect p%04u",w->pairs[i]);
        if(info.fattrib&AM_DIR || info.fsize!=card_file_bytes[role]) continue;
        int runs=card_runs(w,w->path,card_file_bytes[role],&fs);
        if(runs<1 || !fs || !card_allocated(w,fs,(unsigned)runs))
            return card_refuse(choice,"p%04u %s reads as unallocated; no new card was made",
                w->pairs[i],card_leaves[role]);
    }
    return KUI_VMU_GAME_CARD_READY;
}
static int card_create(struct kui_vmu_game_card_work *w,const struct kui_vmu_game_card_platform *platform,
    struct kui_vmu_game_card_choice *choice,uint8_t identity[16],kui_log_fn log,kui_cancel_fn cancel) {
    unsigned pair=0;
    for(unsigned n=2u;n<=KUI_VMU_GAME_CARD_PAIRS_MAX && !pair;n++)
        if(!(w->used[n/8u]&(1u<<(n%8u)))) pair=n;
    if(!pair) return card_refuse(choice,"all pair folders p0002-p9999 are in use");
    static const char *const parents[]={"0:/KUI",KUI_VMU_GAME_CARD_ROOT};
    for(unsigned i=0;i<2u;i++) {
        FRESULT made=f_mkdir(parents[i]);
        if(made!=FR_OK && made!=FR_EXIST) return card_refuse(choice,"cannot create %s",parents[i]+2);
    }
    for(;pair<=KUI_VMU_GAME_CARD_PAIRS_MAX;pair++) {
        if(w->used[pair/8u]&(1u<<(pair%8u))) continue;
        if(!card_path(w->path,pair,NULL)) return card_refuse(choice,"card path");
        FRESULT made=f_mkdir(w->path);
        if(made==FR_OK) break;
        if(made!=FR_EXIST) return card_refuse(choice,"cannot create p%04u",pair);
    }
    if(pair>KUI_VMU_GAME_CARD_PAIRS_MAX) return card_refuse(choice,"no free pair folder remains");
    choice->pair=pair;
    log("Virtual card: creating p%04u for %s; keep power on",pair,choice->key);
    if(card_stopped(cancel)) return KUI_VMU_GAME_CARD_CANCELLED;
    if(!card_path(w->path,pair,"card.vmd") || !card_write_blank(w->path,w))
        return card_refuse(choice,"cannot write a blank p%04u card",pair);
    memset(w->wire,0,1024u);
    if(card_stopped(cancel)) return KUI_VMU_GAME_CARD_CANCELLED;
    if(!card_path(w->path,pair,"card.journal") || !card_write_new(w->path,w->wire,1024u,w->check))
        return card_refuse(choice,"cannot write the p%04u journal",pair);
    uint8_t salt[16];platform->salt(platform->context,salt);
    kui_vmu_game_card_identity(choice->key,pair,salt,identity);
    char part[96];uint8_t binding[KUI_VMU_GAME_CARD_BINDING_BYTES];
    if(card_stopped(cancel)) return KUI_VMU_GAME_CARD_CANCELLED;
    if(!card_path(w->path,pair,"identity.bin") || !card_path(part,pair,"identity.part") ||
       !card_publish(w->path,part,identity,16u,w->check))
        return card_refuse(choice,"cannot write the p%04u identity",pair);
    if(!kui_vmu_game_card_binding_encode(choice->key,binding) ||
       !card_path(w->path,pair,KUI_VMU_GAME_CARD_BINDING_NAME) || !card_path(part,pair,"game.part") ||
       !card_publish(w->path,part,binding,sizeof(binding),w->check))
        return card_refuse(choice,"cannot bind p%04u to %s",pair,choice->key);
    choice->created=true;
    return KUI_VMU_GAME_CARD_READY;
}
int kui_vmu_game_card_select(const char *product,uint64_t partition_first,uint64_t partition_end,
    const struct kui_vmu_fs_range *forbidden,size_t forbidden_count,
    const struct kui_vmu_game_card_platform *platform,struct kui_vmu_game_card_work *w,
    uint8_t wire[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES],struct kui_vmu_game_card_choice *choice,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(wire) memset(wire,0,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES);
    if(!choice) return KUI_VMU_GAME_CARD_FAILED;
    memset(choice,0,sizeof(*choice));
    if(!wire || !w || !platform || !platform->raw_begin || !platform->raw_end || !platform->salt || !log ||
       (forbidden_count && !forbidden) || partition_first>=partition_end)
        return card_refuse(choice,"game card arguments");
    if(!kui_vmu_game_card_key(product,choice->key)) return KUI_VMU_GAME_CARD_UNBOUND;
    if(card_stopped(cancel)) return KUI_VMU_GAME_CARD_CANCELLED;
    memset(w->used,0,sizeof(w->used));w->pair_count=0;
    DIR dir;FILINFO info;
    FRESULT opened=f_opendir(&dir,KUI_VMU_GAME_CARD_ROOT);
    if(opened==FR_OK) {
        FRESULT r;
        while((r=f_readdir(&dir,&info))==FR_OK && info.fname[0]) {
            unsigned n=kui_vmu_game_card_pair_number(info.fname);
            if(!n) continue;
            w->used[n/8u]|=(uint8_t)(1u<<(n%8u));
            if(info.fattrib&AM_DIR) w->pairs[w->pair_count++]=(uint16_t)n;
        }
        if(f_closedir(&dir)!=FR_OK || r!=FR_OK) return card_refuse(choice,"cannot list %s",KUI_VMU_GAME_CARD_ROOT+2);
    } else if(opened!=FR_NO_PATH && opened!=FR_NO_FILE) return card_refuse(choice,"cannot open %s",KUI_VMU_GAME_CARD_ROOT+2);
    unsigned pair=0;
    for(unsigned i=0;i<w->pair_count;i++) {
        uint8_t binding[KUI_VMU_GAME_CARD_BINDING_BYTES];char key[KUI_VMU_GAME_CARD_KEY_BYTES];
        if(!card_path(w->path,w->pairs[i],KUI_VMU_GAME_CARD_BINDING_NAME)) return card_refuse(choice,"card path");
        if(!card_read_exact(w->path,binding,sizeof(binding)) ||
           !kui_vmu_game_card_binding_decode(binding,sizeof(binding),key) ||
           memcmp(key,choice->key,sizeof(key))) continue;
        if(pair) return card_refuse(choice,"p%04u and p%04u both claim %s",pair,w->pairs[i],choice->key);
        pair=w->pairs[i];
    }
    if(card_stopped(cancel)) return KUI_VMU_GAME_CARD_CANCELLED;
    uint8_t identity[16];int result;
    if(pair) {
        choice->pair=pair;
        if(!card_path(w->path,pair,"identity.bin") || !card_read_exact(w->path,identity,16u))
            return card_refuse(choice,"p%04u identity.bin is missing or not 16 bytes",pair);
        if(card_profile_current(w,pair,partition_first,partition_end,identity)) {
            memcpy(wire,w->check,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES);
            log("Virtual card: p%04u for %s",pair,choice->key);
            return KUI_VMU_GAME_CARD_READY;
        }
    } else {
        result=card_existing_allocated(w,choice);
        if(result) return result;
        if(card_stopped(cancel)) return KUI_VMU_GAME_CARD_CANCELLED;
        result=card_create(w,platform,choice,identity,log,cancel);
        if(result) return result;
        pair=choice->pair;
    }
    result=card_collect(w,pair,partition_first,partition_end,identity,forbidden,forbidden_count,platform,choice,log);
    if(result) return result;
    result=card_save_profile(w,pair,partition_first,partition_end,identity,choice);
    if(result) return result;
    memcpy(wire,w->wire,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES);
    log("Virtual card: p%04u for %s (%s)",pair,choice->key,choice->created?"new blank card":"re-mapped");
    return KUI_VMU_GAME_CARD_READY;
}
