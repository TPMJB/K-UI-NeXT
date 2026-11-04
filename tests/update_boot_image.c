/* SPDX-License-Identifier: GPL-3.0-only */
/* Real FatFs and production boot/resolver codecs on a synthetic block device.
 * Only filesystem-call fault injection is mocked; packages contain generated
 * bytes, never game code or personal data. */
#include "update_fixture.h"
#include "kui/update_boot.h"
#include "kui/storage_policy.h"
#include "kui/media.h"
#include <stdio.h>

#define ROOT_BUILD "111111111111"
#define BUILD_A "222222222222"
#define BUILD_B "333333333333"
#define MODULE "0:/KUI/apps/games/retail-boot.kui"
static unsigned checks,writes,root_opens,root_bytes,member_opens;
static uint8_t *disk;static size_t disk_bytes;static FATFS fs;
static bool observe,cancel_now,media_fault_fired;
static const char *reading_path;
static char member[192],read_fault[192],close_fault[192],cancel_read[192],media_fault[192];
static struct {FIL *file;char path[192];} opened[8];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Update boot failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint64_t blocks(void *unused) {(void)unused;return disk_bytes/512u;}
static int read_blocks(void *unused,uint32_t lba,size_t count,uint8_t *out) {
    (void)unused;if((uint64_t)lba+count>blocks(NULL))return -1;
    if(observe && !media_fault_fired && reading_path && media_fault[0] && !strcmp(reading_path,media_fault)) {
        media_fault_fired=true;return -1;
    }
    memcpy(out,disk+(size_t)lba*512u,count*512u);return 0;
}
static int write_blocks(void *unused,uint32_t lba,size_t count,const uint8_t *in) {
    (void)unused;if((uint64_t)lba+count>blocks(NULL))return -1;
    ++writes;memcpy(disk+(size_t)lba*512u,in,count*512u);return 0;
}
static int sync_blocks(void *unused) {(void)unused;return 0;}
static const struct kui_media_ops media={NULL,blocks,read_blocks,write_blocks,sync_blocks};
uint32_t kui_clock_fattime(void) {return ((2026u-1980u)<<25)|(11u<<21)|(4u<<16);}
static void logger(const char *format,...) {(void)format;}
static bool cancel(void) {return cancel_now;}
static const char *open_path(FIL *file) {
    for(unsigned i=0;i<8u;i++)if(opened[i].file==file)return opened[i].path;
    CHECK(false);return "";
}
FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __wrap_f_open(FIL *file,const TCHAR *path,BYTE flags) {
    if(observe)CHECK(flags==FA_READ);
    FRESULT r=__real_f_open(file,path,flags);if(r!=FR_OK)return r;
    unsigned i=0;while(i<8u && opened[i].file)++i;CHECK(i<8u);
    opened[i].file=file;CHECK(strlen(path)<sizeof(opened[i].path));strcpy(opened[i].path,path);
    if(observe) {if(!strcmp(path,KUI_RUNTIME_PATH))++root_opens;if(!strcmp(path,member))++member_opens;}
    return r;
}
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *file,void *data,UINT bytes,UINT *got) {
    const char *path=open_path(file);
    if(observe && read_fault[0] && !strcmp(path,read_fault) && f_tell(file)>=64u) {*got=0;return FR_DISK_ERR;}
    reading_path=path;FRESULT r=__real_f_read(file,data,bytes,got);reading_path=NULL;
    if(observe && !strcmp(path,KUI_RUNTIME_PATH))root_bytes+=*got;
    if(observe && cancel_read[0] && !strcmp(path,cancel_read) && f_tell(file)>64u)cancel_now=true;
    return r;
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *file) {
    bool fail=observe && close_fault[0] && !strcmp(open_path(file),close_fault);
    for(unsigned i=0;i<8u;i++)if(opened[i].file==file) {opened[i].file=NULL;break;}
    FRESULT r=__real_f_close(file);return fail?FR_DISK_ERR:r;
}
static void idle(void) {
    observe=false;cancel_now=false;read_fault[0]=close_fault[0]=cancel_read[0]=media_fault[0]=0;
    media_fault_fired=false;reading_path=NULL;
    root_opens=root_bytes=member_opens=0;
    for(unsigned i=0;i<8u;i++)CHECK(!opened[i].file);
}
static void parents(const char *path) {
    char copy[192];CHECK(strlen(path)<sizeof(copy));strcpy(copy,path);
    for(char *p=copy+3u;*p;p++)if(*p=='/') {
        *p=0;FRESULT r=f_mkdir(copy);CHECK(r==FR_OK || r==FR_EXIST);*p='/';
    }
}
static void write_file(const char *path,const void *data,size_t bytes) {
    CHECK(!observe);parents(path);FIL file;UINT got;
    CHECK(f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    CHECK(f_write(&file,data,(UINT)bytes,&got)==FR_OK && got==bytes);CHECK(f_close(&file)==FR_OK);
}
static void remove_file(const char *path) {
    CHECK(!observe);FRESULT r=f_unlink(path);CHECK(r==FR_OK || r==FR_NO_FILE || r==FR_NO_PATH);
}
static void runtime_fix(struct uf_package *p,unsigned i) {
    uint8_t *f=uf_file(p,i);uf_put32(f+32u,kui_crc32(0,f+64u,256u));
    uf_put32(f+60u,kui_crc32(0,f,60u));uf_file_hash(p,i);
}
static void transport_marker(struct uf_package *p) {
    const uint32_t words[]={KUI_STORAGE_BOOT_MAGIC1,KUI_STORAGE_BOOT_MAGIC2,1u,KUI_STORAGE_AUTO,~(uint32_t)KUI_STORAGE_AUTO};
    for(unsigned i=0;i<5u;i++)uf_put32(uf_file(p,0)+64u+80u+4u*i,words[i]);
    runtime_fix(p,0);
}
static void seed(struct uf_package *p,bool root) {
    struct kui_update_package decoded;CHECK(uf_decode(p,&decoded)==KUI_UPDATE_OK);
    char path[192];for(unsigned i=0;i<p->count;i++) {
        if(root)snprintf(path,sizeof(path),"0:/KUI/%s",kui_update_catalog_path(i));
        else CHECK(kui_update_bank_path(decoded.build,kui_update_catalog_path(i),path,sizeof(path)));
        write_file(path,uf_file(p,i),UF_FILE_BYTES);
    }
    if(!root) {
        snprintf(path,sizeof(path),KUI_UPDATE_SYSTEM "/%s/package.bin",decoded.build);
        write_file(path,p->data,128u+128u*p->count);
    }
}
static void selection(unsigned slot,uint64_t sequence,struct uf_package *p,const char *previous) {
    struct kui_update_activation a={.sequence=sequence};
    if(p) {memcpy(a.selected,p->data+32u,12u);memcpy(a.manifest_sha256,p->data+64u,32u);}
    if(previous)strcpy(a.previous,previous);
    uint8_t wire[128];CHECK(kui_update_activation_encode(&a,wire));
    write_file(slot?KUI_UPDATE_ACTIVE1:KUI_UPDATE_ACTIVE0,wire,sizeof(wire));
}
static enum kui_update_boot_result boot(const char *compiled,unsigned transport,
    struct kui_runtime_image *out,struct kui_update_boot_view *view,bool baseline) {
    unsigned before=writes;observe=true;
    enum kui_update_boot_result r=baseline?kui_update_boot_baseline(compiled,transport,out,view,logger,cancel):
        kui_update_boot_read(compiled,transport,out,view,logger,cancel);
    observe=false;CHECK(writes==before);
    for(unsigned i=0;i<8u;i++)CHECK(!opened[i].file);
    CHECK((r==KUI_UPDATE_BOOT_CHAIN)==(out->data!=NULL));return r;
}
static void baseline_tests(struct uf_package *root) {
    struct kui_runtime_image image;struct kui_update_boot_view view;char path[192];
    idle();CHECK(boot(ROOT_BUILD,KUI_STORAGE_SCIF,&image,&view,false)==KUI_UPDATE_BOOT_BASELINE);
    CHECK(!view.bank[0] && !view.fallback && root_opens==1u && root_bytes==64u);
    CHECK(kui_runtime_resolve_path(MODULE,path,sizeof(path)) && !strcmp(path,MODULE));
    strcpy(path,MODULE);CHECK(kui_runtime_resolve_path(path,path,sizeof(path)) && !strcmp(path,MODULE));
    idle();CHECK(boot(BUILD_A,KUI_STORAGE_SCI,&image,&view,true)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(!strcmp(image.info.build,ROOT_BUILD) && root_opens==2u && root_bytes==64u+UF_FILE_BYTES);
    CHECK(kui_storage_boot_transport((const void *)((uint8_t *)image.data+80u))==KUI_STORAGE_SCI);
    CHECK(!kui_runtime_resolve_path(MODULE,path,sizeof(path)));kui_runtime_free(&image);
    idle();CHECK(boot(BUILD_A,KUI_STORAGE_IDE,&image,&view,false)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(kui_storage_boot_transport((const void *)((uint8_t *)image.data+80u))==KUI_STORAGE_IDE);kui_runtime_free(&image);
    idle();cancel_now=true;CHECK(boot(ROOT_BUILD,KUI_STORAGE_SCIF,&image,&view,false)==KUI_UPDATE_BOOT_ERROR);
    CHECK(!kui_runtime_resolve_path(MODULE,path,sizeof(path)));idle();
    CHECK(boot(ROOT_BUILD,KUI_STORAGE_AUTO,&image,&view,false)==KUI_UPDATE_BOOT_ERROR);
    (void)root;
}
static void bank_tests(struct uf_package *a,struct uf_package *b) {
    struct kui_runtime_image image;struct kui_update_boot_view view;char path[192],runtime[192];
    CHECK(kui_update_bank_path(BUILD_A,"apps/games/retail-boot.kui",member,sizeof(member)));
    CHECK(kui_update_bank_path(BUILD_A,"runtime.kui",runtime,sizeof(runtime)));
    selection(0,10u,a,"");idle();
    CHECK(boot(ROOT_BUILD,KUI_STORAGE_SCI,&image,&view,false)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(!strcmp(view.bank,BUILD_A) && !strcmp(image.info.build,BUILD_A));
    CHECK(kui_storage_boot_transport((const void *)((uint8_t *)image.data+80u))==KUI_STORAGE_SCI);
    CHECK(!kui_runtime_resolve_path(MODULE,path,sizeof(path)));kui_runtime_free(&image);
    idle();CHECK(boot(BUILD_A,KUI_STORAGE_SCIF,&image,&view,false)==KUI_UPDATE_BOOT_PINNED);
    CHECK(!strcmp(view.bank,BUILD_A) && !view.fallback && !root_opens);
    CHECK(kui_runtime_resolve_path(MODULE,path,sizeof(path)) && !strcmp(path,member));
    strcpy(path,MODULE);CHECK(kui_runtime_resolve_path(path,path,sizeof(path)) && !strcmp(path,member));
    CHECK(!kui_runtime_resolve_path("0:/KUI/apps/games/unknown.kui",path,sizeof(path)));
    CHECK(!kui_runtime_resolve_path(MODULE,path,5u));
    unsigned before=writes;member_opens=0;observe=true;
    CHECK(kui_runtime_read(MODULE,&image,logger,cancel)==KUI_RUNTIME_OK);
    observe=false;CHECK(member_opens==1u && !root_opens && writes==before && !strcmp(image.info.build,BUILD_A));
    kui_runtime_free(&image);
    /* A valid, resealed runtime CRC cannot replace the manifest SHA. Test
     * both payload mutation and a header-only mutation with identical body. */
    unsigned module=0;while(module<a->count && strcmp(kui_update_catalog_path(module),"apps/games/retail-boot.kui"))++module;
    CHECK(module<a->count);uint8_t altered[UF_FILE_BYTES];
    for(unsigned kind=0;kind<2u;kind++) {
        memcpy(altered,uf_file(a,module),sizeof(altered));
        if(kind==0u) {altered[200]^=1u;uf_put32(altered+32,kui_crc32(0,altered+64,256u));}
        else uf_put32(altered+28,768u);
        uf_put32(altered+60,kui_crc32(0,altered,60u));write_file(member,altered,sizeof(altered));
        member_opens=0;observe=true;CHECK(kui_runtime_read(MODULE,&image,logger,cancel)==KUI_RUNTIME_CHECKSUM);
        observe=false;CHECK(!image.data && member_opens==1u);write_file(member,uf_file(a,module),UF_FILE_BYTES);
    }
    remove_file(member);observe=true;CHECK(kui_runtime_read(MODULE,&image,logger,cancel)==KUI_RUNTIME_IO);observe=false;
    CHECK(!image.data && !root_opens);write_file(member,uf_file(a,module),UF_FILE_BYTES);
    strcpy(read_fault,member);observe=true;CHECK(kui_runtime_read(MODULE,&image,logger,cancel)==KUI_RUNTIME_IO);observe=false;
    CHECK(!image.data);read_fault[0]=0;
    strcpy(close_fault,member);observe=true;CHECK(kui_runtime_read(MODULE,&image,logger,cancel)==KUI_RUNTIME_IO);observe=false;
    CHECK(!image.data);close_fault[0]=0;
    /* Missing/damaged selected runtime, receipt, capability and manifest pin
     * fall back to the older complete selection, never its root modules. */
    selection(1,20u,b,BUILD_A);char newer[192];CHECK(kui_update_bank_path(BUILD_B,"runtime.kui",newer,sizeof(newer)));
    uint8_t *saved_b=malloc(b->bytes);CHECK(saved_b);memcpy(saved_b,b->data,b->bytes);
    for(unsigned fault=0;fault<8u;fault++) {
        memcpy(b->data,saved_b,b->bytes);
        if(fault>=5u) {
            uint8_t *f=uf_file(b,0);
            if(fault==5u)f[64u+32u]^=1u;
            if(fault==6u)memcpy(f+64u+112u,f+64u+32u,16u);
            if(fault==7u)memcpy(f+40u,"444444444444",12u);
            runtime_fix(b,0);
        }
        seed(b,false);selection(1,20u,b,BUILD_A);
        if(fault==0u)remove_file(newer);
        if(fault==1u) {memcpy(altered,uf_file(b,0),sizeof(altered));altered[200]^=1u;write_file(newer,altered,sizeof(altered));}
        if(fault==2u) {char receipt[192];snprintf(receipt,sizeof(receipt),KUI_UPDATE_SYSTEM "/%s/package.bin",BUILD_B);remove_file(receipt);}
        if(fault==3u) {
            struct kui_update_activation s={.sequence=20u};strcpy(s.selected,BUILD_B);s.manifest_sha256[0]=0x42;
            uint8_t wire[128];CHECK(kui_update_activation_encode(&s,wire));write_file(KUI_UPDATE_ACTIVE1,wire,sizeof(wire));
        }
        if(fault==4u)strcpy(read_fault,newer);
        idle();if(fault==4u)strcpy(read_fault,newer);
        CHECK(boot(BUILD_A,KUI_STORAGE_SCIF,&image,&view,false)==KUI_UPDATE_BOOT_PINNED);
        CHECK(view.fallback && view.notice[0] && !strcmp(view.bank,BUILD_A));
    }
    memcpy(b->data,saved_b,b->bytes);free(saved_b);
    idle();seed(b,false);selection(1,20u,b,BUILD_A);strcpy(cancel_read,newer);
    CHECK(boot(BUILD_A,KUI_STORAGE_SCI,&image,&view,false)==KUI_UPDATE_BOOT_ERROR);
    CHECK(!image.data && !kui_runtime_resolve_path(MODULE,path,sizeof(path)));idle();
    /* Same-sequence disagreement is not an arbitrary bank choice. */
    selection(1,10u,b,BUILD_A);CHECK(boot(ROOT_BUILD,KUI_STORAGE_SCIF,&image,&view,false)==KUI_UPDATE_BOOT_BASELINE);
    CHECK(view.fallback && !view.bank[0]);
    selection(1,10u,a,"");idle();CHECK(boot(BUILD_A,KUI_STORAGE_SCIF,&image,&view,false)==KUI_UPDATE_BOOT_PINNED);
    /* A torn newer record preserves the older selection. */
    uint8_t torn[64]={0};write_file(KUI_UPDATE_ACTIVE1,torn,sizeof(torn));idle();
    CHECK(boot(BUILD_A,KUI_STORAGE_SCIF,&image,&view,false)==KUI_UPDATE_BOOT_PINNED && view.fallback);
    /* Rollback to empty selection while in a bank must chain the root GUI. */
    selection(1,30u,NULL,BUILD_A);idle();CHECK(boot(BUILD_A,KUI_STORAGE_SCI,&image,&view,false)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(!view.bank[0] && !strcmp(view.build,ROOT_BUILD));kui_runtime_free(&image);
    /* An explicit baseline rollback must not reactivate the older bank when
     * the retained root file is unavailable. */
    remove_file(KUI_RUNTIME_PATH);idle();
    CHECK(boot(BUILD_A,KUI_STORAGE_SCI,&image,&view,false)==KUI_UPDATE_BOOT_ERROR);
    CHECK(!kui_runtime_resolve_path(MODULE,path,sizeof(path)));
    struct uf_package restored_root=uf_build(ROOT_BUILD,0u);transport_marker(&restored_root);seed(&restored_root,true);uf_free(&restored_root);
    /* Manual baseline bypass ignores even a valid selected bank. */
    selection(1,40u,b,BUILD_A);idle();CHECK(boot(ROOT_BUILD,KUI_STORAGE_SCIF,&image,&view,true)==KUI_UPDATE_BOOT_BASELINE);
    CHECK(!view.bank[0] && root_bytes==64u);
    /* Real media faults poison diskio's session. Unlike a logical missing or
     * corrupt file, this cannot recover in-place: refuse all module loads and
     * require an explicit reconnect/restart instead of mixing root modules. */
    idle();strcpy(media_fault,newer);
    CHECK(boot(ROOT_BUILD,KUI_STORAGE_SCIF,&image,&view,false)==KUI_UPDATE_BOOT_ERROR && media_fault_fired);
    CHECK(!kui_runtime_resolve_path(MODULE,path,sizeof(path)));
    idle();CHECK(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(&media);CHECK(f_mount(&fs,"0:",1)==FR_OK);
    kui_update_boot_block();CHECK(!kui_runtime_resolve_path(MODULE,path,sizeof(path)));
    kui_update_boot_reset();CHECK(kui_runtime_resolve_path(MODULE,path,sizeof(path)) && !strcmp(path,MODULE));
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *input=fopen(argv[1],"rb");CHECK(input && !fseek(input,0,SEEK_END));long bytes=ftell(input);
    CHECK(bytes>0 && !(bytes%512) && !fseek(input,0,SEEK_SET));disk_bytes=(size_t)bytes;disk=malloc(disk_bytes);
    CHECK(disk && fread(disk,1,disk_bytes,input)==disk_bytes && !fclose(input));
    kui_media_set(&media);
    CHECK(f_mount(&fs,"0:",1)==FR_OK);
    struct uf_package root=uf_build(ROOT_BUILD,0u),a=uf_build(BUILD_A,0u),b=uf_build(BUILD_B,1u);
    transport_marker(&root);transport_marker(&a);transport_marker(&b);seed(&root,true);seed(&a,false);seed(&b,false);
    baseline_tests(&root);bank_tests(&a,&b);idle();kui_update_boot_reset();
    CHECK(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);uf_free(&root);uf_free(&a);uf_free(&b);free(disk);
    printf("PASS update boot/resolver: %u checks; header-only baseline, checked chain/transport, older-slot fallback, pinned same-stream module SHA, no root mixing or boot writes\n",checks);
    return 0;
}
