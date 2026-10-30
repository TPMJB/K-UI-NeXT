/* SPDX-License-Identifier: GPL-3.0-only */
/* Read-only GUI admission of active cards and immutable import snapshots.
 * These functions are deliberately separate from the native save runtime. */
#include "kui/vmu_library.h"
#include "kui/vmu_collection.h"
#include "kui/vmu_game_assign.h"
#include "kui/vmu_game_card.h"
#include "kui/vmu_persist_profile.h"
#include "kui/hash.h"
#include "kui/retail_image.h"
#include "kui/game_cover.h"
#include "kui/vmu_description.h"
#include "ff.h"
#include "diskio.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ACTIVE_ROOT "0:/KUI/vmu-test"
#define SNAPSHOT_ROOT "0:" KUI_VMU_COLLECTION_ROOT
#define NAME_BYTES 128u
#define NAME_LIMIT 48u
#define CATALOG_BITMAP_BYTES 1250u
#define TITLE_SCAN_LIMIT 2000u
enum { OK=KUI_VMU_LIBRARY_OK, ARG=KUI_VMU_LIBRARY_ARGUMENT,
       IO=KUI_VMU_LIBRARY_IO, BAD=KUI_VMU_LIBRARY_INVALID,
       STOP=KUI_VMU_LIBRARY_CANCELLED };

static bool stopped(kui_cancel_fn cancel) {return cancel && cancel();}
static bool zero(const void *data,size_t n) {
    const uint8_t *p=data;while(n--) if(*p++) return false;return true;
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t get64(const uint8_t *p) {return (uint64_t)get32(p)|(uint64_t)get32(p+4)<<32;}
static void put32(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8u*i));}
static void put64(uint8_t *p,uint64_t v) {put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32));}
static bool leaf(char out[192],const char *path,const char *name) {
    const char *slash=strrchr(path,'/');
    if(!slash || (size_t)(slash-path)>160u) return false;
    int n=snprintf(out,192,"%.*s/%s",(int)(slash-path),path,name);
    return n>0 && n<192;
}
static unsigned path_pair(const char *path) {
    static const char prefix[]=ACTIVE_ROOT "/p";
    if(!path || strncmp(path,prefix,sizeof(prefix)-1u)) return 0;
    const char *at=path+sizeof(prefix)-1u;unsigned pair=0;
    for(unsigned i=0;i<4u;i++) {
        if(at[i]<'0' || at[i]>'9') return 0;
        pair=pair*10u+(unsigned)(at[i]-'0');
    }
    return pair && !strcmp(at+4,"/card.vmd")?pair:0;
}
bool kui_vmu_library_path(const char *path) {
    return path && (path_pair(path) || kui_vmu_collection_path_id(path));
}
static int read_exact(const char *path,void *out,size_t bytes,kui_cancel_fn cancel) {
    if(stopped(cancel)) return STOP;
    FIL file;UINT got=0;FRESULT r=f_open(&file,path,FA_READ);
    if(r!=FR_OK) return r==FR_NO_FILE || r==FR_NO_PATH?BAD:IO;
    bool ok=f_size(&file)==bytes && f_read(&file,out,(UINT)bytes,&got)==FR_OK && got==bytes;
    if(f_close(&file)!=FR_OK) return IO;
    return stopped(cancel)?STOP:ok?OK:BAD;
}
/* missing is separate from a malformed existing file. */
static int optional_exact(const char *path,void *out,size_t bytes,bool *present,kui_cancel_fn cancel) {
    FILINFO info;FRESULT r=f_stat(path,&info);*present=false;
    if(r==FR_NO_FILE || r==FR_NO_PATH) return OK;
    if(r!=FR_OK) return IO;
    *present=true;
    if((info.fattrib&AM_DIR) || info.fsize!=bytes) return BAD;
    return read_exact(path,out,bytes,cancel);
}
static int compare_file(const char *path,const void *data,size_t bytes,kui_cancel_fn cancel) {
    FIL file;uint8_t block[512];UINT got=0;
    if(f_open(&file,path,FA_READ)!=FR_OK) return IO;
    int result=f_size(&file)==bytes?OK:BAD;
    for(size_t at=0;result==OK && at<bytes;at+=sizeof(block)) {
        if(stopped(cancel)) {result=STOP;break;}
        UINT n=(UINT)(bytes-at<sizeof(block)?bytes-at:sizeof(block));
        if(f_read(&file,block,n,&got)!=FR_OK || got!=n) result=IO;
        else if(memcmp(block,(const uint8_t *)data+at,n)) result=BAD;
    }
    if(f_close(&file)!=FR_OK) result=IO;
    return result;
}
static int memory_block(void *context,uint16_t block,void *out) {
    if(block>=256u) return -1;
    memcpy(out,(const uint8_t *)context+(size_t)block*512u,512u);return 0;
}
static int image_summary(uint8_t *image,struct kui_vmu_image_summary *summary) {
    struct kui_vmu_image_workspace *work=malloc(sizeof(*work));
    if(!work) return IO;
    const struct kui_vmu_image_io io={image,memory_block,NULL,NULL};
    int result=kui_vmu_image_validate(&io,KUI_VMU_IMAGE_BYTES,work,summary);
    free(work);return result?BAD:OK;
}
struct active_card {
    uint8_t identity[16],journal[1024],binding[64],import[64];
    char key[KUI_VMU_GAME_CARD_KEY_BYTES];
    bool binding_present,import_present,profile_present;
    struct kui_vmu_persist_profile profile;
};
static int active_metadata(const char *path,struct active_card *card,kui_cancel_fn cancel) {
    char name[192];unsigned pair=path_pair(path);
    if(!pair) return ARG;
    memset(card,0,sizeof(*card));
    if(!leaf(name,path,"identity.bin")) return ARG;
    int result=read_exact(name,card->identity,16u,cancel);
    if(result || zero(card->identity,16u)) return result?result:BAD;
    if(!leaf(name,path,"card.journal")) return ARG;
    if((result=read_exact(name,card->journal,sizeof(card->journal),cancel))) return result;
    if(!leaf(name,path,KUI_VMU_GAME_CARD_BINDING_NAME)) return ARG;
    if((result=optional_exact(name,card->binding,sizeof(card->binding),&card->binding_present,cancel))) return result;
    if(card->binding_present && !kui_vmu_game_card_binding_decode(card->binding,sizeof(card->binding),card->key)) return BAD;
    if(!leaf(name,path,"import.bin")) return ARG;
    if((result=optional_exact(name,card->import,sizeof(card->import),&card->import_present,cancel))) return result;
    if(card->import_present) {
        uint8_t expected[64]={0};memcpy(expected,"KUIVMUI1",8u);memcpy(expected+16,card->identity,16u);
        put32(expected+60,kui_crc32(0,expected,60));
        if(memcmp(expected,card->import,64)) return BAD;
    }
    char profile_name[48];snprintf(profile_name,sizeof(profile_name),"retail-persistent-profile-%04u.bin",pair);
    if(!leaf(name,path,profile_name)) return ARG;
    uint8_t *wire=malloc(KUI_VMU_PERSIST_PROFILE_WIRE_BYTES);
    if(!wire) return IO;
    result=optional_exact(name,wire,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES,&card->profile_present,cancel);
    if(!result && card->profile_present) {
        char expected[96]={0};snprintf(expected,sizeof(expected),"/KUI/vmu-test/p%04u",pair);
        if(kui_vmu_persist_profile_decode(&card->profile,wire,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES) ||
           memcmp(card->profile.image_id,card->identity,16u) ||
           memcmp(card->profile.pair_path,expected,sizeof(expected)) ||
           kui_vmu_game_card_template(card->profile.cid)) result=BAD;
    }
    free(wire);
    if(result) return result;
    return card->binding_present || card->import_present || card->profile_present?OK:BAD;
}
/* Same descriptor hash used by the journal store: identity, capacity, then
 * the exact logical-order useful image and journal extents. */
static uint32_t mapping_crc(const struct active_card *card) {
    const struct kui_vmu_persist_profile *p=&card->profile;
    uint8_t wire[16];uint32_t crc=kui_retail_crc32(0,p->image_id,16u);
    put64(wire,p->card_sectors);put32(wire+8,p->files[0].useful_count);put32(wire+12,p->files[1].useful_count);
    crc=kui_retail_crc32(crc,wire,sizeof(wire));
    for(unsigned role=0;role<2u;role++) for(unsigned i=0;i<p->files[role].useful_count;i++) {
        const struct kui_vmu_persist_extent *e=&p->files[role].useful[i];
        put64(wire,e->lba);put32(wire+8,e->blocks);crc=kui_retail_crc32(crc,wire,12u);
    }
    return crc;
}
static int overlay_journal(const struct active_card *card,uint8_t *image) {
    const uint8_t *marker=card->journal+512u;
    /* Match the native redo protocol's invalid/torn-marker behavior. */
    if(get32(marker+508)!=kui_retail_crc32(0,marker,508u)) return OK;
    if(memcmp(marker,"KUIVMUJ1",8u) || get32(marker+8)!=1u || get32(marker+12)!=512u ||
       get32(marker+44)>=256u || get32(marker+52)!=1u || !zero(marker+64,444u)) return BAD;
    if(!card->profile_present || memcmp(marker+16,card->identity,16u) ||
       get64(marker+32)!=card->profile.card_sectors || get32(marker+40)!=mapping_crc(card) ||
       get32(marker+48)!=kui_retail_crc32(0,card->journal,512u)) return BAD;
    memcpy(image+(size_t)get32(marker+44)*512u,card->journal,512u);return OK;
}
static int read_admitted(const char *path,uint8_t *image,uint8_t identity[16],
    char key[KUI_VMU_GAME_CARD_KEY_BYTES],struct kui_vmu_image_summary *summary,char legacy_label[16],
    kui_log_fn log,kui_cancel_fn cancel) {
    if(!kui_vmu_library_path(path) || !image) return ARG;
    if(stopped(cancel)) return STOP;
    int result=OK;
    if(path_pair(path)) {
        struct active_card *before=malloc(sizeof(*before)),*after=malloc(sizeof(*after));
        if(!before || !after) {free(before);free(after);return IO;}
        result=active_metadata(path,before,cancel);
        if(!result) result=read_exact(path,image,KUI_VMU_IMAGE_BYTES,cancel);
        if(!result) result=compare_file(path,image,KUI_VMU_IMAGE_BYTES,cancel);
        if(!result) result=active_metadata(path,after,cancel);
        if(!result && memcmp(before,after,sizeof(*before))) result=BAD;
        if(!result) result=overlay_journal(before,image);
        if(!result) {
            memcpy(identity,before->identity,16u);memcpy(key,before->key,KUI_VMU_GAME_CARD_KEY_BYTES);
            result=image_summary(image,summary);
        }
        free(before);free(after);
    } else {
        struct kui_vmu_collection_card card;
        if(kui_vmu_collection_verify(path,&card,log,cancel)) result=stopped(cancel)?STOP:BAD;
        if(!result) result=read_exact(path,image,KUI_VMU_IMAGE_BYTES,cancel);
        if(!result) result=compare_file(path,image,KUI_VMU_IMAGE_BYTES,cancel);
        if(!result) {
            struct kui_sha256 sha;uint8_t digest[32];kui_sha256_init(&sha);
            kui_sha256_update(&sha,image,KUI_VMU_IMAGE_BYTES);kui_sha256_digest(&sha,digest);
            if(memcmp(digest,card.sha256,32u)) result=BAD;
        }
        char name[192];
        if(!result && (!leaf(name,path,"identity.bin") || (result=read_exact(name,identity,16u,cancel)))) result=result?result:BAD;
        if(!result) {
            memset(key,0,KUI_VMU_GAME_CARD_KEY_BYTES);*summary=card.summary;
            if(legacy_label) memcpy(legacy_label,card.label,16u);
        }
    }
    return stopped(cancel)?STOP:result;
}
int kui_vmu_library_read(const char *path,uint8_t image[KUI_VMU_IMAGE_BYTES],kui_log_fn log,kui_cancel_fn cancel) {
    uint8_t identity[16];char key[KUI_VMU_GAME_CARD_KEY_BYTES];struct kui_vmu_image_summary summary;
    return read_admitted(path,image,identity,key,&summary,NULL,log,cancel);
}
static bool valid_name(const char *name) {
    if(!name) return false;
    bool visible=false;
    for(unsigned i=0;i<=NAME_LIMIT;i++) {
        unsigned char c=(unsigned char)name[i];
        if(!c) return i && visible;
        if(i==NAME_LIMIT || c<32u || c>126u) return false;
        if(c!=' ') visible=true;
    }
    return false;
}
static bool name_decode(const uint8_t data[NAME_BYTES],const uint8_t identity[16],uint64_t *sequence,char name[49]) {
    if(memcmp(data,"KUIVMUN1",8u) || get32(data+8)!=1u || get32(data+12)!=NAME_BYTES ||
       !get64(data+16) || memcmp(data+24,identity,16u) || !memchr(data+40,0,49u) ||
       !zero(data+89,35u) || get32(data+124)!=kui_crc32(0,data,124u)) return false;
    memcpy(name,data+40,49u);
    if(!valid_name(name)) return false;
    size_t n=strlen(name);
    if(!zero(data+40+n,49u-n)) return false;
    *sequence=get64(data+16);return true;
}
static int name_load(const char *path,const uint8_t identity[16],char name[49],uint64_t *sequence,int *slot,kui_cancel_fn cancel) {
    name[0]=0;*sequence=0;*slot=-1;
    for(unsigned i=0;i<2u;i++) {
        char leaf_name[16],file[192],candidate[49];uint8_t data[NAME_BYTES];uint64_t seq=0;bool present;
        snprintf(leaf_name,sizeof(leaf_name),"name-%c.bin",'a'+i);
        if(!leaf(file,path,leaf_name)) return ARG;
        int result=optional_exact(file,data,sizeof(data),&present,cancel);
        if(result==IO || result==STOP) return result;
        if(result || !present || !name_decode(data,identity,&seq,candidate)) continue;
        if(*slot>=0 && seq==*sequence && strcmp(name,candidate)) {name[0]=0;*slot=-1;*sequence=UINT64_MAX;return BAD;}
        if(*slot<0 || seq>*sequence) {memcpy(name,candidate,49u);*sequence=seq;*slot=(int)i;}
    }
    return OK;
}
int kui_vmu_library_label(const char *path,const uint8_t expected_identity[16],char out[49],kui_cancel_fn cancel) {
    if(!out) return ARG;
    out[0]=0;
    if(!kui_vmu_library_path(path) || !expected_identity || zero(expected_identity,16u)) return ARG;
    char file[192];uint8_t identity[16];
    if(!leaf(file,path,"identity.bin")) return ARG;
    int result=read_exact(file,identity,sizeof(identity),cancel);
    if(result || memcmp(identity,expected_identity,16u)) return result?result:BAD;
    uint64_t sequence;int slot;
    result=name_load(path,identity,out,&sequence,&slot,cancel);
    if(result) out[0]=0;
    return result;
}
/* Display metadata still allocates filesystem clusters. Before allowing that
 * allocation, prove that VMU files and their directories remain allocated and
 * disjoint. Otherwise a damaged bitmap or cross-linked chain could lend save
 * data or directory clusters to a new name record or its directory entry. */
#define LABEL_MAP_RUNS 64u
struct label_file_map {FATFS *fs;DWORD runs[2u+2u*LABEL_MAP_RUNS];unsigned count;};
static int label_object_map(const FFOBJID *object,bool directory,struct label_file_map *map,kui_cancel_fn cancel);
static int label_file_map(const char *path,struct label_file_map *map,kui_cancel_fn cancel) {
    FIL file;memset(map,0,sizeof(*map));
    if(f_open(&file,path,FA_READ)!=FR_OK) return IO;
    int result=label_object_map(&file.obj,false,map,cancel);
    if(f_close(&file)!=FR_OK) result=IO;
    return result;
}
static int label_allocated(const struct label_file_map *map,kui_cancel_fn cancel) {
    if(map->fs->fs_type!=FS_EXFAT) return OK;
    uint8_t bitmap[512];LBA_t loaded=(LBA_t)-1;
    for(unsigned i=0;i<map->count;i++) {
        uint32_t first=map->runs[2u+2u*i],n=map->runs[1u+2u*i];
        for(uint32_t cluster=first;cluster<first+n;cluster++) {
            if(stopped(cancel)) return STOP;
            uint32_t bit=cluster-2u;LBA_t sector=map->fs->bitbase+bit/4096u;
            if(sector!=loaded) {
                if(disk_read(map->fs->pdrv,bitmap,sector,1u)!=RES_OK) return IO;
                loaded=sector;
            }
            if(!(bitmap[(bit%4096u)/8u]&(1u<<(bit%8u)))) return BAD;
        }
    }
    return OK;
}
/* Directory chains need the same protection as file payloads: allocating a
 * cluster marked free while it still holds a directory can erase its saves.
 * Read the allocation information from DIR.obj, but never pass a synthetic
 * FIL to FatFs (its seek implementation may materialize exFAT fragments). */
static int label_object_map(const FFOBJID *object,bool directory,struct label_file_map *map,kui_cancel_fn cancel) {
    memset(map,0,sizeof(*map));map->fs=object->fs;
    FATFS *fs=map->fs;
    if(!fs || (fs->fs_type!=FS_FAT32 && fs->fs_type!=FS_EXFAT) || !fs->csize || fs->n_fatent<3u) return BAD;
    if(!directory && !object->sclust) return object->objsize?BAD:OK;
    uint32_t first=object->sclust?object->sclust:(uint32_t)fs->dirbase;
    if(first<2u || first>=fs->n_fatent) return BAD;
    uint64_t cluster_bytes=(uint64_t)fs->csize*512u,expected=0;
    if(object->sclust && (fs->fs_type==FS_EXFAT || !directory)) {
        if(!object->objsize || (directory && object->objsize%cluster_bytes)) return BAD;
        expected=(object->objsize-1u)/cluster_bytes+1u;
        if(expected>fs->n_fatent-2u || expected>65536u) return BAD;
        if(fs->fs_type==FS_EXFAT && object->stat!=0u && object->stat!=2u) return BAD;
        if(fs->fs_type==FS_EXFAT && object->stat==2u) {
            if(expected>fs->n_fatent-first) return BAD;
            map->count=1u;map->runs[1]=(DWORD)expected;map->runs[2]=first;return OK;
        }
    }
    uint8_t fat[512];LBA_t loaded=(LBA_t)-1;uint32_t cluster=first,total=0;
    for(;;) {
        if(stopped(cancel)) return STOP;
        if(cluster<2u || cluster>=fs->n_fatent || ++total>fs->n_fatent-2u || total>65536u) return BAD;
        /* Revisited clusters are a cycle, including backward contiguous runs. */
        for(unsigned i=0;i<map->count;i++) {
            uint32_t at=map->runs[2u+2u*i],n=map->runs[1u+2u*i];
            if(cluster>=at && cluster-at<n) return BAD;
        }
        if(map->count && map->runs[2u+2u*(map->count-1u)]+map->runs[1u+2u*(map->count-1u)]==cluster)
            ++map->runs[1u+2u*(map->count-1u)];
        else {
            if(map->count>=LABEL_MAP_RUNS) return BAD;
            map->runs[1u+2u*map->count]=1u;map->runs[2u+2u*map->count]=cluster;++map->count;
        }
        uint32_t offset=cluster/128u;
        if(offset>=fs->fsize) return BAD;
        LBA_t sector=fs->fatbase+offset;
        if(sector!=loaded) {
            if(disk_read(fs->pdrv,fat,sector,1u)!=RES_OK) return IO;
            loaded=sector;
        }
        uint32_t next=get32(fat+(cluster%128u)*4u)&(fs->fs_type==FS_FAT32?UINT32_C(0x0fffffff):UINT32_C(0x7fffffff));
        uint32_t end=fs->fs_type==FS_FAT32?UINT32_C(0x0ffffff8):UINT32_C(0x7ffffff8);
        if(next>=end) return expected && (directory?total!=expected:total<expected)?BAD:OK;
        if(next<2u || next>=fs->n_fatent) return BAD;
        cluster=next;
    }
}
/* Keep a sorted ownership set so directories, files and name records cannot
 * silently share clusters. The set is GUI scratch and is freed before return.
 * Its bound is a refusal limit, never a reason to skip an unchecked object. */
struct label_extent {uint32_t first,count;};
struct label_ownership {FATFS *fs;struct label_extent *extents;size_t count,capacity;};
static int label_owned(struct label_ownership *owned,const struct label_file_map *map) {
    if(owned->fs && owned->fs!=map->fs) return BAD;
    owned->fs=map->fs;
    for(unsigned i=0;i<map->count;i++) {
        uint32_t first=map->runs[2u+2u*i],count=map->runs[1u+2u*i];size_t low=0,high=owned->count;
        while(low<high) {size_t middle=low+(high-low)/2u;if(owned->extents[middle].first<first) low=middle+1u;else high=middle;}
        if((low && first-owned->extents[low-1u].first<owned->extents[low-1u].count) ||
           (low<owned->count && owned->extents[low].first-first<count)) return BAD;
        if(owned->count==owned->capacity) {
            size_t capacity=owned->capacity?owned->capacity*2u:128u;
            if(capacity>65536u) return BAD;
            struct label_extent *grown=realloc(owned->extents,capacity*sizeof(*grown));
            if(!grown) return IO;
            owned->extents=grown;owned->capacity=capacity;
        }
        memmove(owned->extents+low+1u,owned->extents+low,(owned->count-low)*sizeof(*owned->extents));
        owned->extents[low]=(struct label_extent){first,count};++owned->count;
    }
    return OK;
}
static int label_directory_owned(const FFOBJID *object,struct label_ownership *owned,kui_cancel_fn cancel) {
    struct label_file_map map;int result=label_object_map(object,true,&map,cancel);
    if(!result) result=label_allocated(&map,cancel);
    if(!result) result=label_owned(owned,&map);
    return result;
}
static int label_tree(const char *directory,unsigned depth,
    struct label_ownership *owned,unsigned *entries,kui_cancel_fn cancel) {
    DIR dir;FILINFO info;FRESULT r=f_opendir(&dir,directory);
    if(r==FR_NO_PATH || r==FR_NO_FILE) return OK;
    if(r!=FR_OK) return IO;
    int result=label_directory_owned(&dir.obj,owned,cancel);
    while(!result && (r=f_readdir(&dir,&info))==FR_OK && info.fname[0]) {
        if(stopped(cancel)) {result=STOP;break;}
        if(++*entries>100000u) {result=BAD;break;}
        char path[192];int n=snprintf(path,sizeof(path),"%s/%s",directory,info.fname);
        if(n<0 || n>=(int)sizeof(path)) {result=BAD;break;}
        if(info.fattrib&AM_DIR) {
            if(depth>=3u) {result=BAD;break;}
            result=label_tree(path,depth+1u,owned,entries,cancel);
        } else {
            struct label_file_map map;result=label_file_map(path,&map,cancel);
            if(!result) result=label_allocated(&map,cancel);
            if(!result) result=label_owned(owned,&map);
        }
        if(result) break;
    }
    if(r!=FR_OK || f_closedir(&dir)!=FR_OK) return IO;
    return result;
}
int kui_vmu_library_check_allocation(kui_cancel_fn cancel) {
    unsigned entries=0;struct label_ownership owned={0};int result=OK;
    static const char *const ancestors[]={"0:/","0:/KUI"};
    for(unsigned i=0;!result && i<2u;i++) {
        DIR dir;FRESULT opened=f_opendir(&dir,ancestors[i]);
        if(opened==FR_NO_PATH || opened==FR_NO_FILE) continue;
        if(opened!=FR_OK) {result=IO;break;}
        result=label_directory_owned(&dir.obj,&owned,cancel);
        if(f_closedir(&dir)!=FR_OK) result=IO;
    }
    if(!result) result=label_tree(ACTIVE_ROOT,0u,&owned,&entries,cancel);
    if(!result) result=label_tree("0:/KUI/vmu",0u,&owned,&entries,cancel);
    free(owned.extents);
    return result;
}
int kui_vmu_library_rename(const char *path,const char *name,const uint8_t expected_identity[16],
    kui_log_fn log,kui_cancel_fn cancel) {
    if(!kui_vmu_library_path(path) || !valid_name(name) || !expected_identity || zero(expected_identity,16u)) return ARG;
    uint8_t *image=malloc(KUI_VMU_IMAGE_BYTES),identity[16];char key[KUI_VMU_GAME_CARD_KEY_BYTES];
    struct kui_vmu_image_summary summary;
    if(!image) return IO;
    int result=read_admitted(path,image,identity,key,&summary,NULL,log,cancel);free(image);
    if(result) return result;
    if(memcmp(expected_identity,identity,16u)) return BAD;
    uint64_t sequence;int slot;char old_name[49];
    result=name_load(path,identity,old_name,&sequence,&slot,cancel);
    if(result) return result;
    if(sequence==UINT64_MAX) return BAD;
    if(slot>=0 && !strcmp(name,old_name)) return OK;
    if(stopped(cancel)) return STOP;
    uint8_t wire[NAME_BYTES]={0};memcpy(wire,"KUIVMUN1",8u);put32(wire+8,1u);put32(wire+12,NAME_BYTES);
    put64(wire+16,sequence+1u);memcpy(wire+24,identity,16u);memcpy(wire+40,name,strlen(name));
    put32(wire+124,kui_crc32(0,wire,124u));
    char target[192],leaf_name[16];snprintf(leaf_name,sizeof(leaf_name),"name-%c.bin",slot==0?'b':'a');
    if(!leaf(target,path,leaf_name)) return ARG;
    /* Admission and the retained alternate precede the sole metadata write. */
    FILINFO info;FRESULT r=f_stat(target,&info);
    if(r!=FR_NO_FILE && r!=FR_NO_PATH && (r!=FR_OK || (info.fattrib&AM_DIR))) return IO;
    result=kui_vmu_library_check_allocation(cancel);if(result) return result;
    FIL file;UINT done=0;
    if(f_open(&file,target,FA_CREATE_ALWAYS|FA_WRITE)!=FR_OK) return IO;
    bool ok=f_write(&file,wire,sizeof(wire),&done)==FR_OK && done==sizeof(wire);
    ok=f_sync(&file)==FR_OK && ok;ok=f_close(&file)==FR_OK && ok;
    if(!ok) return IO;
    uint8_t check[NAME_BYTES];result=read_exact(target,check,sizeof(check),NULL);
    if(result || memcmp(wire,check,sizeof(wire))) return result?result:IO;
    char identity_path[192];if(!leaf(identity_path,path,"identity.bin")) return ARG;
    result=read_exact(identity_path,check,16u,NULL);
    return result?result:memcmp(identity,check,16u)?BAD:OK;
}
static int assignments_load(struct kui_vmu_game_assign_table *table,kui_cancel_fn cancel) {
    uint8_t *wire=malloc(KUI_VMU_GAME_ASSIGN_WIRE_BYTES);
    struct kui_vmu_game_assign_table *candidate=malloc(sizeof(*candidate));
    if(!wire || !candidate) {free(wire);free(candidate);return IO;}
    bool any=false,valid=false;int result=OK;memset(table,0,sizeof(*table));
    for(unsigned i=0;!result && i<2u;i++) {
        char path[64];snprintf(path,sizeof(path),ACTIVE_ROOT "/assign-%c.bin",'a'+i);
        bool present=false;int r=optional_exact(path,wire,KUI_VMU_GAME_ASSIGN_WIRE_BYTES,&present,cancel);
        any|=present;
        if(r==IO || r==STOP) {result=r;break;}
        if(r || !present || !kui_vmu_game_assign_decode(wire,KUI_VMU_GAME_ASSIGN_WIRE_BYTES,candidate)) continue;
        if(valid && candidate->sequence==table->sequence && memcmp(candidate,table,sizeof(*table))) {result=BAD;break;}
        if(!valid || candidate->sequence>table->sequence) {*table=*candidate;valid=true;}
    }
    free(wire);free(candidate);return result?result:any && !valid?BAD:OK;
}
static int directory_ids(const char *root,char kind,uint8_t bitmap[CATALOG_BITMAP_BYTES],unsigned *count,kui_cancel_fn cancel) {
    DIR dir;FILINFO info;memset(bitmap,0,CATALOG_BITMAP_BYTES);*count=0;
    FRESULT r=f_opendir(&dir,root);
    if(r==FR_NO_FILE || r==FR_NO_PATH) return OK;
    if(r!=FR_OK) return IO;
    int result=OK;
    while((r=f_readdir(&dir,&info))==FR_OK && info.fname[0]) {
        if(stopped(cancel)) {result=STOP;break;}
        if(!(info.fattrib&AM_DIR) || strlen(info.fname)!=5u || info.fname[0]!=kind) continue;
        unsigned id=0;bool canonical=true;
        for(unsigned i=1;i<5u;i++) {if(info.fname[i]<'0' || info.fname[i]>'9') {canonical=false;break;}id=id*10u+(unsigned)(info.fname[i]-'0');}
        if(canonical && id && !(bitmap[id/8u]&(1u<<(id%8u)))) {bitmap[id/8u]|=(uint8_t)(1u<<(id%8u));++*count;}
    }
    if(r!=FR_OK || f_closedir(&dir)!=FR_OK) return IO;
    return result;
}
static void text_append(char *games,size_t capacity,const char *key) {
    size_t n=strlen(games),extra=strlen(key)+(n?2u:0u);
    if(!key[0]) return;
    if(n>=3u && !strcmp(games+n-3u,"...")) return;
    if(n+extra>=capacity) {
        if(n+5u<capacity) strcat(games,", ...");
        else if(n>=3u) memcpy(games+n-3u,"...",4u);
        return;
    }
    if(n) strcat(games,", ");
    strcat(games,key);
}
static void game_append(char games[193],const char *key) {text_append(games,193u,key);}

/* Optional title metadata is read once per page request, on the storage
 * worker. No image inspection, cover pixels or filesystem access is added to
 * selection changes. The table only holds keys relevant to this page. */
#define TITLE_KEYS (KUI_VMU_GAME_ASSIGN_MAX+KUI_VMU_ROWS)
struct library_title {char key[KUI_VMU_GAME_CARD_KEY_BYTES],title[129];bool conflict;};
struct library_titles {struct library_title entries[TITLE_KEYS];unsigned count;};
static struct library_title *title_find(struct library_titles *titles,const char *key) {
    for(unsigned i=0;i<titles->count;i++) if(!strcmp(titles->entries[i].key,key)) return titles->entries+i;
    return NULL;
}
static void title_want(struct library_titles *titles,const char *key) {
    if(!key[0] || title_find(titles,key) || titles->count==TITLE_KEYS) return;
    struct library_title *entry=titles->entries+titles->count++;
    memcpy(entry->key,key,sizeof(entry->key));
}
static int titles_load(struct library_titles *titles,kui_cancel_fn cancel) {
    if(!titles->count) return stopped(cancel)?STOP:OK;
    DIR dir;FILINFO info;FRESULT r=f_opendir(&dir,"0:/KUI/covers");
    if(r!=FR_OK) return stopped(cancel)?STOP:OK; /* Optional cache. */
    unsigned visited=0;int result=OK;
    while((r=f_readdir(&dir,&info))==FR_OK && info.fname[0] && visited++<TITLE_SCAN_LIMIT) {
        if(stopped(cancel)) {result=STOP;break;}
        size_t n=strlen(info.fname);
        if((info.fattrib&AM_DIR) || n<5u || info.fname[n-4u]!='.' ||
           (info.fname[n-3u]|32)!='k' || (info.fname[n-2u]|32)!='c' || (info.fname[n-1u]|32)!='v') continue;
        char path[288];int length=snprintf(path,sizeof(path),"0:/KUI/covers/%s",info.fname);
        if(length<0 || (size_t)length>=sizeof(path)) continue;
        FIL file;UINT got=0;uint8_t header[512];struct kui_cover_record record;
        if(f_open(&file,path,FA_READ)!=FR_OK) continue;
        bool valid=f_size(&file)>=sizeof(header) && f_read(&file,header,sizeof(header),&got)==FR_OK && got==sizeof(header);
        if(f_close(&file)!=FR_OK) valid=false;
        char key[KUI_VMU_GAME_CARD_KEY_BYTES];
        if(!valid || !kui_cover_header_decode(&record,header) || !kui_vmu_game_card_key(record.product,key)) continue;
        struct library_title *entry=title_find(titles,key);
        if(!entry || entry->conflict) continue;
        char title[129];kui_cover_display_title(record.title,"",title);
        if(!title[0]) continue;
        /* A title belongs to a checked disc header, not a similarly named
         * cover file. Ignore metadata whose source selector has changed. */
        length=snprintf(path,sizeof(path),"0:%s",record.gdi_path);
        if(length<0 || (size_t)length>=sizeof(path) || f_stat(path,&info)!=FR_OK ||
           (info.fattrib&AM_DIR) || info.fsize!=record.gdi.bytes ||
           info.fdate!=record.gdi.date || info.ftime!=record.gdi.time) continue;
        if(entry->title[0] && strcmp(entry->title,title)) {entry->title[0]=0;entry->conflict=true;}
        else memcpy(entry->title,title,sizeof(entry->title));
    }
    (void)f_closedir(&dir);return result;
}
static unsigned get16(const uint8_t *p) {return p[0]|(unsigned)p[1]<<8;}
static void save_contents(const uint8_t *image,char out[97]) {
    char descriptions[3][33]={{0}};unsigned count=0;
    const uint8_t *fat=image+254u*512u;out[0]=0;
    for(unsigned directory=253u;directory>=241u;directory--) for(unsigned slot=0;slot<16u;slot++) {
        const uint8_t *entry=image+directory*512u+slot*32u;
        if(entry[0]!=0x33u) continue;
        unsigned blocks=get16(entry+24u),offset=get16(entry+26u),block=get16(entry+2u);
        /* The full image was already admitted. Keep independent bounds here
         * because display parsing must never turn a bad offset into a read. */
        if(!blocks || blocks>200u || offset>=blocks) continue;
        for(unsigned at=0;at<offset && block<200u;at++) block=get16(fat+block*2u);
        if(block>=200u) continue;
        struct kui_vmu_description description;
        if(!kui_vmu_description_parse(image+block*512u,512u,(size_t)(blocks-offset)*512u,&description) ||
           !description.description[0]) continue;
        unsigned found=0;while(found<count && strcmp(descriptions[found],description.description)) ++found;
        if(found<count) continue;
        if(count==3u) {text_append(out,97u,"...");return;}
        memcpy(descriptions[count++],description.description,33u);
        text_append(out,97u,description.description);
    }
}
static void row_game(struct kui_vmu_backup_entry *row,const char *key,struct library_titles *titles,char first[129]) {
    struct library_title *title=title_find(titles,key);
    if(title && title->title[0]) {
        game_append(row->titles,title->title);
        if(!first[0]) memcpy(first,title->title,129u);
    } else {
        char unknown[48];snprintf(unknown,sizeof(unknown),"Unknown game (%s)",key);game_append(row->titles,unknown);
    }
}
int kui_vmu_library_list(unsigned page,struct kui_vmu_backup_view *out,kui_log_fn log,kui_cancel_fn cancel) {
    if(!out) return ARG;
    memset(out,0,sizeof(*out));out->page=page;
    uint8_t *image=malloc(KUI_VMU_IMAGE_BYTES),(*ids)[CATALOG_BITMAP_BYTES]=calloc(2u,CATALOG_BITMAP_BYTES);
    struct kui_vmu_game_assign_table *assignments=malloc(sizeof(*assignments));
    struct library_titles *titles=calloc(1u,sizeof(*titles));
    char keys[KUI_VMU_ROWS][KUI_VMU_GAME_CARD_KEY_BYTES]={{0}};
    unsigned file_counts[KUI_VMU_ROWS]={0};
    int result=OK;unsigned counts[2]={0};
    if(!image || !ids || !assignments || !titles) {result=IO;goto done;}
    if((result=assignments_load(assignments,cancel))) {
        snprintf(out->status.message,sizeof(out->status.message),"Cannot read game assignments; restore their backup");goto done;
    }
    result=directory_ids(ACTIVE_ROOT,'p',ids[0],&counts[0],cancel);
    if(!result) result=directory_ids(SNAPSHOT_ROOT,'v',ids[1],&counts[1],cancel);
    if(result) goto done;
    out->total=counts[0]+counts[1];
    if(page>UINT_MAX/KUI_VMU_ROWS || (out->total && page*KUI_VMU_ROWS>=out->total)) {result=ARG;goto done;}
    unsigned start=page*KUI_VMU_ROWS,index=0,bad=0;
    for(unsigned kind=0;!result && kind<2u;kind++) for(unsigned id=1;!result && id<=9999u;id++) {
        if(!(ids[kind][id/8u]&(1u<<(id%8u)))) continue;
        if(index++<start || out->count>=KUI_VMU_ROWS) continue;
        unsigned row_index=out->count++;struct kui_vmu_backup_entry *row=&out->entries[row_index];
        row->active=kind==0u;row->free_blocks=UINT_MAX;
        snprintf(row->folder,sizeof(row->folder),"%c%04u",kind?'v':'p',id);
        snprintf(row->path,sizeof(row->path),"%s/%c%04u/card.vmd",kind?SNAPSHOT_ROOT:ACTIVE_ROOT,kind?'v':'p',id);
        char key[KUI_VMU_GAME_CARD_KEY_BYTES],legacy_label[16]={0};struct kui_vmu_image_summary summary;
        if(log) log("Virtual VMUs: reading %s",row->folder);
        int r=read_admitted(row->path,image,row->identity,key,&summary,legacy_label,log,cancel);
        if(r==IO || r==STOP) {result=r;break;}
        if(r) {
            ++bad;snprintf(row->label,sizeof(row->label),"Unavailable card %c%04u",kind?'v':'p',id);
            snprintf(row->games,sizeof(row->games),"Incomplete or damaged; inspect Details");
            snprintf(row->name,sizeof(row->name),"Unavailable");continue;
        }
        row->readable=true;row->free_blocks=summary.free_blocks;row->bytes=summary.used_blocks*512u;
        file_counts[row_index]=summary.files;save_contents(image,row->contents);
        memcpy(keys[row_index],key,sizeof(key));
        if(kind==0u) {
            for(unsigned a=0;a<assignments->count;a++) {
                const struct kui_vmu_game_assignment *entry=&assignments->entries[a];
                if(entry->pair==id && !memcmp(entry->identity,row->identity,16u)) {++row->users;game_append(row->games,entry->key);title_want(titles,entry->key);}
            }
            if(key[0]) {
                const struct kui_vmu_game_assignment *override=kui_vmu_game_assign_find(assignments,key);
                if(!override) {++row->users;game_append(row->games,key);title_want(titles,key);}
            }
        }
        uint64_t sequence;int slot;r=name_load(row->path,row->identity,row->label,&sequence,&slot,cancel);
        if(r==IO || r==STOP) {result=r;break;}
        row->custom_label=!r && row->label[0];
        if(!row->custom_label) row->label[0]=0;
        if(kind) {
            /* Older snapshot callers supplied source labels before friendly
             * rename records existed. Retain those names, while replacing
             * the format's generic default with the save descriptions. */
            if(!row->custom_label && legacy_label[0] && strcmp(legacy_label,"Virtual card")) {
                snprintf(row->label,sizeof(row->label),"%s",legacy_label);row->custom_label=true;
            }
            snprintf(row->games,sizeof(row->games),"Stored snapshot; use Import saves to activate");
        } else if(!row->games[0]) snprintf(row->games,sizeof(row->games),"No game assignments");
    }
    if(!result) result=titles_load(titles,cancel);
    for(unsigned i=0;!result && i<out->count;i++) {
        struct kui_vmu_backup_entry *row=&out->entries[i];if(!row->readable) continue;
        char first[129]={0};unsigned id=path_pair(row->path);
        if(row->active) {
            for(unsigned a=0;a<assignments->count;a++) {
                const struct kui_vmu_game_assignment *entry=assignments->entries+a;
                if(entry->pair==id && !memcmp(entry->identity,row->identity,16u)) row_game(row,entry->key,titles,first);
            }
            if(keys[i][0] && !kui_vmu_game_assign_find(assignments,keys[i])) row_game(row,keys[i],titles,first);
        }
        if(!row->custom_label) {
            const char *context=first[0]?first:row->contents;
            if(context[0]) {
                const char *comma=first[0]?NULL:strstr(context,", ");size_t length=comma?(size_t)(comma-context):strlen(context);
                char shared[24]={0};
                if(first[0] && row->users>1u) snprintf(shared,sizeof(shared)," +%u game%s",row->users-1u,row->users==2u?"":"s");
                size_t suffix=strlen(shared);
                if(length+suffix>=sizeof(row->label)) length=sizeof(row->label)-suffix-1u;
                if(length>=sizeof(row->label)) length=sizeof(row->label)-1u;
                memcpy(row->label,context,length);row->label[length]=0;
                if(suffix) strcat(row->label,shared);
            } else snprintf(row->label,sizeof(row->label),"%s",!file_counts[i]?
                (row->users?"Empty game card":row->active?"Empty virtual VMU":"Empty backup"):
                row->users>1u?"Shared virtual VMU":row->users?"Game saves":row->active?"Saved games":"Save backup");
        }
        size_t label_bytes=strlen(row->label);
        if(label_bytes>=sizeof(row->name)) label_bytes=sizeof(row->name)-1u;
        memcpy(row->name,row->label,label_bytes);row->name[label_bytes]=0;
    }
    if(!result) {
        if(bad) snprintf(out->status.message,sizeof(out->status.message),"%u cards; %u unavailable on this page",out->total,bad);
        else snprintf(out->status.message,sizeof(out->status.message),"%u cards; active game cards and stored snapshots",out->total);
    }
done:
    free(image);free(ids);free(assignments);free(titles);
    out->status.complete=true;out->status.passed=result==OK;out->status.stopped=result==STOP;
    if(result && !out->status.message[0]) snprintf(out->status.message,sizeof(out->status.message),"Virtual-card listing %s",result==STOP?"stopped":"could not be verified");
    if(result && result!=STOP) ++out->status.errors;
    return result;
}
