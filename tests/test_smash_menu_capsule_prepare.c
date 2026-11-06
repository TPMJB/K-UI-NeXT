/* SPDX-License-Identifier: GPL-3.0-only */
/* Production capsule files against a bounded two-file FatFs model. The card
 * service has its own ownership tests; these exercise every payload-write
 * and existing-file truncate boundary with full capsule allocations. */
#include "ff.h"
#include "kui/core.h"
#include "kui/runtime.h"
#include "kui/smash_menu_capsule.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static bool capsule_wide_window;
static void *capsule_scratch_allocate(size_t bytes) {return capsule_wide_window?malloc(bytes):NULL;}
#define LINK_MAP_WORDS 16u
static DWORD link_map[LINK_MAP_WORDS];
#define malloc capsule_scratch_allocate
#include "../src/apps/smash_menu_capsule_file_prepare.inc"
#undef malloc

enum capsule_role { CHILD,OUTER,
#ifdef SMASH_COLLECTION_FILES_TEST
    VC2,SWIRL,VC2_SESSION,SWIRL_SESSION,
#endif
    CAPSULES };
struct fixture_capsule {
    uint8_t wanted[8704],disk[65536];
    DWORD old_table[8],new_table[8],readback_table[8];
    bool present,rewrite,fail_read,short_read,short_write,fail_sync,fail_close,
        wrong_fs,change_map,corrupt_readback;
    uint32_t disk_bytes,wanted_bytes;
    unsigned creates,expands,writes,reads,closes,maps;
};
static struct fixture_capsule capsules[CAPSULES],*active;
static FIL *active_handle;
static const char *const paths[CAPSULES]={KUI_SMASH_MENU_CAPSULE_PATH,KUI_SMASH_MENU_OUTER_CAPSULE_PATH,
#ifdef SMASH_COLLECTION_FILES_TEST
    KUI_SMASH_MENU_VC2_CAPSULE_PATH,KUI_SMASH_MENU_SWIRL_CAPSULE_PATH,
    KUI_SMASH_CE_VC2_SESSION,KUI_SMASH_CE_SWIRL_SESSION,
#endif
};
static FATFS fs,other;
static struct kui_volume volume;
static struct kui_retail_manifest game;
static struct kui_vmu_persist_profile profile;
static uint8_t page[4096];
static bool cancelled_now;
static const char *problem;
static struct kui_smash_menu_capsule_prepared prepared[CAPSULES];

FRESULT f_open(FIL *file,const TCHAR *name,BYTE mode) {
    assert(!active);
    unsigned role=0u;while(role<CAPSULES && strcmp(name,paths[role]))++role;
    assert(role<CAPSULES);struct fixture_capsule *c=&capsules[role];
    if(mode==FA_READ && !c->present)return FR_NO_FILE;
    if(mode&FA_CREATE_ALWAYS) {
        ++c->creates;c->present=true;c->rewrite=true;c->disk_bytes=0u;
    }
    memset(file,0,sizeof(*file));file->obj.fs=c->wrong_fs?&other:&fs;
    file->obj.objsize=c->disk_bytes;file->obj.sclust=c->disk_bytes?c->old_table[2]:0u;
    active=c;active_handle=file;return FR_OK;
}
FRESULT f_lseek(FIL *file,FSIZE_t at) {
    assert(active && file==active_handle);struct fixture_capsule *c=active;
    if(at==CREATE_LINKMAP) {
        ++c->maps;assert(file->cltbl && file->cltbl[0]>=8u);
        const DWORD *table=c->rewrite?c->new_table:c->old_table;
        if(c->change_map && c->rewrite && c->writes)table=c->readback_table;
        memcpy(file->cltbl,table,table[0]*sizeof(*table));return FR_OK;
    }
    assert(at<=file->obj.objsize);file->fptr=at;return FR_OK;
}
FRESULT f_expand(FIL *file,FSIZE_t bytes,BYTE mode) {
    assert(active && file==active_handle && mode==1u && !active->disk_bytes && bytes<=sizeof(active->disk));
    struct fixture_capsule *c=active;++c->expands;
    c->disk_bytes=(uint32_t)bytes;file->obj.objsize=bytes;file->obj.sclust=c->new_table[2];
    memset(c->disk,0xee,sizeof(c->disk));return FR_OK;
}
FRESULT f_read(FIL *file,void *out,UINT want,UINT *got) {
    assert(active && file==active_handle && file->fptr<=active->disk_bytes && want<=active->disk_bytes-file->fptr);
    struct fixture_capsule *c=active;++c->reads;
    *got=0u;if(c->fail_read)return FR_DISK_ERR;
    *got=c->short_read?want-1u:want;
    memcpy(out,c->disk+file->fptr,*got);
    if(c->corrupt_readback && c->rewrite && c->writes && *got)((uint8_t *)out)[0]^=1u;
    file->fptr+=*got;return FR_OK;
}
FRESULT f_write(FIL *file,const void *in,UINT want,UINT *put) {
    assert(active && file==active_handle && active->rewrite && active->expands && active->maps &&
        file->fptr<=active->disk_bytes && want<=active->disk_bytes-file->fptr);
    struct fixture_capsule *c=active;++c->writes;*put=c->short_write?want-1u:want;
    memcpy(c->disk+file->fptr,in,*put);file->fptr+=*put;return FR_OK;
}
FRESULT f_sync(FIL *file) {assert(active && file==active_handle);return active->fail_sync?FR_DISK_ERR:FR_OK;}
FRESULT f_close(FIL *file) {
    assert(active && file==active_handle && !file->cltbl);struct fixture_capsule *c=active;
    active=NULL;active_handle=NULL;++c->closes;return c->fail_close?FR_DISK_ERR:FR_OK;
}
static bool cancel(void) {return cancelled_now;}
static void log_message(const char *format,...) {(void)format;}
static void fixture(void) {
    assert(!active);capsule_wide_window=false;
    fs=(FATFS){.fs_type=FS_FAT32,.database=100u,.csize=8u,.n_fatent=1002u};
    volume=(struct kui_volume){.start=2048u,.count=10000u};
    game=(struct kui_retail_manifest){.partition_start=2048u,.partition_end=12048u,.track_count=1u,.extent_count=1u};
    game.slots[1].extent=(struct kui_retail_extent){.card_lba=4000u,.blocks=50u};
    memset(&profile,0,sizeof(profile));profile.files[0].allocated_count=1u;
    profile.files[0].allocated[0]=(struct kui_vmu_persist_extent){.lba=5000u,.blocks=256u};
    memset(capsules,0,sizeof(capsules));
    for(unsigned role=0u;role<CAPSULES;role++) {
        struct fixture_capsule *c=&capsules[role];
        for(unsigned i=0u;i<sizeof(c->wanted);++i)c->wanted[i]=(uint8_t)((37u*i)^(i>>5)^(role*0x5bu));
        memcpy(c->disk,c->wanted,sizeof(c->wanted));c->disk_bytes=c->wanted_bytes=sizeof(c->wanted);
        c->old_table[0]=4u;c->old_table[1]=3u;c->old_table[2]=role==CHILD?20u:100u;
        memcpy(c->new_table,c->old_table,sizeof(c->new_table));
        memcpy(c->readback_table,c->old_table,sizeof(c->readback_table));c->readback_table[2]+=10u;
        c->present=true;
    }
    cancelled_now=false;problem=NULL;memset(prepared,0,sizeof(prepared));
}
static bool prepare_role(enum capsule_role role,const struct kui_smash_menu_capsule_runs *forbidden) {
    struct fixture_capsule *c=&capsules[role];
    bool ok=smash_menu_capsule_file_prepare(&fs,&volume,&game,&profile,paths[role],c->wanted,c->wanted_bytes,page,
        forbidden,&prepared[role],&problem,log_message,cancel);
    assert(!active);
    if(!ok)assert(!memcmp(&prepared[role],&(struct kui_smash_menu_capsule_prepared){0},sizeof(prepared[role])));
    return ok;
}
static bool prepare(void) {return prepare_role(CHILD,NULL);}
static int inspect(enum capsule_role role,struct kui_smash_menu_capsule_runs *allocated) {
    int result=smash_menu_capsule_file_inspect(paths[role],&fs,&volume,&game,&profile,allocated,&problem,cancel);
    assert(!active);
    if(result<=0)assert(!memcmp(allocated,&(struct kui_smash_menu_capsule_runs){0},sizeof(*allocated)));
    return result;
}
static void cross_capsule_cases(void) {
    struct kui_smash_menu_capsule_runs outer={0},child={0};
    fixture();assert(inspect(OUTER,&outer)==1 && outer.blocks==24u && outer.first_lba==2932u);
    assert(prepare_role(CHILD,&outer));child=prepared[CHILD].allocated_runs;
    assert(child.blocks==24u && prepared[CHILD].runs.blocks==17u);
    assert(prepare_role(OUTER,&child));
    assert(kui_smash_menu_capsule_runs_disjoint(&child,&prepared[OUTER].allocated_runs));
    assert(!capsules[CHILD].creates && !capsules[OUTER].creates && !capsules[CHILD].writes && !capsules[OUTER].writes);
    fixture();capsules[CHILD].present=capsules[OUTER].present=false;
    assert(prepare_role(CHILD,NULL));child=prepared[CHILD].allocated_runs;
    assert(prepare_role(OUTER,&child));
    assert(!memcmp(capsules[CHILD].disk,capsules[CHILD].wanted,capsules[CHILD].wanted_bytes));
    assert(!memcmp(capsules[OUTER].disk,capsules[OUTER].wanted,capsules[OUTER].wanted_bytes));
    unsigned child_writes=capsules[CHILD].writes,outer_writes=capsules[OUTER].writes;
    assert(prepare_role(CHILD,&prepared[OUTER].allocated_runs));
    assert(prepare_role(OUTER,&prepared[CHILD].allocated_runs));
    assert(capsules[CHILD].writes==child_writes && capsules[OUTER].writes==outer_writes);
    /* An existing aliased file must be refused before CREATE_ALWAYS can
     * truncate/free clusters belonging to the other live capsule. */
    fixture();assert(inspect(OUTER,&outer)==1);capsules[CHILD].old_table[2]=100u;capsules[CHILD].disk[0]^=1u;
    assert(!prepare_role(CHILD,&outer));assert(!capsules[CHILD].creates && !capsules[CHILD].writes);
    fixture();assert(inspect(CHILD,&child)==1);capsules[OUTER].old_table[2]=20u;capsules[OUTER].disk[0]^=1u;
    assert(!prepare_role(OUTER,&child));assert(!capsules[OUTER].creates && !capsules[OUTER].writes);
    /* The corrupt old chain extends one whole cluster beyond its useful
     * bytes. Only that allocation tail aliases the existing outer file. */
    fixture();capsules[OUTER].old_table[2]=23u;assert(inspect(OUTER,&outer)==1);
    capsules[CHILD].old_table[1]=4u;capsules[CHILD].disk[0]^=1u;
    assert(!prepare_role(CHILD,&outer));assert(!capsules[CHILD].creates && !capsules[CHILD].writes);
    /* Conversely, retain the entire other file's allocation, not only its
     * useful sectors. The child lies entirely in the outer chain's tail. */
    fixture();capsules[OUTER].disk_bytes=512u;capsules[OUTER].old_table[2]=17u;
    assert(inspect(OUTER,&outer)==1 && outer.first_lba==2268u && outer.blocks==24u);
    capsules[CHILD].old_table[2]=19u;
    assert(!prepare_role(CHILD,&outer));assert(!capsules[CHILD].creates && !capsules[CHILD].writes);
    /* A new allocation is checked before the first payload write. */
    fixture();assert(inspect(CHILD,&child)==1);capsules[OUTER].present=false;capsules[OUTER].new_table[2]=20u;
    assert(!prepare_role(OUTER,&child));assert(capsules[OUTER].creates==1u && !capsules[OUTER].writes);
    fixture();capsules[OUTER].old_table[2]=23u;assert(inspect(OUTER,&outer)==1);
    capsules[CHILD].present=false;capsules[CHILD].new_table[1]=4u;
    assert(!prepare_role(CHILD,&outer));assert(capsules[CHILD].creates==1u && !capsules[CHILD].writes);
    /* Checking the peer never modifies or truncates its file. */
    assert(!capsules[OUTER].creates && !capsules[OUTER].expands && !capsules[OUTER].writes);
    /* A peer alias appearing only on reopen must never publish the newly
     * written outer plan, even though all of its data writes succeeded. */
    fixture();assert(inspect(CHILD,&child)==1);capsules[OUTER].present=false;
    capsules[OUTER].change_map=true;capsules[OUTER].readback_table[2]=20u;
    assert(!prepare_role(OUTER,&child));assert(capsules[OUTER].writes==3u && !capsules[OUTER].reads);
}
static void verification_window_cases(void) {
    struct fixture_capsule *c=&capsules[CHILD];
    fixture();capsule_wide_window=true;assert(prepare());
    assert(c->reads==1u && !c->writes && !c->creates);
    fixture();capsule_wide_window=true;c->present=false;assert(prepare());
    assert(c->reads==1u && c->writes==1u && c->creates==1u);
    assert(!memcmp(c->disk,c->wanted,c->wanted_bytes));
    fixture();capsule_wide_window=true;c->disk[5000u]^=1u;assert(prepare());
    assert(c->writes==1u && c->reads==2u && !memcmp(c->disk,c->wanted,c->wanted_bytes));
    fixture();capsule_wide_window=true;c->fail_read=true;assert(!prepare());
    assert(!c->creates && !c->writes);
    fixture();capsule_wide_window=true;c->present=false;c->short_write=true;assert(!prepare());
    assert(c->writes==1u && !c->reads);
    fixture();capsule_wide_window=true;c->present=false;c->corrupt_readback=true;assert(!prepare());
    assert(c->writes==1u && c->reads==1u);
    fixture();capsule_wide_window=true;c->present=false;c->change_map=true;assert(!prepare());
    assert(c->writes==1u && !c->reads);
}
int main(void) {
    struct fixture_capsule *c=&capsules[CHILD];struct kui_smash_menu_capsule_runs allocated;
    fixture();assert(prepare());assert(!c->creates && !c->writes && c->reads==3u && c->maps);
    assert(prepared[CHILD].runs.first_lba==2292u && prepared[CHILD].runs.first_blocks==17u && !prepared[CHILD].runs.second_lba);
    assert(prepared[CHILD].allocated_runs.first_blocks==24u);
    fixture();c->present=false;assert(prepare());assert(c->creates==1u && c->expands==1u && c->writes==3u && c->reads==3u && c->maps>=2u);
    assert(!memcmp(c->disk,c->wanted,c->wanted_bytes));
    fixture();c->disk[5000]^=1u;assert(prepare());assert(c->creates==1u && c->writes==3u && !memcmp(c->disk,c->wanted,c->wanted_bytes));
    fixture();c->disk_bytes=512u;assert(prepare());assert(c->creates==1u && c->writes==3u);
    fixture();c->fail_read=true;assert(!prepare());assert(!c->creates && !c->writes);
    fixture();c->short_read=true;assert(!prepare());assert(!c->creates && !c->writes);
    fixture();c->fail_close=true;assert(!prepare());assert(!c->creates && !c->writes);
    fixture();c->wrong_fs=true;assert(!prepare());assert(!c->creates && !c->writes);
    fixture();cancelled_now=true;assert(!prepare());assert(!c->creates && !c->writes);
    fixture();game.slots[1].extent.card_lba=2292u;assert(!prepare());assert(!c->creates && !c->writes);
    fixture();profile.files[0].allocated[0].lba=2290u;assert(!prepare());assert(!c->creates && !c->writes);
    fixture();profile.files[0].allocated[0].lba=2309u;assert(!prepare());assert(!c->creates && !c->writes);
    fixture();c->old_table[0]=8u;c->old_table[1]=1u;c->old_table[2]=20u;c->old_table[3]=1u;c->old_table[4]=30u;
    c->old_table[5]=1u;c->old_table[6]=40u;assert(!prepare());assert(!c->creates && !c->writes);
    fixture();c->present=false;game.slots[1].extent.card_lba=2292u;assert(!prepare());assert(c->creates==1u && !c->writes);
    fixture();c->present=false;profile.files[0].allocated[0].lba=2290u;assert(!prepare());assert(c->creates==1u && !c->writes);
    fixture();c->present=false;profile.files[0].allocated[0].lba=2309u;assert(!prepare());assert(c->creates==1u && !c->writes);
    fixture();c->present=false;c->short_write=true;assert(!prepare());assert(c->writes==1u);
    fixture();c->present=false;c->fail_sync=true;assert(!prepare());assert(c->writes==3u && !c->reads);
    fixture();c->present=false;c->change_map=true;assert(!prepare());assert(c->writes==3u && !c->reads);
    fixture();c->present=false;c->corrupt_readback=true;assert(!prepare());assert(c->writes==3u && c->reads==1u);
    fixture();assert(inspect(CHILD,&allocated)==1 && allocated.blocks==24u && !c->creates && !c->writes);
    fixture();c->present=false;memset(&allocated,0xa5,sizeof(allocated));
    assert(!inspect(CHILD,&allocated) && !memcmp(&allocated,&(struct kui_smash_menu_capsule_runs){0},sizeof(allocated)));
    fixture();c->disk_bytes=0u;assert(!inspect(CHILD,&allocated) && !c->creates && !c->writes);
    fixture();c->wrong_fs=true;assert(inspect(CHILD,&allocated)<0 && !c->creates && !c->writes);
    cross_capsule_cases();verification_window_cases();
    puts("Smash menu double-capsule file preparation: OK");return 0;
}
