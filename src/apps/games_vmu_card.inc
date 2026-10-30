/* SPDX-License-Identifier: GPL-3.0-only */
/* Gives each game its own virtual card: the virtual-A2 module's P2 profile
 * slot is filled with the game's pair before handoff. The stage admits that
 * pair at boot exactly as it admits an installed one. */
#include "kui/vmu_game_card_service.h"
#include "kui/vmu_game_info.h"
#include "kui/vmu_backing.h"
#include "kui/clock.h"
#include "kui/vmu_native_toggle.h"
#include "kui/vmu_native_mode.h"

struct games_card_platform {FATFS *fs;struct kui_volume volume;struct kui_media_ops raw;};
static int games_card_read(void *context,uint32_t lba,uint8_t out[512]) {
    struct games_card_platform *p=context;
    return p->raw.read && !p->raw.read(p->raw.ctx,lba,1u,out)?0:-1;
}
/* CMD10 while the mounted volume is idle, as the import tool pins its card. */
static bool games_card_identity(void *context,struct kui_vmu_game_card_raw *out) {
    struct kui_vmu_sd_identity id;(void)context;
    if(!kui_sd_vmu_profile_identity(&id)) return false;
    memcpy(out->cid,id.cid,16u);out->card_sectors=id.sectors;out->high_capacity=id.high_capacity;
    return true;
}
static bool games_card_raw_begin(void *context,struct kui_vmu_game_card_raw *out) {
    struct games_card_platform *p=context;
    if(f_mount(NULL,"0:",0)!=FR_OK || !kui_sd_raw_read_ops(&p->raw) || !p->raw.read) return false;
    out->read_sector=games_card_read;out->context=p;
    return true;
}
static bool games_card_raw_end(void *context) {
    struct games_card_platform *p=context;p->raw=(struct kui_media_ops){0};
    if(f_mount(p->fs,"0:",1)!=FR_OK) {(void)f_mount(NULL,"0:",0);return false;}
    const struct kui_volume *now=kui_media_volume();
    return (p->fs->fs_type==FS_FAT32 || p->fs->fs_type==FS_EXFAT) &&
        now->start==p->volume.start && now->count==p->volume.count;
}
static void games_card_salt(void *context,uint8_t out[16]) {
    static uint32_t calls;struct games_card_platform *p=context;
    struct kui_datetime now;int64_t seconds=0;
    if(kui_clock_now(&now)) (void)kui_clock_to_seconds(&now,&seconds);
    uint32_t words[4]={(uint32_t)seconds,(uint32_t)((uint64_t)seconds>>32),++calls,p->fs->last_clst};
    memcpy(out,words,sizeof(words));
}
/* A game explicitly assigned to an imported card must never silently run
 * through a loader that cannot expose virtual A2. Other games keep their
 * existing Legacy/standalone behavior. This is mounted read-only file I/O;
 * unlike profile preparation it never unmounts or opens raw storage. */
static bool games_card_route(const struct kui_retail_manifest *map,bool emulation_enabled,bool virtual_a2,bool standalone,
    const char **problem,kui_cancel_fn cancel) {
    static char refusal[128];
    /* The user's explicit Off choice keeps the assignment for a later On
     * launch. Do not inspect card files or implicitly change that choice. */
    if(!emulation_enabled) return true;
    if(virtual_a2) return true; /* games_card_prepare validates the assignment. */
    char key[12];
    if(!kui_vmu_game_card_key(map->product,key)) return true;
    struct kui_vmu_sd_identity id;struct kui_vmu_game_candidates_storage storage={0};
    if(!kui_sd_vmu_profile_identity(&id) || !id.sectors) {
        *problem="cannot verify the SD card holding virtual VMU choices";return false;
    }
    memcpy(storage.cid,id.cid,16u);storage.card_sectors=id.sectors;
    storage.high_capacity=id.high_capacity?1u:0u;
    storage.partition_first=map->partition_start;storage.partition_end=map->partition_end;
    struct kui_vmu_game_candidates_view candidates;
    int listed=kui_vmu_game_candidates_list(key,&storage,&candidates,NULL,cancel);
    if(listed!=KUI_VMU_GAME_CANDIDATES_ABSENT) {
        *problem=listed==KUI_VMU_GAME_CANDIDATES_OK?
            "this game has virtual VMU choices; use a matching virtual-A2 module":
            "virtual VMU choices could not be verified; reopen game options";
        return false;
    }
    struct kui_vmu_game_card_work *work=malloc(sizeof(*work));
    if(!work) {*problem="not enough memory to check the game's virtual card";return false;}
    struct kui_vmu_game_card_choice choice;
    int result=kui_vmu_game_card_assignment(map->product,work,&choice,cancel);
    free(work);
    if(result==KUI_VMU_GAME_CARD_UNBOUND) return true;
    if(result==KUI_VMU_GAME_CARD_READY) {
        *problem=standalone?"this game uses standalone New, which cannot use its assigned virtual A2 card":
            "this game has an imported virtual A2 card; launch it with the combined New reader";
    } else if(result==KUI_VMU_GAME_CARD_CANCELLED) *problem="cancelled while checking the assigned virtual card";
    else {
        snprintf(refusal,sizeof(refusal),"virtual card: %s",choice.problem);
        *problem=refusal;
    }
    return false;
}
static uint32_t games_card_mode_u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
/* Change only the in-memory launch mode, after proving that this module
 * implements physical-VMU passthrough with the unchanged General cache.
 * Older modules retain their existing On behavior and refuse explicit Off. */
static bool games_card_emulation_mode(struct kui_runtime_image *package,bool enabled,const char **problem) {
    if(!package || !package->data) {*problem="native VMU module is unavailable";return false;}
    uint8_t *payload=(uint8_t *)package->data;
    size_t bytes=package->info.payload_bytes;
    int capable=kui_vmu_native_toggle_find(payload,bytes,NULL);
    if(!capable && enabled) return true;
    if(capable!=1) {
        *problem="VMU Off needs the matching toggle-capable native module; reinstall this update";
        return false;
    }
    size_t slot=SIZE_MAX;
    static const uint8_t magic[8]={'K','U','I','N','V','M','0','1'};
    for(size_t at=0;bytes>=KUI_VMU_NATIVE_MODE_BYTES && at<=bytes-KUI_VMU_NATIVE_MODE_BYTES;at++) {
        if(memcmp(payload+at,magic,sizeof(magic))) continue;
        const uint8_t *wire=payload+at;
        uint32_t mode=games_card_mode_u32(wire+16u);
        bool reserved=false;
        for(unsigned i=20u;i<KUI_VMU_NATIVE_MODE_BYTES;i++) reserved|=wire[i]!=0;
        if(games_card_mode_u32(wire+8u)!=1u ||
           games_card_mode_u32(wire+12u)!=KUI_VMU_NATIVE_MODE_BYTES || reserved ||
           mode>KUI_VMU_NATIVE_MODE_READER_ONLY) continue;
        if((mode!=KUI_VMU_NATIVE_MODE_NORMAL && mode!=KUI_VMU_NATIVE_MODE_READER_ONLY) || slot!=SIZE_MAX) {
            *problem="native VMU toggle mode is damaged or ambiguous; reinstall this update";
            return false;
        }
        slot=at;
    }
    if(slot==SIZE_MAX) {*problem="native VMU toggle mode is missing; reinstall this update";return false;}
    uint32_t mode=enabled?KUI_VMU_NATIVE_MODE_NORMAL:KUI_VMU_NATIVE_MODE_READER_ONLY;
    for(unsigned i=0;i<4u;i++) payload[slot+16u+i]=(uint8_t)(mode>>(8u*i));
    return true;
}
/* Called once the virtual-A2 module will really run this game. UNBOUND
 * (no usable product number) keeps an installed card; the build template
 * refuses, since the stage could never admit it. */
static bool games_card_prepare_selected(const struct kui_retail_manifest *map,struct kui_runtime_image *package,
    FATFS *fs,const struct kui_vmu_game_selection *selection,const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    static char refusal[128];
    size_t slot=kui_vmu_game_card_profile_slot(package->data,package->info.payload_bytes);
    if(slot==SIZE_MAX) {*problem="virtual-A2 module has no card profile slot; reinstall the matching candidate";return false;}
    struct kui_vmu_game_card_work *work=malloc(sizeof(*work));
    struct kui_vmu_fs_range *forbidden=map->extent_count?malloc(sizeof(*forbidden)*map->extent_count):NULL;
    uint8_t *wire=malloc(KUI_VMU_PERSIST_PROFILE_WIRE_BYTES);
    if(!work || !wire || (map->extent_count && !forbidden)) {
        free(work);free(forbidden);free(wire);*problem="not enough memory for the virtual card";return false;
    }
    for(uint32_t i=0;i<map->extent_count;i++) {
        const struct kui_retail_extent *e=&map->slots[map->track_count+i].extent;
        forbidden[i]=(struct kui_vmu_fs_range){e->card_lba,e->blocks};
    }
    struct games_card_platform context={fs,*kui_media_volume(),{0}};
    const struct kui_vmu_game_card_platform platform={&context,games_card_identity,games_card_raw_begin,
        games_card_raw_end,games_card_salt};
    struct kui_vmu_game_card_choice choice;
    int result;
    if(selection && selection->selected) {
        char key[12];
        if(!kui_vmu_game_card_key(map->product,key) || memcmp(key,selection->product,sizeof(key))) {
            free(work);free(forbidden);free(wire);*problem="the virtual VMU choice belongs to another game";return false;
        }
        struct kui_vmu_game_candidate_target target={.pair=selection->pair};
        memcpy(target.identity,selection->identity,16u);
        result=kui_vmu_game_card_select_exact(map->product,&target,&selection->storage,
            map->partition_start,map->partition_end,forbidden,map->extent_count,
            &platform,work,wire,&choice,log,cancel);
    } else result=kui_vmu_game_card_select(map->product,map->partition_start,map->partition_end,
        forbidden,map->extent_count,&platform,work,wire,&choice,log,cancel);
    bool ok=false;
    if(result==KUI_VMU_GAME_CARD_READY) {
        memcpy((uint8_t *)package->data+slot,wire,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES);ok=true;
    } else if(result==KUI_VMU_GAME_CARD_UNBOUND) {
        if(kui_vmu_persist_profile_decode(&work->cached,(const uint8_t *)package->data+slot,
           KUI_VMU_PERSIST_PROFILE_WIRE_BYTES) || kui_vmu_game_card_template(work->cached.cid))
            *problem="this game has no product number for its own virtual card, and no card is installed";
        else {log("Virtual card: product \"%s\" names no game card; using the installed card",map->product);ok=true;}
    } else if(result==KUI_VMU_GAME_CARD_CHOICE_REQUIRED) *problem="choose a virtual VMU in game options before launching";
    else if(result==KUI_VMU_GAME_CARD_CANCELLED) *problem="cancelled while preparing the virtual card";
    else {
        snprintf(refusal,sizeof(refusal),"virtual card: %s",choice.problem);
        *problem=refusal;
    }
    free(work);free(forbidden);free(wire);
    return ok;
}
static bool games_card_prepare(const struct kui_retail_manifest *map,struct kui_runtime_image *package,
    FATFS *fs,const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    return games_card_prepare_selected(map,package,fs,NULL,problem,log,cancel);
}
