/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_match_memory.h"
#include "kui/hash.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

#define RECORD_BYTES 208u
#define RECORD_CRC 204u
#define RECORD_PRODUCT 44u
#define RECORD_TITLE 56u
#define RECORD_FORGET 185u
#define PATH_BYTES 96u
struct record {
    uint8_t wire[RECORD_BYTES];
    uint32_t sequence;
    struct kui_vmu_match_memory_target target;
    bool forgotten;
};

static size_t length(const char *s,size_t cap) {
    size_t n=0;while(n<cap && s[n]) ++n;return n;
}
static bool text_valid(const char *s,size_t cap,bool ascii,bool nonempty) {
    size_t n=length(s,cap);bool visible=false;
    if(n==cap) return false;
    for(size_t i=0;i<n;i++) {
        unsigned c=(unsigned char)s[i];
        if(c<32u || c==127u || (ascii && c>126u)) return false;
        if(c!=' ') visible=true;
    }
    return !nonempty || visible;
}
static int entry_compare(const struct kui_vmu_import_entry *a,
    const struct kui_vmu_import_entry *b) {
    int order=strcmp(a->name,b->name);if(order) return order;
    order=strcmp(a->application,b->application);if(order) return order;
    order=strcmp(a->short_description,b->short_description);if(order) return order;
    order=strcmp(a->long_description,b->long_description);if(order) return order;
    if(a->type!=b->type) return a->type<b->type?-1:1;
    if(a->blocks!=b->blocks) return a->blocks<b->blocks?-1:1;
    if(a->header_offset!=b->header_offset) return a->header_offset<b->header_offset?-1:1;
    return 0;
}
static void hash_text(struct kui_sha256 *hash,const char *text) {
    uint8_t n=(uint8_t)strlen(text);kui_sha256_update(hash,&n,1u);
    kui_sha256_update(hash,text,n);
}
int kui_vmu_match_memory_key(const struct kui_vmu_import_plan *plan,
    const struct kui_vmu_import_match_group *group,uint8_t key[32]) {
    if(!plan || !group || !key || !plan->entry_count ||
       plan->entry_count>KUI_VMU_IMAGE_DIRECTORY_ENTRIES) return KUI_VMU_MATCH_MEMORY_INVALID;
    uint16_t members[KUI_VMU_IMAGE_DIRECTORY_ENTRIES];unsigned count=0,blocks=0;
    for(unsigned i=0;i<KUI_VMU_IMAGE_DIRECTORY_ENTRIES;i++) {
        if(!(group->members[i/8u]&(1u<<(i%8u)))) continue;
        if(i>=plan->entry_count) return KUI_VMU_MATCH_MEMORY_INVALID;
        const struct kui_vmu_import_entry *e=&plan->entries[i];
        if((e->type!=0x33u && e->type!=0xccu) || !e->blocks ||
           e->blocks>KUI_VMU_IMAGE_USER_BLOCKS || e->header_offset>=e->blocks ||
           !text_valid(e->name,sizeof(e->name),true,false) ||
           !text_valid(e->application,sizeof(e->application),true,false) ||
           !text_valid(e->short_description,sizeof(e->short_description),true,false) ||
           !text_valid(e->long_description,sizeof(e->long_description),true,false))
            return KUI_VMU_MATCH_MEMORY_INVALID;
        unsigned at=count;
        while(at && entry_compare(e,&plan->entries[members[at-1u]])<0) {
            members[at]=members[at-1u];--at;
        }
        members[at]=(uint16_t)i;++count;blocks+=e->blocks;
    }
    if(!count || count!=group->files || blocks!=group->blocks ||
       blocks>KUI_VMU_IMAGE_USER_BLOCKS) return KUI_VMU_MATCH_MEMORY_INVALID;
    struct kui_sha256 hash;kui_sha256_init(&hash);
    static const char domain[]="KUI VMU confirmed metadata group v1";
    kui_sha256_update(&hash,domain,sizeof(domain));
    uint8_t n[2]={(uint8_t)count,(uint8_t)(count>>8)};
    kui_sha256_update(&hash,n,sizeof(n));
    for(unsigned i=0;i<count;i++) {
        const struct kui_vmu_import_entry *e=&plan->entries[members[i]];
        uint8_t numbers[5]={e->type,(uint8_t)e->blocks,(uint8_t)(e->blocks>>8),
            (uint8_t)e->header_offset,(uint8_t)(e->header_offset>>8)};
        kui_sha256_update(&hash,numbers,sizeof(numbers));
        hash_text(&hash,e->name);hash_text(&hash,e->application);
        hash_text(&hash,e->short_description);hash_text(&hash,e->long_description);
    }
    uint8_t digest[32];kui_sha256_digest(&hash,digest);memcpy(key,digest,sizeof(digest));
    return KUI_VMU_MATCH_MEMORY_OK;
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(n>>(8u*i));
}
static bool target_valid(const struct kui_vmu_match_memory_target *target) {
    return target && text_valid(target->product,sizeof(target->product),true,true) &&
        text_valid(target->title,sizeof(target->title),false,true);
}
static bool zero_tail(const uint8_t *data,size_t bytes) {
    size_t n=length((const char *)data,bytes);
    for(size_t i=n;i<bytes;i++) if(data[i]) return false;
    return n<bytes;
}
static bool decode(struct record *record,const uint8_t key[32]) {
    const uint8_t *wire=record->wire;
    if(memcmp(wire,"KUIVMM01",8u) || !get32(wire+8u) ||
       memcmp(wire+12u,key,32u) || wire[RECORD_FORGET]>1u ||
       get32(wire+RECORD_CRC)!=kui_crc32(0,wire,RECORD_CRC)) return false;
    for(unsigned i=RECORD_FORGET+1u;i<RECORD_CRC;i++) if(wire[i]) return false;
    if(!zero_tail(wire+RECORD_PRODUCT,sizeof(record->target.product)) ||
       !zero_tail(wire+RECORD_TITLE,sizeof(record->target.title))) return false;
    record->sequence=get32(wire+8u);record->forgotten=wire[RECORD_FORGET]!=0;
    memcpy(record->target.product,wire+RECORD_PRODUCT,sizeof(record->target.product));
    memcpy(record->target.title,wire+RECORD_TITLE,sizeof(record->target.title));
    if(record->forgotten) return !record->target.product[0] && !record->target.title[0];
    return target_valid(&record->target);
}
static void encode(struct record *record,const uint8_t key[32],uint32_t sequence,
    const struct kui_vmu_match_memory_target *target) {
    memset(record,0,sizeof(*record));record->sequence=sequence;record->forgotten=!target;
    memcpy(record->wire,"KUIVMM01",8u);put32(record->wire+8u,sequence);
    memcpy(record->wire+12u,key,32u);
    if(target) {
        strcpy(record->target.product,target->product);strcpy(record->target.title,target->title);
        memcpy(record->wire+RECORD_PRODUCT,record->target.product,sizeof(record->target.product));
        memcpy(record->wire+RECORD_TITLE,record->target.title,sizeof(record->target.title));
    }
    record->wire[RECORD_FORGET]=(uint8_t)record->forgotten;
    put32(record->wire+RECORD_CRC,kui_crc32(0,record->wire,RECORD_CRC));
}
static void path_for(const uint8_t key[32],const char *suffix,char path[PATH_BYTES]) {
    char hex[65];kui_hex(key,32u,hex);
    (void)snprintf(path,PATH_BYTES,"%s/%s%s",KUI_VMU_MATCH_MEMORY_ROOT,hex,suffix);
}
static int read_record(const char *path,const uint8_t key[32],struct record *record) {
    FILINFO info;FRESULT result=f_stat(path,&info);
    if(result==FR_NO_FILE || result==FR_NO_PATH) return KUI_VMU_MATCH_MEMORY_MISSING;
    if(result!=FR_OK) return KUI_VMU_MATCH_MEMORY_IO;
    if((info.fattrib&AM_DIR) || info.fsize!=RECORD_BYTES) return KUI_VMU_MATCH_MEMORY_CORRUPT;
    FIL file;result=f_open(&file,path,FA_READ);
    if(result!=FR_OK) return KUI_VMU_MATCH_MEMORY_IO;
    int status=KUI_VMU_MATCH_MEMORY_CORRUPT;
    if(f_size(&file)==RECORD_BYTES) {
        UINT got=0;
        result=f_read(&file,record->wire,RECORD_BYTES,&got);
        if(result!=FR_OK) status=KUI_VMU_MATCH_MEMORY_IO;
        else if(got==RECORD_BYTES && decode(record,key)) status=KUI_VMU_MATCH_MEMORY_OK;
    }
    if(f_close(&file)!=FR_OK) status=KUI_VMU_MATCH_MEMORY_IO;
    return status;
}
static int read_pair(const uint8_t key[32],struct record records[2],int present[2],unsigned *latest) {
    for(unsigned i=0;i<2u;i++) {
        char path[PATH_BYTES];path_for(key,i?"-b.bin":"-a.bin",path);
        present[i]=read_record(path,key,&records[i]);
        if(present[i]!=KUI_VMU_MATCH_MEMORY_OK && present[i]!=KUI_VMU_MATCH_MEMORY_MISSING)
            return present[i];
    }
    if(present[0]==KUI_VMU_MATCH_MEMORY_MISSING && present[1]==KUI_VMU_MATCH_MEMORY_MISSING)
        return KUI_VMU_MATCH_MEMORY_MISSING;
    *latest=present[0]!=KUI_VMU_MATCH_MEMORY_OK?1u:
        present[1]!=KUI_VMU_MATCH_MEMORY_OK?0u:records[1].sequence>records[0].sequence?1u:0u;
    if(present[0]==KUI_VMU_MATCH_MEMORY_OK && present[1]==KUI_VMU_MATCH_MEMORY_OK &&
       records[0].sequence==records[1].sequence &&
       memcmp(records[0].wire,records[1].wire,RECORD_BYTES)) return KUI_VMU_MATCH_MEMORY_CORRUPT;
    return KUI_VMU_MATCH_MEMORY_OK;
}
int kui_vmu_match_memory_load(const struct kui_vmu_import_plan *plan,
    const struct kui_vmu_import_match_group *group,struct kui_vmu_match_memory_target *target) {
    if(!target) return KUI_VMU_MATCH_MEMORY_INVALID;
    uint8_t key[32];int status=kui_vmu_match_memory_key(plan,group,key);
    memset(target,0,sizeof(*target));if(status) return status;
    struct record records[2];int present[2];unsigned latest=0;
    status=read_pair(key,records,present,&latest);if(status) return status;
    if(records[latest].forgotten) return KUI_VMU_MATCH_MEMORY_MISSING;
    *target=records[latest].target;return KUI_VMU_MATCH_MEMORY_OK;
}
static int directory(const char *path) {
    FRESULT result=f_mkdir(path);if(result==FR_OK) return KUI_VMU_MATCH_MEMORY_OK;
    if(result!=FR_EXIST) return KUI_VMU_MATCH_MEMORY_IO;
    FILINFO info;
    return f_stat(path,&info)==FR_OK && (info.fattrib&AM_DIR)?
        KUI_VMU_MATCH_MEMORY_OK:KUI_VMU_MATCH_MEMORY_IO;
}
static int publish(const uint8_t key[32],const struct record *record,unsigned slot) {
    if(directory("0:/KUI") || directory("0:/KUI/vmu") || directory(KUI_VMU_MATCH_MEMORY_ROOT))
        return KUI_VMU_MATCH_MEMORY_IO;
    char part[PATH_BYTES],destination[PATH_BYTES];path_for(key,".part",part);
    path_for(key,slot?"-b.bin":"-a.bin",destination);
    /* Only our unpublished, deterministic scratch filename may be removed.
     * A directory there is not a scratch record and is never unlinked. */
    FILINFO info;FRESULT result=f_stat(part,&info);
    if(result==FR_OK) {
        if((info.fattrib&AM_DIR) || f_unlink(part)!=FR_OK) return KUI_VMU_MATCH_MEMORY_IO;
    } else if(result!=FR_NO_FILE && result!=FR_NO_PATH) return KUI_VMU_MATCH_MEMORY_IO;
    FIL file;result=f_open(&file,part,FA_WRITE|FA_CREATE_NEW);
    if(result!=FR_OK) return KUI_VMU_MATCH_MEMORY_IO;
    UINT written=0;bool ok=f_write(&file,record->wire,RECORD_BYTES,&written)==FR_OK && written==RECORD_BYTES;
    if(ok && f_sync(&file)!=FR_OK) ok=false;
    if(f_close(&file)!=FR_OK) ok=false;
    if(!ok) return KUI_VMU_MATCH_MEMORY_IO;
    struct record verified;int status=read_record(part,key,&verified);
    if(status || memcmp(record->wire,verified.wire,RECORD_BYTES))
        return status?status:KUI_VMU_MATCH_MEMORY_CORRUPT;
    result=f_stat(destination,&info);
    if(result==FR_OK) {
        if((info.fattrib&AM_DIR) || f_unlink(destination)!=FR_OK) return KUI_VMU_MATCH_MEMORY_IO;
    } else if(result!=FR_NO_FILE && result!=FR_NO_PATH) return KUI_VMU_MATCH_MEMORY_IO;
    if(f_rename(part,destination)!=FR_OK) return KUI_VMU_MATCH_MEMORY_IO;
    status=read_record(destination,key,&verified);
    if(status || memcmp(record->wire,verified.wire,RECORD_BYTES))
        return status?status:KUI_VMU_MATCH_MEMORY_CORRUPT;
    return KUI_VMU_MATCH_MEMORY_OK;
}
static int remember(const struct kui_vmu_import_plan *plan,
    const struct kui_vmu_import_match_group *group,const struct kui_vmu_match_memory_target *target) {
    uint8_t key[32];int status=kui_vmu_match_memory_key(plan,group,key);if(status) return status;
    struct record records[2];int present[2];unsigned latest=0;
    status=read_pair(key,records,present,&latest);
    if(status!=KUI_VMU_MATCH_MEMORY_OK && status!=KUI_VMU_MATCH_MEMORY_MISSING) return status;
    if(status==KUI_VMU_MATCH_MEMORY_OK) {
        const struct record *old=&records[latest];
        if(!target && old->forgotten) return KUI_VMU_MATCH_MEMORY_OK;
        if(target && !old->forgotten && !strcmp(target->product,old->target.product) &&
           !strcmp(target->title,old->target.title)) return KUI_VMU_MATCH_MEMORY_OK;
        if(old->sequence==UINT32_MAX) return KUI_VMU_MATCH_MEMORY_INVALID;
    } else if(!target) return KUI_VMU_MATCH_MEMORY_OK;
    uint32_t sequence=status==KUI_VMU_MATCH_MEMORY_OK?records[latest].sequence+1u:1u;
    unsigned slot=status==KUI_VMU_MATCH_MEMORY_OK?1u-latest:0u;
    struct record next;encode(&next,key,sequence,target);return publish(key,&next,slot);
}
int kui_vmu_match_memory_remember(const struct kui_vmu_import_plan *plan,
    const struct kui_vmu_import_match_group *group,const struct kui_vmu_match_memory_target *target) {
    if(!target_valid(target)) return KUI_VMU_MATCH_MEMORY_INVALID;
    return remember(plan,group,target);
}
int kui_vmu_match_memory_forget(const struct kui_vmu_import_plan *plan,
    const struct kui_vmu_import_match_group *group) {
    return remember(plan,group,NULL);
}
