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
#define ROOT_SWAP "0:/KUI/root-reread-test.kui"
static unsigned checks,writes,root_opens,root_bytes,member_opens,bank_gui_bytes,bank_gui_opens;
static uint8_t *disk;static size_t disk_bytes;static FATFS fs;
static bool observe,cancel_now,media_fault_fired,root_reread_swap;
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
    const char *actual=observe && root_reread_swap && root_opens && !strcmp(path,KUI_RUNTIME_PATH)?ROOT_SWAP:path;
    FRESULT r=__real_f_open(file,actual,flags);if(r!=FR_OK)return r;
    unsigned i=0;while(i<8u && opened[i].file)++i;CHECK(i<8u);
    opened[i].file=file;CHECK(strlen(path)<sizeof(opened[i].path));strcpy(opened[i].path,path);
    if(observe) {
        if(!strcmp(path,KUI_RUNTIME_PATH))++root_opens;
        if(!strcmp(path,member))++member_opens;
        if(strstr(path,KUI_UPDATE_SYSTEM "/")==path && strstr(path,"/runtime.kui"))++bank_gui_opens;
    }
    return r;
}
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *file,void *data,UINT bytes,UINT *got) {
    const char *path=open_path(file);
    if(observe && read_fault[0] && !strcmp(path,read_fault) && f_tell(file)>=64u) {*got=0;return FR_DISK_ERR;}
    reading_path=path;FRESULT r=__real_f_read(file,data,bytes,got);reading_path=NULL;
    if(observe && !strcmp(path,KUI_RUNTIME_PATH))root_bytes+=*got;
    if(observe && strstr(path,KUI_UPDATE_SYSTEM "/")==path && strstr(path,"/runtime.kui"))bank_gui_bytes+=*got;
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
    observe=false;cancel_now=root_reread_swap=false;read_fault[0]=close_fault[0]=cancel_read[0]=media_fault[0]=0;
    media_fault_fired=false;reading_path=NULL;
    root_opens=root_bytes=member_opens=bank_gui_bytes=bank_gui_opens=0;
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
    uint8_t *f=uf_file(p,i);uf_put32(f+32u,kui_crc32(0,f+64u,UF_PAYLOAD_BYTES));
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
        if(!root && decoded.entries[i].policy==KUI_UPDATE_INHERIT_ROOT)continue;
        if(root)snprintf(path,sizeof(path),"0:/KUI/%s",kui_update_catalog_path(i));
        else CHECK(kui_update_bank_path(decoded.build,decoded.entries[i].path,path,sizeof(path)));
        write_file(path,root?uf_file(p,i):p->data+decoded.entries[i].payload_offset,
            root?UF_FILE_BYTES:(size_t)decoded.entries[i].bytes);
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
static enum kui_update_boot_result boot_handoff(const char *compiled,unsigned transport,
    const struct kui_update_handoff_context *context,struct kui_runtime_image *out,struct kui_update_boot_view *view) {
    unsigned before=writes;observe=true;
    enum kui_update_boot_result r=kui_update_boot_read_handoff(compiled,transport,context,out,view,logger,cancel);
    observe=false;CHECK(writes==before);
    for(unsigned i=0;i<8u;i++)CHECK(!opened[i].file);
    CHECK((r==KUI_UPDATE_BOOT_CHAIN)==(out->data!=NULL));return r;
}
static enum kui_update_boot_result reload_gui(const char *compiled,unsigned transport,
    struct kui_runtime_image *out,struct kui_update_boot_view *view) {
    unsigned before=writes;observe=true;
    enum kui_update_boot_result r=kui_update_boot_reload(compiled,transport,out,view,logger,cancel);
    observe=false;CHECK(writes==before);
    for(unsigned i=0;i<8u;i++)CHECK(!opened[i].file);
    CHECK(r==KUI_UPDATE_BOOT_CHAIN || r==KUI_UPDATE_BOOT_ERROR);
    CHECK((r==KUI_UPDATE_BOOT_CHAIN)==(out->data!=NULL));return r;
}
static void pinned_bank(const char *build) {
    char actual[192],expected[192];struct kui_update_runtime_stream stream;
    CHECK(kui_update_bank_path(build,"apps/games/retail-boot.kui",expected,sizeof(expected)));
    CHECK(kui_update_runtime_resolve(MODULE,actual,sizeof(actual),&stream) && !strcmp(actual,expected));
    CHECK(stream.pinned && !strcmp(stream.build,build) && stream.bytes==UF_FILE_BYTES);
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
static void handoff_tests(struct uf_package *a,struct uf_package *b) {
    struct kui_runtime_image image;struct kui_update_boot_view view;
    struct kui_update_handoff_context context={.payload_offset=128u};
    struct kui_update_handoff h;char runtime[192],path[192];
    CHECK(kui_update_bank_path(BUILD_A,"runtime.kui",runtime,sizeof(runtime)));
    selection(0,10u,a,"");remove_file(KUI_UPDATE_ACTIVE1);idle();
    CHECK(boot(ROOT_BUILD,KUI_STORAGE_SCI,&image,&view,false)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(bank_gui_bytes==UF_FILE_BYTES && bank_gui_opens==1u && !root_bytes);
    memcpy(context.wire,(uint8_t *)image.data+context.payload_offset,sizeof(context.wire));
    CHECK(kui_update_handoff_decode(context.wire,&h));
    CHECK(!strcmp(h.build,BUILD_A) && h.sequence==10u && h.transport==KUI_STORAGE_SCI &&
        h.payload_offset==context.payload_offset && h.file_bytes==UF_FILE_BYTES);
    CHECK(h.payload_crc32!=image.info.crc32 && h.memory_bytes==image.info.memory_bytes);
    CHECK(!kui_update_handoff_empty(image.data,image.info.payload_bytes,NULL));
    CHECK(kui_crc32(0,image.data,image.info.payload_bytes)==image.info.crc32);
    kui_runtime_free(&image);idle();
    CHECK(boot_handoff(BUILD_A,KUI_STORAGE_SCI,&context,&image,&view)==KUI_UPDATE_BOOT_PINNED);
    CHECK(bank_gui_bytes==64u+KUI_UPDATE_HANDOFF_BYTES && bank_gui_opens==1u && !root_bytes && !view.fallback);
    CHECK(kui_runtime_resolve_path(MODULE,path,sizeof(path)) && strstr(path,"/222222222222/"));
    /* Direct root/same-build entry has no checked transfer context. */
    idle();CHECK(boot(BUILD_A,KUI_STORAGE_SCI,&image,&view,false)==KUI_UPDATE_BOOT_PINNED);
    CHECK(bank_gui_bytes==UF_FILE_BYTES);
    for(unsigned kind=0;kind<12u;kind++) {
        struct kui_update_handoff_context bad=context;struct kui_update_handoff changed=h;
        switch(kind) {
        case 0:bad.wire[188]^=1u;break;
        case 1:bad.wire[148]^=1u;break;
        case 2:bad.payload_offset+=4u;break;
        case 3:changed.transport=KUI_STORAGE_IDE;break;
        case 4:++changed.sequence;break;
        case 5:changed.manifest_sha256[0]^=1u;break;
        case 6:changed.runtime_sha256[0]^=1u;break;
        case 7:strcpy(changed.build,BUILD_B);break;
        case 8:changed.file_bytes+=4u;changed.memory_bytes+=4u;break;
        case 9:++changed.payload_crc32;break;
        case 10:changed.memory_bytes+=4u;break;
        default:changed.payload_offset+=4u;bad.payload_offset+=4u;break;
        }
        if(kind>=3u)CHECK(kui_update_handoff_encode(bad.wire,&changed));
        idle();CHECK(boot_handoff(BUILD_A,KUI_STORAGE_SCI,&bad,&image,&view)==KUI_UPDATE_BOOT_PINNED);
        CHECK(bank_gui_bytes>=UF_FILE_BYTES && !root_opens && !view.fallback);
    }
    /* A saved filled RAM slot on disk cannot confer adoption. Neither does a
     * resealed changed header, or a receipt with a different manifest. */
    for(unsigned kind=0;kind<3u;kind++) {
        uint8_t altered[UF_FILE_BYTES];memcpy(altered,uf_file(a,0),sizeof(altered));
        if(kind==0u)memcpy(altered+64u+128u,context.wire,sizeof(context.wire));
        if(kind==1u) {uf_put32(altered+28u,768u);uf_put32(altered+60u,kui_crc32(0,altered,60u));}
        if(kind<2u)write_file(runtime,altered,sizeof(altered));
        else {
            struct uf_package other=uf_build(BUILD_A,1u);transport_marker(&other);
            uf_file(&other,1u)[64u+8u]^=1u;runtime_fix(&other,1u);
            snprintf(path,sizeof(path),KUI_UPDATE_SYSTEM "/%s/package.bin",BUILD_A);
            write_file(path,other.data,128u+128u*other.count);uf_free(&other);
        }
        idle();CHECK(boot_handoff(BUILD_A,KUI_STORAGE_SCI,&context,&image,&view)==KUI_UPDATE_BOOT_CHAIN);
        CHECK(view.fallback && !view.bank[0] && !strcmp(image.info.build,ROOT_BUILD));
        CHECK(!kui_runtime_resolve_path(MODULE,path,sizeof(path)));kui_runtime_free(&image);
        idle();seed(a,false);
    }
    /* The active selection is reread. An older RAM context cannot choose its
     * old bank over a valid newer bank, a conflict, or explicit baseline. */
    selection(1,20u,b,BUILD_A);idle();
    CHECK(boot_handoff(BUILD_A,KUI_STORAGE_SCI,&context,&image,&view)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(!strcmp(view.bank,BUILD_B) && bank_gui_bytes==UF_FILE_BYTES);
    struct kui_update_handoff next;
    CHECK(kui_update_handoff_decode((uint8_t *)image.data+128u,&next));
    CHECK(memcmp(h.nonce,next.nonce,sizeof(h.nonce)));kui_runtime_free(&image);
    selection(1,10u,b,BUILD_A);idle();
    CHECK(boot_handoff(BUILD_A,KUI_STORAGE_SCI,&context,&image,&view)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(view.fallback && !view.bank[0] && !bank_gui_bytes);kui_runtime_free(&image);
    selection(1,30u,NULL,BUILD_A);idle();
    CHECK(boot_handoff(BUILD_A,KUI_STORAGE_SCI,&context,&image,&view)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(!view.bank[0] && !bank_gui_bytes);kui_runtime_free(&image);
    /* Held B remains header-only in the baseline GUI even with an active
     * bank, and bank GUI recovery still chains to the retained baseline. */
    remove_file(KUI_UPDATE_ACTIVE1);idle();
    CHECK(boot(ROOT_BUILD,KUI_STORAGE_SCI,&image,&view,true)==KUI_UPDATE_BOOT_BASELINE);
    CHECK(root_bytes==64u && !bank_gui_bytes);
    idle();CHECK(boot(BUILD_A,KUI_STORAGE_SCI,&image,&view,true)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(!bank_gui_bytes);kui_runtime_free(&image);
    for(unsigned kind=0;kind<2u;kind++) {
        idle();
        if(kind)strcpy(cancel_read,runtime);
        else snprintf(cancel_read,sizeof(cancel_read),KUI_UPDATE_SYSTEM "/%s/package.bin",BUILD_A);
        CHECK(boot_handoff(BUILD_A,KUI_STORAGE_SCI,&context,&image,&view)==KUI_UPDATE_BOOT_ERROR);
        CHECK(!image.data && !kui_runtime_resolve_path(MODULE,path,sizeof(path)));
    }
    idle();
    printf("PASS bank GUI reads: dispatcher=%u bytes, adopted GUI=%u bytes (64-byte header + 192-byte EMPTY slot); direct entry fully checked\n",
        (unsigned)UF_FILE_BYTES,64u+KUI_UPDATE_HANDOFF_BYTES);
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
    CHECK(!kui_runtime_resolve_path("0:/KUI/data/unknown.kui",path,sizeof(path)));
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
        if(kind==0u) {altered[200]^=1u;uf_put32(altered+32,kui_crc32(0,altered+64,UF_PAYLOAD_BYTES));}
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
static void additive_tests(struct uf_package *root) {
    struct uf_package inherited=uf_build("444444444444",1u);transport_marker(&inherited);uf_inherit_modules(&inherited);
    idle();seed(&inherited,false);selection(0,100u,&inherited,BUILD_B);
    struct kui_runtime_image image;struct kui_update_boot_view view;char path[192];
    CHECK(boot(ROOT_BUILD,KUI_STORAGE_SCI,&image,&view,false)==KUI_UPDATE_BOOT_CHAIN);
    struct kui_update_handoff_context context={.payload_offset=128u};
    memcpy(context.wire,(uint8_t *)image.data+128u,sizeof(context.wire));kui_runtime_free(&image);idle();
    CHECK(boot_handoff("444444444444",KUI_STORAGE_SCI,&context,&image,&view)==KUI_UPDATE_BOOT_PINNED);
    CHECK(bank_gui_bytes==256u);
    struct kui_update_runtime_stream stream;
    CHECK(kui_update_runtime_resolve(MODULE,path,sizeof(path),&stream) && !strcmp(path,MODULE) && !stream.pinned);
    strcpy(path,MODULE);CHECK(kui_runtime_resolve_path(path,path,sizeof(path)) && !strcmp(path,MODULE));
    observe=true;CHECK(kui_runtime_read(MODULE,&image,logger,cancel)==KUI_RUNTIME_OK);observe=false;
    CHECK(!strcmp(image.info.build,ROOT_BUILD));kui_runtime_free(&image);
    /* Root inheritance is intentional sharing, including the root's absent
     * optional files. A leftover bank file must not turn absence into opt-in. */
    const char *optional="0:/KUI/apps/games/vmu-toy.kui";
    remove_file(optional);char bank_optional[192];
    CHECK(kui_update_bank_path("444444444444","apps/games/vmu-toy.kui",bank_optional,sizeof(bank_optional)));
    write_file(bank_optional,uf_file(root,0u),UF_FILE_BYTES);
    CHECK(kui_runtime_resolve_path(optional,path,sizeof(path)) && !strcmp(path,optional));
    FILINFO info;CHECK(f_stat(path,&info)==FR_NO_FILE);
    observe=true;CHECK(kui_runtime_read(optional,&image,logger,cancel)==KUI_RUNTIME_IO);observe=false;
    CHECK(!image.data);
    unsigned optional_index=0u;while(optional_index<root->count && strcmp(kui_update_catalog_path(optional_index),"apps/games/vmu-toy.kui"))++optional_index;
    CHECK(optional_index<root->count);write_file(optional,uf_file(root,optional_index),UF_FILE_BYTES);
    CHECK(kui_runtime_resolve_path(optional,path,sizeof(path)) && f_stat(path,&info)==FR_OK);
    /* The shared root still receives normal envelope/CRC validation. */
    unsigned module=0u;while(module<root->count && strcmp(kui_update_catalog_path(module),"apps/games/retail-boot.kui"))++module;
    CHECK(module<root->count);uint8_t altered[UF_FILE_BYTES];memcpy(altered,uf_file(root,module),sizeof(altered));altered[300u]^=1u;
    write_file(MODULE,altered,sizeof(altered));observe=true;
    CHECK(kui_runtime_read(MODULE,&image,logger,cancel)==KUI_RUNTIME_CHECKSUM);observe=false;
    write_file(MODULE,uf_file(root,module),UF_FILE_BYTES);
    /* A bank replacement beside inherited members remains bank-only. Its
     * absence must not quietly select the available retained root module. */
    struct uf_package mixed=uf_build("555555555555",1u);transport_marker(&mixed);uf_inherit_modules(&mixed);
    uint8_t *entry=uf_entry(&mixed,1u);entry[120u]=KUI_UPDATE_REPLACE;uf_put64(entry+80u,UF_FILE_BYTES);
    uf_hash(uf_file(&mixed,1u),UF_FILE_BYTES,entry+88u);mixed.bytes+=UF_FILE_BYTES;
    uf_put64(mixed.data+24u,2u*UF_FILE_BYTES);uf_manifest_hash(&mixed);
    seed(&mixed,false);selection(1,110u,&mixed,"444444444444");idle();
    CHECK(boot("555555555555",KUI_STORAGE_SCI,&image,&view,false)==KUI_UPDATE_BOOT_PINNED);
    const char *probe="0:/KUI/apps/games/probe.kui";
    CHECK(kui_update_runtime_resolve(probe,path,sizeof(path),&stream) && stream.pinned && strstr(path,"/555555555555/"));
    char replacement[192];strcpy(replacement,path);remove_file(replacement);
    CHECK(kui_runtime_resolve_path(probe,path,sizeof(path)) && !strcmp(path,replacement));
    observe=true;CHECK(kui_runtime_read(probe,&image,logger,cancel)==KUI_RUNTIME_IO);observe=false;
    CHECK(!image.data);
    idle();uf_free(&mixed);uf_free(&inherited);
}
static void reload_tests(struct uf_package *root,struct uf_package *a,struct uf_package *b) {
    struct kui_runtime_image image;struct kui_update_boot_view view;char path[192],newer[192],receipt[192];
    CHECK(kui_update_bank_path(BUILD_B,"runtime.kui",newer,sizeof(newer)));
    snprintf(receipt,sizeof(receipt),KUI_UPDATE_SYSTEM "/%s/package.bin",BUILD_B);
    idle();remove_file(KUI_UPDATE_ACTIVE0);remove_file(KUI_UPDATE_ACTIVE1);
    CHECK(boot(ROOT_BUILD,KUI_STORAGE_SCIF,&image,&view,false)==KUI_UPDATE_BOOT_BASELINE);
    idle();CHECK(reload_gui(ROOT_BUILD,KUI_STORAGE_SCI,&image,&view)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(!view.bank[0] && !strcmp(view.build,ROOT_BUILD) && !strcmp(image.info.build,ROOT_BUILD));
    CHECK(root_bytes>=UF_FILE_BYTES && !bank_gui_bytes);
    CHECK(kui_storage_boot_transport((const void *)((const uint8_t *)image.data+80u))==KUI_STORAGE_SCI);
    CHECK(!kui_runtime_resolve_path(MODULE,path,sizeof(path)));
    kui_runtime_free(&image);kui_update_boot_reload_abort();
    CHECK(kui_runtime_resolve_path(MODULE,path,sizeof(path)) && !strcmp(path,MODULE));
    /* A same-build GUI reload is a fresh complete read and executable chain,
     * with its normal RAM-only bank stamp, rather than a no-op pin. */
    selection(0,200u,a,"");idle();
    CHECK(boot(BUILD_A,KUI_STORAGE_SCIF,&image,&view,false)==KUI_UPDATE_BOOT_PINNED);pinned_bank(BUILD_A);
    idle();CHECK(reload_gui(BUILD_A,KUI_STORAGE_IDE,&image,&view)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(!strcmp(view.bank,BUILD_A) && !strcmp(image.info.build,BUILD_A) && bank_gui_bytes==UF_FILE_BYTES && !root_opens);
    CHECK(kui_storage_boot_transport((const void *)((const uint8_t *)image.data+80u))==KUI_STORAGE_IDE);
    struct kui_update_handoff h;
    CHECK(kui_update_handoff_decode((const uint8_t *)image.data+128u,&h));
    CHECK(!strcmp(h.build,BUILD_A) && h.sequence==200u && h.transport==KUI_STORAGE_IDE);
    CHECK(!kui_runtime_resolve_path(MODULE,path,sizeof(path)));
    kui_runtime_free(&image);kui_update_boot_reload_abort();pinned_bank(BUILD_A);
    /* Selection may change during the running GUI's session. Aborting a
     * checked new chain must restore its original A receipt, not B's pin. */
    selection(1,210u,b,BUILD_A);idle();
    CHECK(reload_gui(BUILD_A,KUI_STORAGE_SCI,&image,&view)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(!strcmp(view.bank,BUILD_B) && !strcmp(image.info.build,BUILD_B) && bank_gui_bytes==UF_FILE_BYTES);
    CHECK(kui_update_handoff_decode((const uint8_t *)image.data+128u,&h));
    CHECK(!strcmp(h.build,BUILD_B) && h.sequence==210u && h.transport==KUI_STORAGE_SCI);
    kui_runtime_free(&image);kui_update_boot_reload_abort();pinned_bank(BUILD_A);
    observe=true;unsigned before=writes;
    CHECK(kui_runtime_read(MODULE,&image,logger,cancel)==KUI_RUNTIME_OK);
    observe=false;CHECK(writes==before && !strcmp(image.info.build,BUILD_A));kui_runtime_free(&image);
    /* Logical receipt/runtime failures retain normal older-slot fallback;
     * each successful fallback is also a chain when its build is unchanged. */
    for(unsigned fault=0;fault<5u;fault++) {
        idle();seed(b,false);
        if(fault==0u)remove_file(newer);
        if(fault==1u)remove_file(receipt);
        if(fault==2u) {
            uint8_t altered[UF_FILE_BYTES];memcpy(altered,uf_file(b,0u),sizeof(altered));altered[450u]^=1u;
            uf_put32(altered+32u,kui_crc32(0,altered+64u,UF_PAYLOAD_BYTES));
            uf_put32(altered+60u,kui_crc32(0,altered,60u));write_file(newer,altered,sizeof(altered));
        }
        if(fault==3u)strcpy(read_fault,newer);
        if(fault==4u)strcpy(close_fault,receipt);
        CHECK(reload_gui(BUILD_A,KUI_STORAGE_SCI,&image,&view)==KUI_UPDATE_BOOT_CHAIN);
        CHECK(view.fallback && view.notice[0] && !strcmp(view.bank,BUILD_A) && !strcmp(image.info.build,BUILD_A));
        CHECK(kui_update_handoff_decode((const uint8_t *)image.data+128u,&h) && h.sequence==200u);
        kui_runtime_free(&image);kui_update_boot_reload_abort();pinned_bank(BUILD_A);
    }
    idle();seed(b,false);
    for(unsigned when=0;when<3u;when++) {
        idle();
        if(when==0u)cancel_now=true;
        if(when==1u)strcpy(cancel_read,receipt);
        if(when==2u)strcpy(cancel_read,newer);
        CHECK(reload_gui(BUILD_A,KUI_STORAGE_SCI,&image,&view)==KUI_UPDATE_BOOT_ERROR);
        CHECK(!image.data);pinned_bank(BUILD_A);
        /* No pending transfer is left behind after automatic restoration. */
        kui_update_boot_reload_abort();pinned_bank(BUILD_A);
    }
    idle();CHECK(reload_gui(BUILD_A,KUI_STORAGE_AUTO,&image,&view)==KUI_UPDATE_BOOT_ERROR);pinned_bank(BUILD_A);
    /* An explicit baseline selection and unavailable root never reactivate
     * the older bank. Failure restores the running A GUI's module resolver. */
    selection(1,220u,NULL,BUILD_A);remove_file(KUI_RUNTIME_PATH);idle();
    CHECK(reload_gui(BUILD_A,KUI_STORAGE_SCI,&image,&view)==KUI_UPDATE_BOOT_ERROR);pinned_bank(BUILD_A);
    idle();seed(root,true);
    CHECK(reload_gui(BUILD_A,KUI_STORAGE_SCI,&image,&view)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(!view.bank[0] && !strcmp(image.info.build,ROOT_BUILD));
    CHECK(kui_storage_boot_transport((const void *)((const uint8_t *)image.data+80u))==KUI_STORAGE_SCI);
    kui_runtime_free(&image);kui_update_boot_reload_abort();pinned_bank(BUILD_A);
    /* With both bank targets unavailable, ordinary boot's checked retained
     * root is the reload fallback, while an abort still restores A. */
    selection(1,230u,b,BUILD_A);remove_file(newer);
    CHECK(kui_update_bank_path(BUILD_A,"runtime.kui",path,sizeof(path)));remove_file(path);idle();
    CHECK(reload_gui(BUILD_A,KUI_STORAGE_SCIF,&image,&view)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(view.fallback && !view.bank[0] && !strcmp(image.info.build,ROOT_BUILD));
    kui_runtime_free(&image);kui_update_boot_reload_abort();pinned_bank(BUILD_A);
    idle();seed(a,false);seed(b,false);
}
static void subset_bank_tests(struct uf_package *root) {
    CHECK(kui_update_catalog_count()>=26u);
    struct uf_package old=uf_build("888888888888",1u);transport_marker(&old);
    /* Preserve the original26-role format exactly, with no new Smash rows.
     * Only the GUI is supplied; its existing receipt must remain valid. */
    memmove(old.data+128u+26u*128u,uf_file(&old,0u),26u*UF_FILE_BYTES);
    old.count=26u;old.bytes=128u+26u*(128u+UF_FILE_BYTES);
    uf_put32(old.data+16u,old.count);uf_put32(old.data+20u,old.count*128u);
    uf_put64(old.data+24u,(uint64_t)old.count*UF_FILE_BYTES);uf_manifest_hash(&old);uf_inherit_modules(&old);
    idle();seed(&old,false);selection(0,9000u,&old,BUILD_A);
    struct kui_runtime_image image;struct kui_update_boot_view view;
    CHECK(boot("888888888888",KUI_STORAGE_SCI,&image,&view,false)==KUI_UPDATE_BOOT_PINNED);
    const char *relative="apps/games/smash-vc2-vmu-helper.bin";
    const char *logical="0:/KUI/apps/games/smash-vc2-vmu-helper.bin";
    char resolved[192],shadow[192];struct kui_update_runtime_stream stream;
    /* The old26 default intentionally omits this later raw role. Seed an
     * explicit root fixture rather than adding a production catalog entry. */
    unsigned role=0u;while(role<root->count && strcmp(kui_update_catalog_path(role),relative))++role;
    bool added_helper=role==root->count;
    if(added_helper)write_file(logical,uf_file(root,0u),UF_FILE_BYTES);
    CHECK(kui_update_runtime_resolve(logical,resolved,sizeof(resolved),&stream) && !strcmp(resolved,logical) && !stream.pinned);
    observe=true;CHECK(kui_runtime_read(logical,&image,logger,cancel)==KUI_RUNTIME_OK);observe=false;
    CHECK(!strcmp(image.info.build,ROOT_BUILD));kui_runtime_free(&image);
    CHECK(kui_update_bank_path("888888888888",relative,shadow,sizeof(shadow)));
    write_file(shadow,uf_file(root,0u),UF_FILE_BYTES);remove_file(logical);
    CHECK(kui_runtime_resolve_path(logical,resolved,sizeof(resolved)) && !strcmp(resolved,logical));
    FILINFO info;CHECK(f_stat(resolved,&info)==FR_NO_FILE);
    observe=true;CHECK(kui_runtime_read(logical,&image,logger,cancel)==KUI_RUNTIME_IO);observe=false;
    write_file(logical,uf_file(root,added_helper?0u:role),UF_FILE_BYTES);
    CHECK(kui_runtime_resolve_path("0:/KUI/apps/games/not-allowed.bin",resolved,sizeof(resolved)) &&
        !strcmp(resolved,"0:/KUI/apps/games/not-allowed.bin"));
    /* A future software path needs no compiled catalog entry. Omission has
     * deliberate root-inheritance semantics; declaring it replaces only the
     * bank member and never falls back when that replacement goes missing. */
    const char *future="0:/KUI/apps/games/future-reader.kui";
    write_file(future,uf_file(root,1u),UF_FILE_BYTES);
    CHECK(kui_update_runtime_resolve(future,resolved,sizeof(resolved),&stream) && !strcmp(resolved,future) && !stream.pinned);
    struct uf_package all=uf_build("999999999999",1u);transport_marker(&all);
    struct uf_package sparse={.count=2u,.bytes=128u+2u*(128u+UF_FILE_BYTES)};
    sparse.data=calloc(1u,sparse.bytes);CHECK(sparse.data);memcpy(sparse.data,all.data,128u);
    memcpy(uf_entry(&sparse,0u),uf_entry(&all,0u),128u);memcpy(uf_entry(&sparse,1u),uf_entry(&all,1u),128u);
    memset(uf_entry(&sparse,1u),0,KUI_UPDATE_PATH_BYTES);strcpy((char *)uf_entry(&sparse,1u),"apps/games/future-reader.kui");
    memcpy(uf_file(&sparse,0u),uf_file(&all,0u),UF_FILE_BYTES);memcpy(uf_file(&sparse,1u),uf_file(&all,1u),UF_FILE_BYTES);
    uf_put32(sparse.data+16u,2u);uf_put32(sparse.data+20u,256u);uf_put64(sparse.data+24u,2u*UF_FILE_BYTES);uf_manifest_hash(&sparse);
    write_file("0:/KUI/updates/future-role.kup",sparse.data,sparse.bytes);
    struct kui_update_package *installed=malloc(sizeof(*installed));CHECK(installed);
    struct kui_app_status status;struct kui_update_pin pin;
    CHECK(kui_update_inspect("0:/KUI/updates/future-role.kup",installed,&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    kui_update_package_pin(installed,&pin);
    CHECK(kui_update_install("0:/KUI/updates/future-role.kup",&pin,&status,logger,cancel,NULL)==KUI_UPDATE_OK && status.passed);
    CHECK(kui_update_bank_verify("999999999999",installed,&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    CHECK(installed->count==2u && kui_update_package_find(installed,"apps/games/future-reader.kui"));
    for(unsigned i=0u;i<root->count;i++) {
        char root_path[192];snprintf(root_path,sizeof(root_path),"0:/KUI/%s",kui_update_catalog_path(i));
        FIL file;UINT got=0u;uint8_t bytes[UF_FILE_BYTES];CHECK(f_open(&file,root_path,FA_READ)==FR_OK);
        CHECK(f_size(&file)==UF_FILE_BYTES && f_read(&file,bytes,sizeof(bytes),&got)==FR_OK && got==sizeof(bytes));
        CHECK(!memcmp(bytes,uf_file(root,i),sizeof(bytes)) && f_close(&file)==FR_OK);
    }
    free(installed);idle();
    CHECK(boot("999999999999",KUI_STORAGE_SCI,&image,&view,false)==KUI_UPDATE_BOOT_PINNED);
    CHECK(kui_update_runtime_resolve(future,resolved,sizeof(resolved),&stream) && stream.pinned && strstr(resolved,"/999999999999/"));
    observe=true;CHECK(kui_runtime_read(future,&image,logger,cancel)==KUI_RUNTIME_OK);observe=false;
    CHECK(!strcmp(image.info.build,"999999999999"));kui_runtime_free(&image);remove_file(resolved);
    observe=true;CHECK(kui_runtime_read(future,&image,logger,cancel)==KUI_RUNTIME_IO);observe=false;
    CHECK(!image.data);idle();
    if(added_helper)remove_file(logical);
    uf_free(&sparse);uf_free(&all);uf_free(&old);
}
static void withdrawn_tests(void) {
    static const char *const builds[]={"28cadccf0e71","33b04057db86"};
    for(unsigned i=0u;i<2u;i++) {
        struct uf_package p=uf_build(builds[i],1u);transport_marker(&p);
        char path[192];CHECK(kui_update_bank_path(builds[i],"runtime.kui",path,sizeof(path)));
        write_file(path,uf_file(&p,0u),UF_FILE_BYTES);
        if(i==1u)write_file(ROOT_SWAP,uf_file(&p,0u),UF_FILE_BYTES);
        snprintf(path,sizeof(path),KUI_UPDATE_SYSTEM "/%s/package.bin",builds[i]);
        write_file(path,p.data,128u+128u*p.count);selection(i,1000u+i,&p,builds[i^1u]);uf_free(&p);
    }
    struct kui_runtime_image image;struct kui_update_boot_view view;char path[192];idle();
    CHECK(boot(ROOT_BUILD,KUI_STORAGE_SCI,&image,&view,false)==KUI_UPDATE_BOOT_BASELINE);
    CHECK(view.fallback && !view.bank[0] && root_bytes==64u && !bank_gui_bytes);
    CHECK(kui_runtime_resolve_path(MODULE,path,sizeof(path)) && !strcmp(path,MODULE));
    struct kui_update_activation selected;unsigned slot=0u;
    CHECK(kui_update_activation_latest(&selected,&slot,cancel)==KUI_UPDATE_OK && selected.sequence==1001u &&
        !strcmp(selected.selected,builds[1]));
    /* The small header inspection and later full image are separate opens.
     * A valid-CRC withdrawn GUI substituted on the latter must not chain. */
    idle();root_reread_swap=true;
    CHECK(boot(BUILD_A,KUI_STORAGE_SCI,&image,&view,false)==KUI_UPDATE_BOOT_ERROR);
    CHECK(!image.data && root_opens==2u && !kui_runtime_resolve_path(MODULE,path,sizeof(path)));
    /* No manual deletion or boot-time rewrite: the installer can still read
     * this exact old sequence and publish the next safe selection. */
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *input=fopen(argv[1],"rb");CHECK(input && !fseek(input,0,SEEK_END));long bytes=ftell(input);
    CHECK(bytes>0 && !(bytes%512) && !fseek(input,0,SEEK_SET));disk_bytes=(size_t)bytes;disk=malloc(disk_bytes);
    CHECK(disk && fread(disk,1,disk_bytes,input)==disk_bytes && !fclose(input));
    kui_media_set(&media);
    CHECK(f_mount(&fs,"0:",1)==FR_OK);
    struct uf_package root=uf_build(ROOT_BUILD,0u),a=uf_build(BUILD_A,0u),b=uf_build(BUILD_B,1u);
    transport_marker(&root);transport_marker(&a);transport_marker(&b);seed(&root,true);seed(&a,false);seed(&b,false);
    baseline_tests(&root);handoff_tests(&a,&b);bank_tests(&a,&b);additive_tests(&root);reload_tests(&root,&a,&b);subset_bank_tests(&root);withdrawn_tests();idle();kui_update_boot_reset();
    CHECK(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);uf_free(&root);uf_free(&a);uf_free(&b);free(disk);
    printf("PASS update boot/resolver: %u checks; header-only baseline, checked chain/transport, older-slot fallback, pinned same-stream module SHA, no root mixing or boot writes\n",checks);
    return 0;
}
