/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_game_candidates.h"
#include "kui/vmu_library.h"
#include "kui/retail_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_FLAG 1u
struct candidate_record {
    struct kui_vmu_game_assignment target;
    uint32_t flags;
};
struct candidate_table {
    uint64_t sequence;
    struct kui_vmu_game_candidates_storage storage;
    uint32_t count;
    struct candidate_record entries[KUI_VMU_GAME_CANDIDATES_MAX];
};
struct candidate_work {
    struct candidate_table table,other;
    struct kui_vmu_game_assign_table legacy,legacy_other;
    uint8_t wire[KUI_VMU_GAME_CANDIDATES_WIRE_BYTES];
    bool legacy_loaded;
    int active;
};
static const char *const candidate_paths[2]={KUI_VMU_GAME_CANDIDATES_PATH_A,KUI_VMU_GAME_CANDIDATES_PATH_B};
static const char *const legacy_paths[2]={"0:/KUI/vmu-test/assign-a.bin","0:/KUI/vmu-test/assign-b.bin"};
static const uint8_t candidate_magic[8]={'K','U','I','V','M','U','C','2'};
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t get64(const uint8_t *p) {return get32(p)|((uint64_t)get32(p+4u)<<32);}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(n>>(8u*i));}
static void put64(uint8_t *p,uint64_t n) {put32(p,(uint32_t)n);put32(p+4u,(uint32_t)(n>>32));}
static bool zero(const void *data,size_t bytes) {
    const uint8_t *p=data;for(size_t i=0;i<bytes;i++)if(p[i])return false;return true;
}
static bool stopped(kui_cancel_fn cancel) {return cancel && cancel();}
static bool valid_storage(const struct kui_vmu_game_candidates_storage *s) {
    return s && !zero(s->cid,16u) && s->card_sectors && s->high_capacity<=1u &&
        s->partition_first<s->partition_end && s->partition_end<=s->card_sectors &&
        s->partition_end<=UINT64_C(0x100000000);
}
static void storage_copy(struct kui_vmu_game_candidates_storage *out,
    const struct kui_vmu_game_candidates_storage *in) {
    memset(out,0,sizeof(*out));memcpy(out->cid,in->cid,16u);
    out->card_sectors=in->card_sectors;out->partition_first=in->partition_first;
    out->partition_end=in->partition_end;out->high_capacity=in->high_capacity;
}
static bool storage_equal(const struct kui_vmu_game_candidates_storage *a,
    const struct kui_vmu_game_candidates_storage *b) {
    return !memcmp(a->cid,b->cid,16u) && a->card_sectors==b->card_sectors &&
        a->partition_first==b->partition_first && a->partition_end==b->partition_end &&
        a->high_capacity==b->high_capacity;
}
static bool valid_key(const char key[KUI_VMU_GAME_CARD_KEY_BYTES]) {
    char canonical[KUI_VMU_GAME_CARD_KEY_BYTES];
    return key && memchr(key,0,KUI_VMU_GAME_CARD_KEY_BYTES) &&
        kui_vmu_game_card_key(key,canonical) && !memcmp(key,canonical,sizeof(canonical));
}
static bool valid_entry(const struct kui_vmu_game_assignment *e) {
    return e && valid_key(e->key) && e->pair && e->pair<=9999u && !zero(e->identity,16u);
}
/* Canonical sorted records make duplicate/default/per-game limits linear to
 * check, and repeated import batches serialize identically regardless of order. */
static bool valid_table(const struct candidate_table *t) {
    if(!t || !valid_storage(&t->storage) || t->count>KUI_VMU_GAME_CANDIDATES_MAX) return false;
    unsigned products=0,members=0,defaults=0;
    for(uint32_t i=0;i<t->count;i++) {
        const struct candidate_record *e=t->entries+i;
        if(!valid_entry(&e->target) || e->flags>DEFAULT_FLAG) return false;
        int order=i?memcmp(t->entries[i-1u].target.key,e->target.key,KUI_VMU_GAME_CARD_KEY_BYTES):-1;
        if(order>0) return false;
        if(!i || order<0) {++products;members=defaults=0;}
        else if(t->entries[i-1u].target.pair>=e->target.pair) return false;
        if(++members>KUI_VMU_GAME_CANDIDATES_PER_GAME ||
           (defaults+=(e->flags&DEFAULT_FLAG))>1u || products>KUI_VMU_GAME_CANDIDATES_PRODUCTS) return false;
    }
    return true;
}
static bool encode(const struct candidate_table *t,uint8_t *out) {
    memset(out,0,KUI_VMU_GAME_CANDIDATES_WIRE_BYTES);
    if(!valid_table(t) || !t->sequence) return false;
    memcpy(out,candidate_magic,8u);put32(out+8u,2u);put32(out+12u,KUI_VMU_GAME_CANDIDATES_WIRE_BYTES);
    put64(out+16u,t->sequence);put32(out+24u,t->count);put32(out+28u,40u);
    put32(out+32u,512u);put32(out+36u,KUI_VMU_GAME_CANDIDATES_MAX);
    memcpy(out+40u,t->storage.cid,16u);put64(out+56u,t->storage.card_sectors);
    put64(out+64u,t->storage.partition_first);put64(out+72u,t->storage.partition_end);
    put32(out+80u,t->storage.high_capacity);
    for(uint32_t i=0;i<t->count;i++) {
        uint8_t *p=out+512u+(size_t)i*40u;const struct candidate_record *e=t->entries+i;
        memcpy(p,e->target.key,12u);put32(p+12u,e->target.pair);memcpy(p+16u,e->target.identity,16u);
        put32(p+32u,e->flags);
    }
    uint32_t crc=kui_retail_crc32(0,out,508u);
    crc=kui_retail_crc32(crc,out+512u,KUI_VMU_GAME_CANDIDATES_WIRE_BYTES-512u);
    put32(out+508u,crc);return true;
}
static bool decode(const uint8_t *p,size_t bytes,struct candidate_table *t) {
    memset(t,0,sizeof(*t));
    if(!p || bytes!=KUI_VMU_GAME_CANDIDATES_WIRE_BYTES || memcmp(p,candidate_magic,8u) ||
       get32(p+8u)!=2u || get32(p+12u)!=KUI_VMU_GAME_CANDIDATES_WIRE_BYTES || !get64(p+16u) ||
       get32(p+24u)>KUI_VMU_GAME_CANDIDATES_MAX || get32(p+28u)!=40u || get32(p+32u)!=512u ||
       get32(p+36u)!=KUI_VMU_GAME_CANDIDATES_MAX || !zero(p+84u,424u)) return false;
    uint32_t count=get32(p+24u),crc=kui_retail_crc32(0,p,508u);
    crc=kui_retail_crc32(crc,p+512u,KUI_VMU_GAME_CANDIDATES_WIRE_BYTES-512u);
    if(crc!=get32(p+508u) || !zero(p+512u+(size_t)count*40u,
       (KUI_VMU_GAME_CANDIDATES_MAX-count)*40u)) return false;
    t->sequence=get64(p+16u);t->count=count;memcpy(t->storage.cid,p+40u,16u);
    t->storage.card_sectors=get64(p+56u);t->storage.partition_first=get64(p+64u);
    t->storage.partition_end=get64(p+72u);t->storage.high_capacity=get32(p+80u);
    for(uint32_t i=0;i<count;i++) {
        const uint8_t *at=p+512u+(size_t)i*40u;struct candidate_record *e=t->entries+i;
        if(!zero(at+36u,4u)) {memset(t,0,sizeof(*t));return false;}
        memcpy(e->target.key,at,12u);e->target.pair=get32(at+12u);
        memcpy(e->target.identity,at+16u,16u);e->flags=get32(at+32u);
    }
    if(valid_table(t)) return true;
    memset(t,0,sizeof(*t));return false;
}
static int read_record(const char *path,uint8_t *out,size_t bytes,bool *present,bool *sized) {
    FILINFO info;*present=*sized=false;
    FRESULT r=f_stat(path,&info);
    if(r==FR_NO_FILE || r==FR_NO_PATH) return KUI_VMU_GAME_CANDIDATES_OK;
    if(r!=FR_OK) return KUI_VMU_GAME_CANDIDATES_IO;
    *present=true;
    if((info.fattrib&AM_DIR) || info.fsize!=bytes) return KUI_VMU_GAME_CANDIDATES_OK;
    FIL file;UINT got=0;
    if(f_open(&file,path,FA_READ)!=FR_OK) return KUI_VMU_GAME_CANDIDATES_IO;
    *sized=f_size(&file)==bytes;
    bool ok=!*sized || (f_read(&file,out,(UINT)bytes,&got)==FR_OK && got==bytes);
    if(f_close(&file)!=FR_OK) ok=false;
    return ok?KUI_VMU_GAME_CANDIDATES_OK:KUI_VMU_GAME_CANDIDATES_IO;
}
static int load(struct candidate_work *w,const struct kui_vmu_game_candidates_storage *storage,
    kui_cancel_fn cancel) {
    bool any=false,found=false;w->active=-1;memset(&w->table,0,sizeof(w->table));
    for(unsigned slot=0;slot<2u;slot++) {
        if(stopped(cancel)) return KUI_VMU_GAME_CANDIDATES_CANCELLED;
        bool present,sized;int r=read_record(candidate_paths[slot],w->wire,sizeof(w->wire),&present,&sized);
        if(r) return r;
        any|=present;
        if(!sized || !decode(w->wire,sizeof(w->wire),&w->other)) continue;
        if(found && w->other.sequence==w->table.sequence && memcmp(&w->other,&w->table,sizeof(w->table)))
            return KUI_VMU_GAME_CANDIDATES_INVALID;
        if(!found || w->other.sequence>w->table.sequence) {w->table=w->other;w->active=(int)slot;found=true;}
    }
    if(!found) {
        if(any) return KUI_VMU_GAME_CANDIDATES_INVALID;
        if(storage)storage_copy(&w->table.storage,storage);
    } else if(storage && !storage_equal(&w->table.storage,storage)) return KUI_VMU_GAME_CANDIDATES_STALE;
    return KUI_VMU_GAME_CANDIDATES_OK;
}
static int legacy_load(struct candidate_work *w,kui_cancel_fn cancel) {
    if(w->legacy_loaded) return KUI_VMU_GAME_CANDIDATES_OK;
    bool any=false,found=false;memset(&w->legacy,0,sizeof(w->legacy));
    for(unsigned i=0;i<2u;i++) {
        if(stopped(cancel)) return KUI_VMU_GAME_CANDIDATES_CANCELLED;
        bool present,sized;int r=read_record(legacy_paths[i],w->wire,KUI_VMU_GAME_ASSIGN_WIRE_BYTES,&present,&sized);
        if(r) return r;
        any|=present;
        if(!sized || !kui_vmu_game_assign_decode(w->wire,KUI_VMU_GAME_ASSIGN_WIRE_BYTES,&w->legacy_other)) continue;
        if(found && w->legacy_other.sequence==w->legacy.sequence && memcmp(&w->legacy,&w->legacy_other,sizeof(w->legacy)))
            return KUI_VMU_GAME_CANDIDATES_INVALID;
        if(!found || w->legacy_other.sequence>w->legacy.sequence) {w->legacy=w->legacy_other;found=true;}
    }
    if(any && !found) return KUI_VMU_GAME_CANDIDATES_INVALID;
    w->legacy_loaded=true;return KUI_VMU_GAME_CANDIDATES_OK;
}
static unsigned first(const struct candidate_table *t,const char key[12]) {
    unsigned i=0;while(i<t->count && memcmp(t->entries[i].target.key,key,12u)<0) ++i;return i;
}
static unsigned members(const struct candidate_table *t,const char key[12]) {
    unsigned at=first(t,key),n=0;while(at+n<t->count && !memcmp(t->entries[at+n].target.key,key,12u))++n;return n;
}
static unsigned products(const struct candidate_table *t) {
    unsigned n=0;for(unsigned i=0;i<t->count;i++)if(!i || memcmp(t->entries[i-1u].target.key,t->entries[i].target.key,12u))++n;
    return n;
}
static int add(struct candidate_table *t,const struct kui_vmu_game_assignment *e) {
    unsigned at=first(t,e->key),n=members(t,e->key);
    if(!n && products(t)==KUI_VMU_GAME_CANDIDATES_PRODUCTS) return KUI_VMU_GAME_CANDIDATES_FULL;
    while(at<t->count && !memcmp(t->entries[at].target.key,e->key,12u) && t->entries[at].target.pair<e->pair)++at;
    if(at<t->count && !memcmp(t->entries[at].target.key,e->key,12u) && t->entries[at].target.pair==e->pair)
        return memcmp(t->entries[at].target.identity,e->identity,16u)?KUI_VMU_GAME_CANDIDATES_STALE:KUI_VMU_GAME_CANDIDATES_OK;
    if(t->count==KUI_VMU_GAME_CANDIDATES_MAX || n==KUI_VMU_GAME_CANDIDATES_PER_GAME) return KUI_VMU_GAME_CANDIDATES_FULL;
    memmove(t->entries+at+1u,t->entries+at,(t->count-at)*sizeof(t->entries[0]));
    memset(t->entries+at,0,sizeof(t->entries[0]));t->entries[at].target=*e;++t->count;
    return KUI_VMU_GAME_CANDIDATES_OK;
}
static int folder_state(const struct kui_vmu_game_assignment *e,kui_log_fn log) {
    char path[64];FILINFO info;snprintf(path,sizeof(path),"0:/KUI/vmu-test/p%04u",(unsigned)e->pair);
    FRESULT r=f_stat(path,&info);
    if(r==FR_NO_FILE || r==FR_NO_PATH) {
        if(log)log("Virtual VMU choices: legacy %s p%04u folder is absent (result %d)",
            e->key,(unsigned)e->pair,KUI_VMU_GAME_CANDIDATES_MISSING);
        return KUI_VMU_GAME_CANDIDATES_MISSING;
    }
    if(r!=FR_OK)return KUI_VMU_GAME_CANDIDATES_IO;
    return (info.fattrib&AM_DIR)?KUI_VMU_GAME_CANDIDATES_OK:KUI_VMU_GAME_CANDIDATES_STALE;
}
static bool repair_entry(const struct kui_vmu_game_candidates_repair *repair,
    const struct kui_vmu_game_assignment *e) {
    if(repair)for(unsigned i=0;i<repair->count;i++)if(!memcmp(repair->entries+i,e,sizeof(*e)))return true;
    return false;
}
static int migrate(struct candidate_work *w,const char key[12],
    const struct kui_vmu_game_candidates_repair *repair,struct kui_vmu_game_candidates_repair *report,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(members(&w->table,key)) return KUI_VMU_GAME_CANDIDATES_OK;
    int r=legacy_load(w,cancel);if(r)return r;
    const struct kui_vmu_game_assignment *e=kui_vmu_game_assign_find(&w->legacy,key);
    if(!e)return KUI_VMU_GAME_CANDIDATES_OK;
    r=folder_state(e,log);
    if(r==KUI_VMU_GAME_CANDIDATES_MISSING) {
        if(report) {
            if(report->count==KUI_VMU_GAME_CANDIDATES_PRODUCTS)return KUI_VMU_GAME_CANDIDATES_FULL;
            report->entries[report->count++]=*e;return KUI_VMU_GAME_CANDIDATES_OK;
        }
        if(repair_entry(repair,e))return KUI_VMU_GAME_CANDIDATES_OK;
    }
    return r?r:add(&w->table,e);
}
static bool valid_repair(const struct kui_vmu_game_candidates_repair *repair) {
    if(!repair || !valid_storage(&repair->storage) || !repair->legacy_sequence ||
       !repair->count || repair->count>KUI_VMU_GAME_CANDIDATES_PRODUCTS)return false;
    for(unsigned i=0;i<repair->count;i++) {
        if(!valid_entry(repair->entries+i))return false;
        for(unsigned j=0;j<i;j++)if(!memcmp(repair->entries[i].key,repair->entries[j].key,12u))return false;
    }
    return true;
}
static bool repair_keys_selected(const struct kui_vmu_game_candidates_repair *repair,
    const void *entries,size_t count,size_t stride) {
    if(!repair)return true;
    for(unsigned i=0;i<repair->count;i++) {
        bool found=false;
        for(size_t j=0;j<count;j++)if(!memcmp(repair->entries[i].key,(const uint8_t *)entries+j*stride,12u)) {found=true;break;}
        if(!found)return false;
    }
    return true;
}
static int confirm_repair(struct candidate_work *w,const struct kui_vmu_game_candidates_storage *storage,
    const struct kui_vmu_game_candidates_repair *repair,kui_log_fn log,kui_cancel_fn cancel) {
    if(!repair)return KUI_VMU_GAME_CANDIDATES_OK;
    if(!storage_equal(storage,&repair->storage) || w->table.sequence!=repair->catalogue_sequence)
        return KUI_VMU_GAME_CANDIDATES_STALE;
    int r=legacy_load(w,cancel);if(r)return r;
    if(w->legacy.sequence!=repair->legacy_sequence)return KUI_VMU_GAME_CANDIDATES_STALE;
    for(unsigned i=0;i<repair->count;i++) {
        if(stopped(cancel))return KUI_VMU_GAME_CANDIDATES_CANCELLED;
        const struct kui_vmu_game_assignment *expected=repair->entries+i;
        const struct kui_vmu_game_assignment *current=kui_vmu_game_assign_find(&w->legacy,expected->key);
        if(!current || memcmp(current,expected,sizeof(*current)))return KUI_VMU_GAME_CANDIDATES_STALE;
        r=folder_state(current,log);
        if(r!=KUI_VMU_GAME_CANDIDATES_MISSING)return r?r:KUI_VMU_GAME_CANDIDATES_STALE;
    }
    return KUI_VMU_GAME_CANDIDATES_OK;
}
static int required_record(const char *path,void *out,size_t bytes) {
    bool present,sized;int r=read_record(path,out,bytes,&present,&sized);
    return r?r:present && sized?KUI_VMU_GAME_CANDIDATES_OK:KUI_VMU_GAME_CANDIDATES_STALE;
}
static int target_valid(const struct kui_vmu_game_assignment *e,kui_log_fn log,kui_cancel_fn cancel) {
    if(stopped(cancel)) return KUI_VMU_GAME_CANDIDATES_CANCELLED;
    char path[96];uint8_t actual[64],marker[64]={0};
    snprintf(path,sizeof(path),"0:/KUI/vmu-test/p%04u/identity.bin",(unsigned)e->pair);
    int r=required_record(path,actual,16u);
    if(r || memcmp(actual,e->identity,16u)) {
        if(log)log("Virtual VMU choices: %s p%04u identity %s (result %d)",e->key,(unsigned)e->pair,
            r==KUI_VMU_GAME_CANDIDATES_IO?"could not be read":"is missing, malformed or changed",r?r:KUI_VMU_GAME_CANDIDATES_STALE);
        return r?r:KUI_VMU_GAME_CANDIDATES_STALE;
    }
    memcpy(marker,"KUIVMUI1",8u);memcpy(marker+16u,e->identity,16u);
    put32(marker+60u,kui_retail_crc32(0,marker,60u));
    snprintf(path,sizeof(path),"0:/KUI/vmu-test/p%04u/import.bin",(unsigned)e->pair);
    r=required_record(path,actual,64u);
    if(r || memcmp(actual,marker,64u)) {
        if(log)log("Virtual VMU choices: %s p%04u import marker %s (result %d)",e->key,(unsigned)e->pair,
            r==KUI_VMU_GAME_CANDIDATES_IO?"could not be read":"is missing, malformed or changed",r?r:KUI_VMU_GAME_CANDIDATES_STALE);
        return r?r:KUI_VMU_GAME_CANDIDATES_STALE;
    }
    static const char *const names[2]={"card.vmd","card.journal"};
    static const unsigned bytes[2]={131072u,1024u};
    for(unsigned i=0;i<2u;i++) {
        FILINFO info;snprintf(path,sizeof(path),"0:/KUI/vmu-test/p%04u/%s",(unsigned)e->pair,names[i]);
        FRESULT stat=f_stat(path,&info);
        if(stat!=FR_OK || (info.fattrib&AM_DIR) || info.fsize!=bytes[i]) {
            r=stat!=FR_OK && stat!=FR_NO_FILE && stat!=FR_NO_PATH?KUI_VMU_GAME_CANDIDATES_IO:KUI_VMU_GAME_CANDIDATES_STALE;
            if(log)log("Virtual VMU choices: %s p%04u %s %s (result %d)",e->key,(unsigned)e->pair,names[i],
                r==KUI_VMU_GAME_CANDIDATES_IO?"could not be inspected":"is missing or has the wrong type/length",r);
            return r;
        }
    }
    return stopped(cancel)?KUI_VMU_GAME_CANDIDATES_CANCELLED:KUI_VMU_GAME_CANDIDATES_OK;
}
static int all_targets(const struct candidate_table *t,kui_log_fn log,kui_cancel_fn cancel) {
    for(unsigned i=0;i<t->count;i++) {int r=target_valid(&t->entries[i].target,log,cancel);if(r)return r;}
    return KUI_VMU_GAME_CANDIDATES_OK;
}
static bool verify_record(const char *path,const uint8_t *wire) {
    FIL file;uint8_t sector[512];
    if(f_open(&file,path,FA_READ)!=FR_OK)return false;
    bool ok=f_size(&file)==KUI_VMU_GAME_CANDIDATES_WIRE_BYTES;
    for(size_t at=0;ok && at<KUI_VMU_GAME_CANDIDATES_WIRE_BYTES;at+=sizeof(sector)) {
        UINT got=0;ok=f_read(&file,sector,sizeof(sector),&got)==FR_OK && got==sizeof(sector) &&
            !memcmp(sector,wire+at,sizeof(sector));
    }
    return f_close(&file)==FR_OK && ok;
}
static bool write_record(const char *path,const uint8_t *wire,bool create) {
    FIL file;UINT done=0;
    if(f_open(&file,path,FA_WRITE|(create?FA_CREATE_NEW:FA_OPEN_EXISTING))!=FR_OK)return false;
    bool ok=create || f_size(&file)==KUI_VMU_GAME_CANDIDATES_WIRE_BYTES;
    if(ok)ok=f_write(&file,wire,KUI_VMU_GAME_CANDIDATES_WIRE_BYTES,&done)==FR_OK && done==KUI_VMU_GAME_CANDIDATES_WIRE_BYTES;
    if(f_sync(&file)!=FR_OK)ok=false;
    if(f_close(&file)!=FR_OK)ok=false;
    return ok && verify_record(path,wire);
}
/* Namespace changes are confined to bootstrap/repair of a missing or wrongly
 * sized inactive slot. A normal update overwrites a preallocated fixed-size
 * inactive file, without truncation, allocation, rename or deletion. */
static int stage_slot(unsigned target,const uint8_t *wire) {
    FILINFO info;
    char part[64];snprintf(part,sizeof(part),"0:/KUI/vmu-test/candidates-%c.part",'a'+target);
    FRESULT present=f_stat(part,&info);
    if(present==FR_OK && ((info.fattrib&AM_DIR) || f_unlink(part)!=FR_OK)) return KUI_VMU_GAME_CANDIDATES_IO;
    if(present!=FR_OK && present!=FR_NO_FILE) return KUI_VMU_GAME_CANDIDATES_IO;
    if(!write_record(part,wire,true))return KUI_VMU_GAME_CANDIDATES_IO;
    present=f_stat(candidate_paths[target],&info);
    if(present==FR_OK && ((info.fattrib&AM_DIR) || f_unlink(candidate_paths[target])!=FR_OK))
        return KUI_VMU_GAME_CANDIDATES_IO;
    if(present!=FR_OK && present!=FR_NO_FILE) return KUI_VMU_GAME_CANDIDATES_IO;
    if(f_rename(part,candidate_paths[target])!=FR_OK) return KUI_VMU_GAME_CANDIDATES_IO;
    return verify_record(candidate_paths[target],wire)?KUI_VMU_GAME_CANDIDATES_OK:KUI_VMU_GAME_CANDIDATES_IO;
}
static int publish(struct candidate_work *w,kui_log_fn log,kui_cancel_fn cancel) {
    if(w->table.sequence==UINT64_MAX)return KUI_VMU_GAME_CANDIDATES_INVALID;
    if(stopped(cancel))return KUI_VMU_GAME_CANDIDATES_CANCELLED;
    int admitted=kui_vmu_library_check_allocation(cancel);
    if(admitted)return admitted==KUI_VMU_LIBRARY_CANCELLED?
        KUI_VMU_GAME_CANDIDATES_CANCELLED:KUI_VMU_GAME_CANDIDATES_INVALID;
    if(stopped(cancel))return KUI_VMU_GAME_CANDIDATES_CANCELLED;
    /* Bootstrap an empty predecessor before publishing any product choices.
     * A first data-write fault then retains valid legacy fallback. Creation of
     * the filesystem's initial directory entries is not a journaling promise:
     * torn exFAT namespace metadata still fails closed for filesystem repair. */
    if(w->active<0) {
        memset(&w->other,0,sizeof(w->other));storage_copy(&w->other.storage,&w->table.storage);w->other.sequence=1u;
        if(!encode(&w->other,w->wire))return KUI_VMU_GAME_CANDIDATES_INVALID;
        int r=stage_slot(0u,w->wire);if(r)return r;
        w->active=0;w->table.sequence=1u;
    }
    unsigned target=w->active==0?1u:0u;FILINFO info;
    FRESULT present=f_stat(candidate_paths[target],&info);
    if(present==FR_OK && (info.fattrib&AM_DIR))return KUI_VMU_GAME_CANDIDATES_IO;
    if(present!=FR_OK && present!=FR_NO_FILE)return KUI_VMU_GAME_CANDIDATES_IO;
    if(present!=FR_OK || info.fsize!=sizeof(w->wire)) {
        bool exists,sized;int r=read_record(candidate_paths[w->active],w->wire,sizeof(w->wire),&exists,&sized);
        if(r || !exists || !sized || !decode(w->wire,sizeof(w->wire),&w->other) ||
           w->other.sequence!=w->table.sequence || !storage_equal(&w->other.storage,&w->table.storage))
            return KUI_VMU_GAME_CANDIDATES_IO;
        r=stage_slot(target,w->wire);if(r)return r;
    }
    ++w->table.sequence;
    if(!encode(&w->table,w->wire))return KUI_VMU_GAME_CANDIDATES_INVALID;
    if(stopped(cancel))return KUI_VMU_GAME_CANDIDATES_CANCELLED;
    if(!write_record(candidate_paths[target],w->wire,false))return KUI_VMU_GAME_CANDIDATES_IO;
    if(log)log("Virtual VMU choices saved (generation %lu)",(unsigned long)w->table.sequence);
    return KUI_VMU_GAME_CANDIDATES_OK;
}
int kui_vmu_game_candidates_list(const char *product,const struct kui_vmu_game_candidates_storage *storage,
    struct kui_vmu_game_candidates_view *out,kui_log_fn log,kui_cancel_fn cancel) {
    if(!out) return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
    memset(out,0,sizeof(*out));out->default_index=KUI_VMU_GAME_CANDIDATES_NO_DEFAULT;
    char key[12];
    if(!valid_storage(storage) || !kui_vmu_game_card_key(product,key)) return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
    struct candidate_work *w=calloc(1,sizeof(*w));if(!w)return KUI_VMU_GAME_CANDIDATES_IO;
    int r=load(w,storage,cancel);
    if(!r) {
        unsigned at=first(&w->table,key),n=members(&w->table,key);
        for(unsigned i=0;!r && i<n;i++)r=target_valid(&w->table.entries[at+i].target,log,cancel);
        if(!r) {
            memcpy(out->key,key,sizeof(key));storage_copy(&out->storage,storage);out->sequence=w->table.sequence;
            out->count=n;out->present=n!=0;
            for(unsigned i=0;i<n;i++) {
                const struct candidate_record *e=w->table.entries+at+i;
                out->targets[i].pair=e->target.pair;memcpy(out->targets[i].identity,e->target.identity,16u);
                if(e->flags&DEFAULT_FLAG)out->default_index=i;
            }
            if(!n)r=KUI_VMU_GAME_CANDIDATES_ABSENT;
        }
    }
    free(w);return r;
}
int kui_vmu_game_candidates_visit(const struct kui_vmu_game_candidates_storage *storage,
    kui_vmu_game_candidates_visit_fn visit,void *context,kui_log_fn log,kui_cancel_fn cancel) {
    if((storage && !valid_storage(storage)) || !visit)return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
    struct candidate_work *w=calloc(1,sizeof(*w));if(!w)return KUI_VMU_GAME_CANDIDATES_IO;
    int r=load(w,storage,cancel);
    if(!r && w->active<0)r=KUI_VMU_GAME_CANDIDATES_ABSENT;
    if(!r)r=all_targets(&w->table,log,cancel);
    for(unsigned i=0;!r && i<w->table.count;i++) {
        if(stopped(cancel)) {r=KUI_VMU_GAME_CANDIDATES_CANCELLED;break;}
        const struct candidate_record *e=w->table.entries+i;
        struct kui_vmu_game_candidate_target target={.pair=e->target.pair};
        memcpy(target.identity,e->target.identity,16u);
        if(!visit(context,e->target.key,&target,(e->flags&DEFAULT_FLAG)!=0))r=KUI_VMU_GAME_CANDIDATES_CANCELLED;
    }
    if(!r && stopped(cancel))r=KUI_VMU_GAME_CANDIDATES_CANCELLED;
    free(w);return r;
}
int kui_vmu_game_candidates_identity_unpinned(unsigned pair,const uint8_t identity[16],kui_cancel_fn cancel) {
    if(!pair || pair>9999u || !identity || zero(identity,16u))return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
    struct candidate_work *w=calloc(1,sizeof(*w));if(!w)return KUI_VMU_GAME_CANDIDATES_IO;
    int r=KUI_VMU_GAME_CANDIDATES_OK;bool any=false,found=false,referenced=false;
    for(unsigned slot=0;!r && slot<2u;slot++) {
        if(stopped(cancel)) {r=KUI_VMU_GAME_CANDIDATES_CANCELLED;break;}
        bool present,sized;r=read_record(candidate_paths[slot],w->wire,sizeof(w->wire),&present,&sized);
        if(r)break;
        any|=present;
        if(!sized || !decode(w->wire,sizeof(w->wire),&w->other))continue;
        if(found && w->table.sequence==w->other.sequence && memcmp(&w->table,&w->other,sizeof(w->table))) {
            r=KUI_VMU_GAME_CANDIDATES_INVALID;break;
        }
        if(!found)w->table=w->other;
        found=true;
        for(unsigned i=0;i<w->other.count;i++) {
            const struct kui_vmu_game_assignment *e=&w->other.entries[i].target;
            if(e->pair==pair && !memcmp(e->identity,identity,16u))referenced=true;
        }
    }
    if(!r && any && !found)r=KUI_VMU_GAME_CANDIDATES_INVALID;
    if(!r && referenced)r=KUI_VMU_GAME_CANDIDATES_STALE;
    free(w);return r;
}
static int preflight(const struct kui_vmu_game_candidates_plan *plans,size_t count,
    const struct kui_vmu_game_candidates_storage *storage,const struct kui_vmu_game_candidates_repair *repair,
    struct kui_vmu_game_candidates_repair *report,kui_log_fn log,kui_cancel_fn cancel) {
    if(!valid_storage(storage) || count>KUI_VMU_GAME_CANDIDATES_PRODUCTS || (count && !plans)) {
        if(log)log("Virtual VMU choices preflight: invalid storage or plan count (result %d)",KUI_VMU_GAME_CANDIDATES_ARGUMENT);
        return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
    }
    for(size_t i=0;i<count;i++) {
        if(!valid_key(plans[i].key) || plans[i].additional>KUI_VMU_GAME_CANDIDATES_PER_GAME) {
            if(log)log("Virtual VMU choices preflight: invalid plan %u (result %d)",(unsigned)i,KUI_VMU_GAME_CANDIDATES_ARGUMENT);
            return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
        }
        for(size_t j=0;j<i;j++)if(!memcmp(plans[i].key,plans[j].key,12u)) {
            if(log)log("Virtual VMU choices preflight: duplicate product %s (result %d)",plans[i].key,KUI_VMU_GAME_CANDIDATES_ARGUMENT);
            return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
        }
    }
    if(repair && (!valid_repair(repair) || !repair_keys_selected(repair,plans,count,sizeof(*plans))))
        return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
    struct candidate_work *w=calloc(1,sizeof(*w));
    if(!w) {if(log)log("Virtual VMU choices preflight: not enough memory");return KUI_VMU_GAME_CANDIDATES_IO;}
    int r=load(w,storage,cancel);
    if(r && log)log("Virtual VMU choices preflight: catalogue read refused (result %d)",r);
    if(!r)r=confirm_repair(w,storage,repair,log,cancel);
    if(!r && w->table.sequence==UINT64_MAX) {
        if(log)log("Virtual VMU choices preflight: catalogue generation exhausted");
        r=KUI_VMU_GAME_CANDIDATES_INVALID;
    }
    for(size_t i=0;!r && i<count;i++) {
        r=migrate(w,plans[i].key,repair,report,log,cancel);
        if(r && log)log("Virtual VMU choices preflight: legacy product %s refused (result %d)",plans[i].key,r);
    }
    uint32_t total=w->table.count;unsigned games=products(&w->table);
    if(!r && log)log("Virtual VMU choices preflight: %u existing products, %lu candidates; %u planned products",
        games,(unsigned long)total,(unsigned)count);
    for(size_t i=0;!r && i<count;i++) {
        unsigned n=members(&w->table,plans[i].key);
        if(plans[i].additional>KUI_VMU_GAME_CANDIDATES_PER_GAME-n) {
            if(log)log("Virtual VMU choices preflight: %s has %u candidates + %lu planned; per-game limit %u",
                plans[i].key,n,(unsigned long)plans[i].additional,KUI_VMU_GAME_CANDIDATES_PER_GAME);
            r=KUI_VMU_GAME_CANDIDATES_FULL;
        } else if(plans[i].additional>KUI_VMU_GAME_CANDIDATES_MAX-total) {
            if(log)log("Virtual VMU choices preflight: %lu candidates + %lu planned; total limit %u",
                (unsigned long)total,(unsigned long)plans[i].additional,KUI_VMU_GAME_CANDIDATES_MAX);
            r=KUI_VMU_GAME_CANDIDATES_FULL;
        } else if(!n && plans[i].additional && ++games>KUI_VMU_GAME_CANDIDATES_PRODUCTS) {
            if(log)log("Virtual VMU choices preflight: product %s exceeds the %u-product limit",
                plans[i].key,KUI_VMU_GAME_CANDIDATES_PRODUCTS);
            r=KUI_VMU_GAME_CANDIDATES_FULL;
        } else total+=plans[i].additional;
    }
    if(!r)r=all_targets(&w->table,log,cancel);
    if(!r && report && report->count) {
        storage_copy(&report->storage,storage);report->catalogue_sequence=w->table.sequence;
        report->legacy_sequence=w->legacy.sequence;r=KUI_VMU_GAME_CANDIDATES_MISSING;
    }
    if(log)log("Virtual VMU choices preflight: result %d",r);
    free(w);return r;
}
int kui_vmu_game_candidates_preflight(const struct kui_vmu_game_candidates_plan *plans,size_t count,
    const struct kui_vmu_game_candidates_storage *storage,kui_log_fn log,kui_cancel_fn cancel) {
    return preflight(plans,count,storage,NULL,NULL,log,cancel);
}
int kui_vmu_game_candidates_preflight_repair(const struct kui_vmu_game_candidates_plan *plans,size_t count,
    const struct kui_vmu_game_candidates_storage *storage,struct kui_vmu_game_candidates_repair *out,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(!out)return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
    memset(out,0,sizeof(*out));int r=preflight(plans,count,storage,NULL,out,log,cancel);
    if(r!=KUI_VMU_GAME_CANDIDATES_MISSING)memset(out,0,sizeof(*out));
    return r;
}
int kui_vmu_game_candidates_preflight_confirmed(const struct kui_vmu_game_candidates_plan *plans,size_t count,
    const struct kui_vmu_game_candidates_storage *storage,const struct kui_vmu_game_candidates_repair *repair,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(!repair)return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
    return preflight(plans,count,storage,repair,NULL,log,cancel);
}
static int apply(const struct kui_vmu_game_assignment *entries,size_t count,bool select,
    const struct kui_vmu_game_candidates_storage *storage,const struct kui_vmu_game_candidates_repair *repair,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(!valid_storage(storage) || !entries || !count || count>KUI_VMU_GAME_CANDIDATES_MAX)
        return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
    if(select && count>KUI_VMU_GAME_CANDIDATES_PRODUCTS)return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
    for(size_t i=0;i<count;i++) {
        if(!valid_entry(entries+i))return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
        if(select)for(size_t j=0;j<i;j++)if(!memcmp(entries[i].key,entries[j].key,12u))return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
    }
    if(repair && (!valid_repair(repair) || !repair_keys_selected(repair,entries,count,sizeof(*entries))))
        return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
    struct candidate_work *w=calloc(1,sizeof(*w));if(!w)return KUI_VMU_GAME_CANDIDATES_IO;
    int r=load(w,storage,cancel);
    if(!r)r=confirm_repair(w,storage,repair,log,cancel);
    w->other=w->table;
    /* Migrate all affected legacy keys before adding new records. */
    for(size_t i=0;!r && i<count;i++)r=migrate(w,entries[i].key,repair,NULL,log,cancel);
    for(size_t i=0;!r && i<count;i++)r=add(&w->table,entries+i);
    for(size_t i=0;!r && select && i<count;i++) {
        unsigned at=first(&w->table,entries[i].key),n=members(&w->table,entries[i].key);
        for(unsigned j=0;j<n;j++) {
            struct candidate_record *e=w->table.entries+at+j;
            e->flags=e->target.pair==entries[i].pair?DEFAULT_FLAG:0u;
        }
    }
    if(!r)r=all_targets(&w->table,log,cancel);
    bool changed=memcmp(&w->table,&w->other,sizeof(w->table))!=0;
    if(!r && changed)r=publish(w,log,cancel);
    free(w);return r;
}
int kui_vmu_game_candidates_apply(const struct kui_vmu_game_assignment *entries,size_t count,
    const struct kui_vmu_game_candidates_storage *storage,kui_log_fn log,kui_cancel_fn cancel) {
    return apply(entries,count,false,storage,NULL,log,cancel);
}
int kui_vmu_game_candidates_apply_confirmed(const struct kui_vmu_game_assignment *entries,size_t count,
    const struct kui_vmu_game_candidates_storage *storage,const struct kui_vmu_game_candidates_repair *repair,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(!repair)return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
    return apply(entries,count,false,storage,repair,log,cancel);
}
int kui_vmu_game_candidates_apply_selected(const struct kui_vmu_game_assignment *entries,size_t count,
    const struct kui_vmu_game_candidates_storage *storage,kui_log_fn log,kui_cancel_fn cancel) {
    return apply(entries,count,true,storage,NULL,log,cancel);
}
int kui_vmu_game_candidates_set_default(const char *product,const struct kui_vmu_game_candidate_target *target,
    uint64_t expected_sequence,const struct kui_vmu_game_candidates_storage *storage,kui_log_fn log,kui_cancel_fn cancel) {
    char key[12];
    if(!valid_storage(storage) || !kui_vmu_game_card_key(product,key) ||
       (target && (!target->pair || target->pair>9999u || zero(target->identity,16u)))) return KUI_VMU_GAME_CANDIDATES_ARGUMENT;
    struct candidate_work *w=calloc(1,sizeof(*w));if(!w)return KUI_VMU_GAME_CANDIDATES_IO;
    int r=load(w,storage,cancel);unsigned at=0,n=0;bool matched=target==NULL,changed=false;
    if(!r && w->table.sequence!=expected_sequence)r=KUI_VMU_GAME_CANDIDATES_STALE;
    if(!r) {at=first(&w->table,key);n=members(&w->table,key);if(!n)r=KUI_VMU_GAME_CANDIDATES_ABSENT;}
    for(unsigned i=0;!r && i<n;i++) {
        struct candidate_record *e=w->table.entries+at+i;
        bool selected=target && target->pair==e->target.pair && !memcmp(target->identity,e->target.identity,16u);
        if(selected)matched=true;
        uint32_t flag=selected?DEFAULT_FLAG:0u;changed|=flag!=e->flags;e->flags=flag;
    }
    if(!r && !matched)r=KUI_VMU_GAME_CANDIDATES_STALE;
    if(!r)r=all_targets(&w->table,log,cancel);
    if(!r && changed)r=publish(w,log,cancel);
    free(w);return r;
}
