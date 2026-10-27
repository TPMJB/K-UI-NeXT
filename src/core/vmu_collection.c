/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_collection.h"
#include "kui/destination.h"
#include "kui/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROOT "0:" KUI_VMU_COLLECTION_ROOT
enum { OK=KUI_VMU_COLLECTION_OK, ARG=KUI_VMU_COLLECTION_ARGUMENT,
       IO=KUI_VMU_COLLECTION_IO, BAD=KUI_VMU_COLLECTION_VERIFY,
       STOP=KUI_VMU_COLLECTION_CANCELLED };
static unsigned get32(const uint8_t *p) {
    return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);
}
static void put32(uint8_t *p,unsigned n) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(8*i));
}
static int zero(const uint8_t *p,size_t n) {
    for(size_t i=0;i<n;i++) if(p[i]) return 0;
    return 1;
}
static int stopped(kui_cancel_fn cancel) {return cancel && cancel();}
static int file_error(FRESULT r) {
    return r==FR_NO_FILE || r==FR_NO_PATH || r==FR_INVALID_NAME?BAD:IO;
}
static int path_for(char out[192],unsigned id,const char *leaf) {
    int n=snprintf(out,192,ROOT "/v%04u/%s",id,leaf);
    return n>0 && n<192;
}
unsigned kui_vmu_collection_path_id(const char *path) {
    static const char prefix[]=ROOT "/v";
    if(!path || strncmp(path,prefix,sizeof(prefix)-1)) return 0;
    const char *at=path+sizeof(prefix)-1;
    if(strlen(at)!=13u) return 0;
    unsigned id=0;
    for(unsigned i=0;i<4;i++) {
        if(at[i]<'0' || at[i]>'9') return 0;
        id=id*10u+(unsigned)(at[i]-'0');
    }
    return id && !strcmp(at+4,"/card.vmd")?id:0;
}
/* Every open has one matching close, even after cancellation or failed sync.
 * Existing files are read only; publication only uses CREATE_NEW. */
static int read_exact(const char *path,void *out,size_t bytes,
    const void *expected,kui_cancel_fn cancel) {
    FIL f;FRESULT r=f_open(&f,path,FA_READ);
    if(r!=FR_OK) return file_error(r);
    int result=f_size(&f)==bytes?OK:BAD;
    uint8_t block[512];
    for(size_t at=0;result==OK && at<bytes;) {
        if(stopped(cancel)) {result=STOP;break;}
        UINT count=(UINT)(bytes-at>sizeof(block)?sizeof(block):bytes-at),got=0;
        r=f_read(&f,block,count,&got);
        if(r!=FR_OK || got!=count) {result=IO;break;}
        if(expected && memcmp(block,(const uint8_t *)expected+at,count)) {result=BAD;break;}
        if(out) memcpy((uint8_t *)out+at,block,count);
        at+=count;
    }
    if(f_close(&f)!=FR_OK) result=IO;
    return result;
}
static int write_new(const char *path,const void *bytes,size_t count,kui_cancel_fn cancel) {
    if(stopped(cancel)) return STOP;
    FIL f;FRESULT r=f_open(&f,path,FA_WRITE|FA_CREATE_NEW);
    if(r!=FR_OK) return IO;
    int result=OK;
    for(size_t at=0;at<count;) {
        if(stopped(cancel)) {result=STOP;break;}
        UINT n=(UINT)(count-at>4096?4096:count-at),put=0;
        r=f_write(&f,(const uint8_t *)bytes+at,n,&put);
        if(r!=FR_OK || put!=n) {result=IO;break;}
        at+=n;
    }
    if(result==OK && f_sync(&f)!=FR_OK) result=IO;
    if(f_close(&f)!=FR_OK) result=IO;
    return result==OK?read_exact(path,NULL,count,bytes,cancel):result;
}
struct image_reader {
    FIL *file;
    const uint8_t *memory;
    kui_cancel_fn cancel;
    int error;
};
static int image_read(void *context,uint16_t block,void *out) {
    struct image_reader *r=context;
    if(stopped(r->cancel)) {r->error=STOP;return -1;}
    if(block>=256) {r->error=BAD;return -1;}
    if(r->memory) {memcpy(out,r->memory+(size_t)block*512u,512);return 0;}
    UINT got=0;
    if(f_lseek(r->file,(FSIZE_t)block*512u)!=FR_OK ||
       f_read(r->file,out,512,&got)!=FR_OK || got!=512) {r->error=IO;return -1;}
    return 0;
}
static int inspect_image(struct image_reader *r,struct kui_vmu_collection_card *card,
    uint32_t *crc) {
    struct kui_vmu_image_workspace *work=malloc(sizeof(*work));
    if(!work) return IO;
    struct kui_vmu_image_io io={r,image_read,NULL,NULL};
    int result=kui_vmu_image_validate(&io,KUI_VMU_IMAGE_BYTES,work,&card->summary);
    free(work);
    if(result) return r->error?r->error:KUI_VMU_COLLECTION_IMAGE;
    struct kui_sha256 sha;kui_sha256_init(&sha);
    uint8_t block[512];*crc=0;
    for(unsigned i=0;i<256;i++) {
        if(image_read(r,(uint16_t)i,block)) return r->error;
        kui_sha256_update(&sha,block,512);*crc=kui_crc32(*crc,block,512);
    }
    kui_sha256_digest(&sha,card->sha256);
    return OK;
}
static int summary_equal(const struct kui_vmu_image_summary *a,const uint8_t *b) {
    return a->files==get32(b+20) && a->game_files==get32(b+24) &&
        a->used_blocks==get32(b+28) && a->free_blocks==get32(b+32);
}
static int verify_id(unsigned id,struct kui_vmu_collection_card *out,kui_cancel_fn cancel) {
    uint8_t ready[512],identity[16],journal[1024]={0},proof[32],expected[32]={0};
    char path[192];path_for(path,id,"ready.bin");
    int result=read_exact(path,ready,sizeof(ready),NULL,cancel);
    if(result) return result;
    if(memcmp(ready,"KUIVMC1\0",8) || get32(ready+8)!=1 || get32(ready+12)!=id ||
       get32(ready+16)!=KUI_VMU_IMAGE_BYTES || !ready[36] || !zero(ready+100,408) ||
       get32(ready+508)!=kui_crc32(0,ready,508) || zero(ready+52,16)) return BAD;
    bool terminated=false;
    for(unsigned i=36;i<52;i++) {
        if(!ready[i]) terminated=true;
        else if(terminated || ready[i]<32 || ready[i]>126) return BAD;
    }
    if(!terminated) return BAD;
    path_for(path,id,"identity.bin");
    result=read_exact(path,identity,16,ready+52,cancel);if(result) return result;
    path_for(path,id,"card.journal");
    result=read_exact(path,NULL,1024,journal,cancel);if(result) return result;
    path_for(path,id,"card.vmd.crc");
    result=read_exact(path,proof,32,NULL,cancel);if(result) return result;
    path_for(path,id,"card.vmd");
    FIL f;FRESULT fr=f_open(&f,path,FA_READ);
    if(fr!=FR_OK) return file_error(fr);
    struct kui_vmu_collection_card card={.id=id};uint32_t crc=0;
    struct image_reader reader={&f,NULL,cancel,0};
    result=f_size(&f)==KUI_VMU_IMAGE_BYTES?inspect_image(&reader,&card,&crc):BAD;
    if(f_close(&f)!=FR_OK) result=IO;
    if(result) return result;
    memcpy(expected,"KUIVMD1\0",8);put32(expected+8,KUI_VMU_IMAGE_BYTES);
    put32(expected+12,crc);put32(expected+28,kui_crc32(0,expected,28));
    if(memcmp(expected,proof,32) || memcmp(card.sha256,ready+68,32) ||
       !summary_equal(&card.summary,ready)) return BAD;
    memcpy(card.label,ready+36,16);memcpy(card.path,path,strlen(path)+1);
    *out=card;return OK;
}
int kui_vmu_collection_verify(const char *path,struct kui_vmu_collection_card *out,
    kui_log_fn log,kui_cancel_fn cancel) {
    (void)log;
    if(!out) return ARG;
    unsigned id=kui_vmu_collection_path_id(path);
    memset(out,0,sizeof(*out));
    if(!id) return ARG;
    return verify_id(id,out,cancel);
}
int kui_vmu_collection_publish(const void *image,size_t bytes,const char *label,
    struct kui_vmu_collection_card *out,kui_log_fn log,kui_cancel_fn cancel) {
    if(!out) return ARG;
    uintptr_t a=(uintptr_t)image,b=(uintptr_t)out;
    if(image && (a<=b?b-a<bytes:a-b<sizeof(*out))) return ARG;
    char text[16]={0};
    if(label) for(unsigned i=0;i<15 && label[i];i++)
        text[i]=label[i]>=32 && label[i]<=126?label[i]:'_';
    if(!text[0]) memcpy(text,"Virtual card",13);
    memset(out,0,sizeof(*out));
    if(!image || bytes!=KUI_VMU_IMAGE_BYTES) return ARG;
    struct kui_vmu_collection_card card={0};uint32_t crc=0;
    struct image_reader reader={NULL,image,cancel,0};
    int result=inspect_image(&reader,&card,&crc);if(result) return result;
    if(stopped(cancel)) return STOP;
    if(!kui_destination_mkdirs(KUI_VMU_COLLECTION_ROOT,log)) return IO;
    char folder[192],path[192];
    unsigned id;
    for(id=1;id<=9999;id++) {
        if(stopped(cancel)) return STOP;
        snprintf(folder,sizeof(folder),ROOT "/v%04u",id);
        FRESULT r=f_mkdir(folder);
        if(r==FR_OK) break;
        if(r!=FR_EXIST) return IO;
    }
    if(id>9999) return KUI_VMU_COLLECTION_FULL;
    uint8_t ready[512]={0},proof[32]={0},journal[1024]={0},identity[32];
    uint8_t id_bytes[4];put32(id_bytes,id);
    struct kui_sha256 sha;kui_sha256_init(&sha);
    static const char domain[]="K-UI virtual collection identity v1";
    kui_sha256_update(&sha,domain,sizeof(domain));kui_sha256_update(&sha,id_bytes,4);
    kui_sha256_update(&sha,card.sha256,32);kui_sha256_digest(&sha,identity);
    if(zero(identity,16)) identity[0]=1;
    memcpy(proof,"KUIVMD1\0",8);put32(proof+8,KUI_VMU_IMAGE_BYTES);put32(proof+12,crc);
    put32(proof+28,kui_crc32(0,proof,28));
    memcpy(ready,"KUIVMC1\0",8);put32(ready+8,1);put32(ready+12,id);
    put32(ready+16,KUI_VMU_IMAGE_BYTES);put32(ready+20,card.summary.files);
    put32(ready+24,card.summary.game_files);put32(ready+28,card.summary.used_blocks);
    put32(ready+32,card.summary.free_blocks);memcpy(ready+36,text,16);
    memcpy(ready+52,identity,16);memcpy(ready+68,card.sha256,32);
    put32(ready+508,kui_crc32(0,ready,508));
    const char *names[]={"card.vmd","card.journal","identity.bin","card.vmd.crc","ready.bin"};
    const void *contents[]={image,journal,identity,proof,ready};
    const size_t sizes[]={KUI_VMU_IMAGE_BYTES,1024,16,32,512};
    for(unsigned i=0;i<5;i++) {
        /* Stop is honored up to publication. Once the final ready marker
         * starts, finish its bounded readback/admission without cancellation:
         * a published complete card must not be reported as cancelled. */
        if(i==4 && stopped(cancel)) return STOP;
        path_for(path,id,names[i]);result=write_new(path,contents[i],sizes[i],i==4?NULL:cancel);
        if(result) return result;
    }
    result=verify_id(id,&card,NULL);
    if(result) return result;
    *out=card;
    if(log) log("Virtual VMU: created %s (%u saves, %u free blocks)",card.path,card.summary.files,card.summary.free_blocks);
    return OK;
}
int kui_vmu_collection_list(unsigned page,struct kui_vmu_collection_page *out,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(!out) return ARG;
    memset(out,0,sizeof(*out));
    if(page>9999u/KUI_VMU_COLLECTION_ROWS) return ARG;
    if(stopped(cancel)) return STOP;
    DIR dir;FRESULT fr=f_opendir(&dir,ROOT);
    if(fr==FR_NO_FILE || fr==FR_NO_PATH) {out->page=page;return OK;}
    if(fr!=FR_OK) return IO;
    /* 1.25 KiB membership bitmap sorts IDs without trusting names as paths or
     * opening 9,999 nonexistent directories on every manager refresh. */
    uint8_t ids[1250]={0};int result=OK;FILINFO info;
    for(;;) {
        if(stopped(cancel)) {result=STOP;break;}
        fr=f_readdir(&dir,&info);
        if(fr!=FR_OK) {result=IO;break;}
        if(!info.fname[0]) break;
        if(!(info.fattrib&AM_DIR) || strlen(info.fname)!=5 || info.fname[0]!='v') continue;
        unsigned id=0;
        for(unsigned i=1;i<5;i++) {
            if(info.fname[i]<'0' || info.fname[i]>'9') {id=0;break;}
            id=id*10u+(unsigned)(info.fname[i]-'0');
        }
        if(id) ids[id/8u]|=(uint8_t)(1u<<(id%8u));
    }
    if(f_closedir(&dir)!=FR_OK) result=IO;
    struct kui_vmu_collection_page found={.page=page};
    for(unsigned id=1;result==OK && id<=9999;id++) {
        if(!(ids[id/8u]&(1u<<(id%8u)))) continue;
        struct kui_vmu_collection_card card;
        int r=verify_id(id,&card,cancel);
        if(r==BAD || r==KUI_VMU_COLLECTION_IMAGE) {
            if(log) log("Virtual VMU: incomplete or changed snapshot v%04u omitted",id);
            continue;
        }
        if(r) {result=r;break;}
        if(found.total>=page*KUI_VMU_COLLECTION_ROWS && found.count<KUI_VMU_COLLECTION_ROWS)
            found.entries[found.count++]=card;
        ++found.total;
    }
    if(result==OK) *out=found;
    return result;
}
