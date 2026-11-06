/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_session_prepare.h"
#include "kui/native_session.h"
#include "kui/native_return.h"
#include "kui/game_launch_settings.h"
#include "kui/storage_policy.h"
#include "kui/media.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(value>>(8u*i));
}
static bool same_path(const char *a,const char *b) {
    char x[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP],y[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];
    return kui_game_launch_settings_normalize(x,a) && kui_game_launch_settings_normalize(y,b) && !strcmp(x,y);
}
/* Runtime bytes are checked in bounded pages. Finding the initialized boot
 * marker here prevents a successfully reloaded image from choosing another
 * storage device when its own KOS starts again. No runtime file is changed. */
static bool runtime_plan(FATFS *fs,const struct kui_volume *volume,
    const struct kui_retail_manifest *active,struct kui_native_return_plan *plan,
    const char **problem,kui_cancel_fn cancel) {
    memset(plan,0,sizeof(*plan));
    FIL file;FRESULT r=f_open(&file,KUI_RUNTIME_PATH,FA_READ);
    if(r!=FR_OK) {*problem="return to K-UI needs a readable KUI/runtime.kui";return false;}
    UINT got=0;uint64_t bytes=f_size(&file);
    bool ok=bytes>=64u && f_read(&file,plan->header,64u,&got)==FR_OK && got==64u;
    struct kui_runtime_info info;
    if(ok)ok=kui_runtime_header(plan->header,bytes,&info)==KUI_RUNTIME_OK;
    /* Match the loader's bounded sequential reads without growing the I/O
     * worker's stack. Low memory retains the original complete 4 KiB pass. */
    uint8_t fallback[4096];
    uint8_t *allocation=ok?malloc(32768u):NULL;
    uint8_t *page=allocation?allocation:fallback;
    const UINT page_bytes=allocation?32768u:(UINT)sizeof(fallback);
    uint32_t crc=0,window[5]={0};unsigned markers=0,words=0;
    for(uint32_t at=0;ok && at<info.payload_bytes;) {
        UINT take=info.payload_bytes-at>page_bytes?page_bytes:info.payload_bytes-at;
        if(cancel()) {ok=false;*problem="return-runtime validation cancelled";break;}
        got=0;ok=f_read(&file,page,take,&got)==FR_OK && got==take;
        if(!ok)break;
        crc=kui_retail_crc32(crc,page,take);
        for(unsigned i=0;i<take;i+=4u) {
            memmove(window,window+1u,4u*sizeof(*window));window[4]=word(page+i);++words;
            if(words>=5u && window[0]==KUI_STORAGE_BOOT_MAGIC1 && window[1]==KUI_STORAGE_BOOT_MAGIC2 &&
               window[2]==1u && window[3]==KUI_STORAGE_AUTO && window[4]==~(uint32_t)KUI_STORAGE_AUTO)++markers;
        }
        at+=take;
    }
    free(allocation);
    if(ok)ok=crc==info.crc32 && markers==1u;
    if(f_close(&file)!=FR_OK)ok=false;
    if(!ok) {*problem="return runtime changed, has an invalid CRC/boot marker, or cannot be read";return false;}
    struct kui_retail_extent extents[KUI_NATIVE_RETURN_EXTENTS];unsigned count=0;uint64_t mapped_bytes=0;
    if(!kui_native_session_map_file(fs,volume,KUI_RUNTIME_PATH,extents,KUI_NATIVE_RETURN_EXTENTS,
       &count,&mapped_bytes,problem,cancel) || mapped_bytes!=bytes)return false;
    plan->version=KUI_NATIVE_RETURN_VERSION;plan->bytes=sizeof(*plan);
    plan->file_bytes=(uint32_t)bytes;plan->extent_count=count;
    plan->card_sectors=active->card_sectors;plan->partition_first=active->partition_start;
    plan->partition_end=active->partition_end;
    for(unsigned i=0;i<count;i++)plan->extents[i]=(struct kui_native_return_extent){
        extents[i].file_block,extents[i].card_lba,extents[i].blocks};
    if(!kui_native_return_plan_valid(plan)) {*problem="return runtime allocation or header is not supported";return false;}
    return !cancel();
}
bool kui_native_session_prepare(FATFS *fs,const struct kui_volume *volume,
    const char *selected,const struct kui_native_session_request *request,
    const struct kui_retail_manifest *active,const char *version,struct kui_runtime_image *package,
    const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    if(!fs || !volume || !selected || !active || !version || !package || !package->data ||
       !problem || !log || !cancel)return false;
    size_t offset=0;int found=kui_native_session_descriptor_find(package->data,package->info.payload_bytes,&offset);
    if(!found)return true;
    *problem="session-control package is ambiguous or malformed";
    if(found!=1)return false;
    if(active->storage_transport!=KUI_STORAGE_SCI || active->reader!=KUI_RETAIL_READER_ASYNC ||
       (active->flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED)) ||
       active->track_count+active->extent_count>KUI_RETAIL_ASYNC_SLOTS ||
       kui_retail_manifest_validate(active)!=KUI_GAME_OK) {
        *problem="in-game controls currently require native SCI Background New (X)";return false;
    }
    if(request && (!request->count || request->count>KUI_NATIVE_SESSION_DISCS_MAX ||
       request->selected>=request->count ||
       !memchr(request->paths[request->selected],0,sizeof(request->paths[0])) ||
       !same_path(selected,request->paths[request->selected]))) {
        *problem="selected disc no longer matches the prepared catalogue group";return false;
    }
    for(unsigned i=0;i<active->track_count;i++) {
        const struct kui_retail_track *track=&active->slots[i].track;
        if(!kui_retail_track_control(track) && track->end_lba>active->session_lba) {
            *problem="in-game controls currently exclude high-density CDDA tracks";return false;
        }
    }
    uint8_t *wire=malloc(KUI_NATIVE_SESSION_WIRE_BYTES);
    struct kui_retail_manifest *map=malloc(sizeof(*map));
    struct kui_native_return_plan *ret=malloc(sizeof(*ret));
    bool ok=false;
    if(!wire || !map || !ret) {*problem="not enough memory to prepare in-game controls";goto done;}
    memcpy(wire,(uint8_t *)package->data+offset,KUI_NATIVE_SESSION_WIRE_BYTES);
    if(!runtime_plan(fs,volume,active,ret,problem,cancel) ||
       !kui_native_return_encode(wire+KUI_NATIVE_SESSION_RETURN_OFFSET,ret))goto done;
    uint32_t flags=KUI_NATIVE_SESSION_FLAG_RETURN;
    if(request && request->count>1u) {
        for(unsigned i=0;i<request->count;i++) {
            if(!memchr(request->paths[i],0,sizeof(request->paths[i])) || cancel()) {
                *problem="disc-set preparation stopped or has an invalid path";goto done;
            }
            char member_version[7]={0};
            if(i==request->selected) {*map=*active;snprintf(member_version,sizeof(member_version),"%s",version);}
            else if(!kui_native_session_map_gdi(fs,volume,request->paths[i],KUI_RETAIL_READER_ASYNC,
                map,member_version,problem,log,cancel))goto done;
            if(map->card_sectors!=active->card_sectors || map->partition_start!=active->partition_start ||
               map->partition_end!=active->partition_end || strcmp(map->product,active->product) ||
               strcmp(map->region,active->region) || strcmp(member_version,version)) {
                *problem="disc-set members differ in card, product, region or revision";goto done;
            }
            if(kui_retail_manifest_encode(map,wire+KUI_NATIVE_SESSION_MAP_OFFSET+i*KUI_NATIVE_SESSION_MAP_BYTES)!=KUI_GAME_OK) {
                *problem="cannot encode checked disc-set map";goto done;
            }
        }
        flags|=KUI_NATIVE_SESSION_FLAG_DISCS;
        put32(wire+KUI_NATIVE_SESSION_COUNT_OFFSET,request->count);
        put32(wire+KUI_NATIVE_SESSION_SELECTED_OFFSET,request->selected);
    }
    put32(wire+KUI_NATIVE_SESSION_FLAGS_OFFSET,flags);
    put32(wire+KUI_NATIVE_SESSION_CRC_OFFSET,kui_native_session_crc(wire));
    if(!kui_native_session_wire_valid(wire,KUI_NATIVE_SESSION_WIRE_BYTES,true)) {
        *problem="prepared in-game control descriptor failed validation";goto done;
    }
    if(cancel()) {*problem="in-game control preparation cancelled";goto done;}
    memcpy((uint8_t *)package->data+offset,wire,KUI_NATIVE_SESSION_WIRE_BYTES);
    log("In-game controls prepared: direct K-UI return%s",flags&KUI_NATIVE_SESSION_FLAG_DISCS?" and checked disc switching":"");
    ok=true;
done:
    free(wire);free(map);free(ret);return ok;
}
