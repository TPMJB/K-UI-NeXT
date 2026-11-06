/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_vmu_late_seed.h"

/* The helper is a separate read-only file. Its runs never enter the GDI TOC
 * or game extent table; only this module's unique launch-local seed changes. */
static bool vc2_late_runs(const DWORD *table,size_t words,const FATFS *fs,
    const struct kui_volume *volume,struct kui_vc2_vmu_late_seed *seed) {
    if(!table || !fs || !volume || !seed || words<4u || (words&1u) ||
       !volume->count || (uint64_t)volume->start+volume->count>UINT32_MAX ||
       !fs->database || fs->database>=volume->count || !fs->csize ||
       (fs->csize&(fs->csize-1u)) || fs->n_fatent<=2u ||
       (uint64_t)fs->database+(uint64_t)(fs->n_fatent-2u)*fs->csize>volume->count ||
       table[0]!=words || table[words-1u])return false;
    uint32_t lba[2]={0u,0u},blocks[2]={0u,0u};unsigned runs=0u;
    uint32_t done=0u;
    for(size_t at=1u;at+1u<words;at+=2u) {
        uint32_t clusters=table[at],first=table[at+1u];
        if(!clusters || first<2u || first>=fs->n_fatent ||
           clusters>fs->n_fatent-first)return false;
        uint64_t sector=(uint64_t)fs->database+(uint64_t)(first-2u)*fs->csize;
        uint64_t count=(uint64_t)clusters*fs->csize;
        if(sector<fs->database || sector>=volume->count ||
           count>volume->count-sector)return false;
        if(done==KUI_VC2_LATE_FILE_BLOCKS)continue;
        if(count>KUI_VC2_LATE_FILE_BLOCKS-done)count=KUI_VC2_LATE_FILE_BLOCKS-done;
        uint64_t card=(uint64_t)volume->start+sector;
        if(card>UINT32_MAX || card+count>(uint64_t)volume->start+volume->count)return false;
        if(runs && card==(uint64_t)lba[runs-1u]+blocks[runs-1u])
            blocks[runs-1u]+=(uint32_t)count;
        else {
            if(runs==2u)return false;
            if(runs && card<(uint64_t)lba[0]+blocks[0] && card+count>lba[0])return false;
            lba[runs]=(uint32_t)card;blocks[runs]=(uint32_t)count;++runs;
        }
        done+=(uint32_t)count;
    }
    if(done!=KUI_VC2_LATE_FILE_BLOCKS || !runs ||
       (runs==2u && (uint64_t)lba[1]<(uint64_t)lba[0]+blocks[0] &&
        (uint64_t)lba[1]+blocks[1]>lba[0]))return false;
    seed->first_lba=lba[0];seed->first_blocks=blocks[0];
    seed->second_lba=runs==2u?lba[1]:0u;
    return true;
}

static bool vc2_ce_vmu_late_prepare(FATFS *fs,const struct kui_volume *volume,
    struct kui_runtime_image *package,const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    if(!fs || !volume || !package || !package->data || !problem || !log || !cancel)return false;
    *problem="late VC2 helper seed is missing, changed or ambiguous";
    uint8_t *data=package->data;size_t found=SIZE_MAX;
    struct kui_vc2_vmu_late_seed seed={0};
    if(package->info.payload_bytes<KUI_VC2_LATE_SEED_BYTES)return false;
    for(size_t at=0u;at<=package->info.payload_bytes-KUI_VC2_LATE_SEED_BYTES;++at) {
        if(le32(data+at)!=KUI_VC2_LATE_SEED_MAGIC0 ||
           le32(data+at+4u)!=KUI_VC2_LATE_SEED_MAGIC1 ||
           le32(data+at+8u)!=KUI_VC2_LATE_SEED_VERSION ||
           le32(data+at+12u)!=KUI_VC2_LATE_SEED_BYTES)continue;
        if(found!=SIZE_MAX || (at&3u))return false;
        memcpy(&seed,data+at,sizeof(seed));
        if(seed.helper_bytes!=KUI_VC2_LATE_FILE_BYTES || seed.first_lba ||
           seed.first_blocks || seed.second_lba || seed.reserved || seed.seed_crc32)return false;
        found=at;
    }
    if(found==SIZE_MAX || cancel())return false;
    FIL file;
    *problem="late VC2 helper is missing or unreadable; install the matching update";
    if(f_open(&file,KUI_VC2_LATE_FILE_PATH,FA_READ)!=FR_OK)return false;
    bool ok=file.obj.fs==fs && f_size(&file)==KUI_VC2_LATE_FILE_BYTES;
    if(ok) {
        link_map[0]=LINK_MAP_WORDS;file.cltbl=link_map;
        FRESULT result=f_lseek(&file,CREATE_LINKMAP);
        *problem="late VC2 helper allocation needs at most two valid physical runs";
        ok=result==FR_OK && link_map[0]>=4u && link_map[0]<=LINK_MAP_WORDS &&
            vc2_late_runs(link_map,link_map[0],fs,volume,&seed);
    }
    uint8_t page[4096];uint32_t crc=0u;
    if(ok)*problem="late VC2 helper changed, was cancelled or could not be read";
    if(ok)ok=f_lseek(&file,0u)==FR_OK && f_tell(&file)==0u;
    for(uint32_t at=0u;ok && at<KUI_VC2_LATE_FILE_BYTES;) {
        if(cancel()) {ok=false;break;}
        UINT take=KUI_VC2_LATE_FILE_BYTES-at>sizeof(page)?sizeof(page):KUI_VC2_LATE_FILE_BYTES-at;
        UINT got=0u;ok=f_read(&file,page,take,&got)==FR_OK && got==take;
        if(ok)crc=kui_retail_crc32(crc,page,take);
        at+=take;
    }
    file.cltbl=NULL;
    if(f_close(&file)!=FR_OK) {ok=false;*problem="cannot close the read-only late VC2 helper";}
    if(!ok || crc!=seed.helper_crc32 || cancel())return false;
    seed.seed_crc32=0u;
    seed.seed_crc32=kui_retail_crc32(0u,&seed,sizeof(seed));
    memcpy(data+found,&seed,sizeof(seed));
    log("VC2 helper: checked %u bytes in %u read-only physical runs; CRC32=%08x",
        KUI_VC2_LATE_FILE_BYTES,seed.second_lba?2u:1u,crc);
    return true;
}
