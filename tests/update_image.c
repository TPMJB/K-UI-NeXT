/* SPDX-License-Identifier: GPL-3.0-only */
/* Production updater/selection codecs, runtime admission and FatFs. Only the
 * block device is synthetic; every fault cut starts from one real FS image. */
#include "update_fixture.h"
#include "kui/media.h"
#include "ff.h"
#include <stdio.h>
static unsigned checks,writes,syncs;
static uint8_t *disk;static size_t disk_bytes;static FATFS fs;
static bool active,cancel_before,low_space,tear_activation,torn_activation,activation_read_fail;
static bool inherit_observe;static unsigned inherited_opens;
static int fail_write=-1,fail_sync=-1,cancel_after=-1;
static FIL *activation_file;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Updater FS failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
#define PACKAGE_A "0:/KUI/updates/a.kup"
#define PACKAGE_B "0:/KUI/updates/b.kup"
#define BUILD_A "222222222222"
#define BUILD_B "333333333333"
static uint64_t blocks(void *ctx) {(void)ctx;return disk_bytes/512u;}
static int read_blocks(void *ctx,uint32_t block,size_t count,uint8_t *out) {
    (void)ctx;if((uint64_t)block+count>blocks(NULL))return -1;
    memcpy(out,disk+(size_t)block*512u,count*512u);return 0;
}
static int write_blocks(void *ctx,uint32_t block,size_t count,const uint8_t *in) {
    (void)ctx;if((uint64_t)block+count>blocks(NULL))return -1;
    if(active) {unsigned call=writes++;if(fail_write>=0 && call>=(unsigned)fail_write)return -1;}
    memcpy(disk+(size_t)block*512u,in,count*512u);return 0;
}
static int sync_blocks(void *ctx) {
    (void)ctx;if(active) {unsigned call=syncs++;if(fail_sync>=0 && call>=(unsigned)fail_sync)return -1;}return 0;
}
static const struct kui_media_ops media={NULL,blocks,read_blocks,write_blocks,sync_blocks};
uint32_t kui_clock_fattime(void) {return ((2026u-1980u)<<25)|(11u<<21)|(4u<<16);}
static void logger(const char *format,...) {(void)format;}
static bool cancel(void) {return cancel_before || (cancel_after>=0 && writes>=(unsigned)cancel_after);}
static void mount_volume(void) {kui_media_set(&media);CHECK(f_mount(&fs,"0:",1)==FR_OK);}
static void unmount_volume(void) {CHECK(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);}
static bool system_path(const char *path) {return !strncmp(path,KUI_UPDATE_SYSTEM "/",sizeof(KUI_UPDATE_SYSTEM));}
FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __wrap_f_open(FIL *f,const TCHAR *path,BYTE flags) {
    if(active && (flags&FA_WRITE)) CHECK(system_path(path));
    if(active && inherit_observe && strstr(path,"/apps/"))++inherited_opens;
    if(active && activation_read_fail && !(flags&FA_WRITE) && strstr(path,"/active"))return FR_DISK_ERR;
    FRESULT r=__real_f_open(f,path,flags);
    if(active && r==FR_OK && (flags&FA_WRITE) && strstr(path,"/active"))activation_file=f;
    return r;
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *f) {if(activation_file==f)activation_file=NULL;return __real_f_close(f);}
FRESULT __real_f_write(FIL *,const void *,UINT,UINT *);
FRESULT __wrap_f_write(FIL *f,const void *data,UINT bytes,UINT *put) {
    if(active && tear_activation && !torn_activation && activation_file==f && bytes==128u) {
        torn_activation=true;FRESULT r=__real_f_write(f,data,64u,put);return r==FR_OK?FR_DISK_ERR:r;
    }
    return __real_f_write(f,data,bytes,put);
}
FRESULT __real_f_getfree(const TCHAR *,DWORD *,FATFS **);
FRESULT __wrap_f_getfree(const TCHAR *path,DWORD *clusters,FATFS **volume) {
    FRESULT r=__real_f_getfree(path,clusters,volume);if(active && low_space && r==FR_OK)*clusters=0u;return r;
}
static void directory(const char *path) {
    FRESULT r=f_mkdir(path);CHECK(r==FR_OK || r==FR_EXIST);
}
static void parents(const char *path) {
    char copy[256];CHECK(strlen(path)<sizeof(copy));strcpy(copy,path);
    for(char *p=copy+3u;*p;p++)if(*p=='/') {*p=0;directory(copy);*p='/';}
}
static void file_write(const char *path,const void *data,size_t bytes) {
    parents(path);FIL f;UINT done=0;CHECK(f_open(&f,path,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
    CHECK(f_write(&f,data,(UINT)bytes,&done)==FR_OK && done==bytes);CHECK(f_close(&f)==FR_OK);
}
static void file_equal(const char *path,const void *expected,size_t bytes) {
    FIL f;UINT got=0;uint8_t *actual=malloc(bytes?bytes:1u);CHECK(actual);
    CHECK(f_open(&f,path,FA_READ)==FR_OK && f_size(&f)==bytes);
    CHECK(f_read(&f,actual,(UINT)bytes,&got)==FR_OK && got==bytes && !memcmp(actual,expected,bytes));
    CHECK(f_close(&f)==FR_OK);free(actual);
}
static struct uf_package baseline;
static const char *const protected_paths[]={"0:/KUI/vmu/cards/v0001/card.vmd","0:/KUI/vmu-test/p0005/card.journal",
    "0:/KUI/prefs/vmu.bin","0:/Games/untouched/disc.gdi","0:/KUI/logs/previous.txt"};
static uint8_t sentinel[1024];
static void protected_equal(void) {
    char path[256];for(unsigned i=0;i<baseline.count;i++) {
        snprintf(path,sizeof(path),"0:/KUI/%s",kui_update_catalog_path(i));file_equal(path,uf_file(&baseline,i),UF_FILE_BYTES);
    }
    for(unsigned i=0;i<sizeof(protected_paths)/sizeof(protected_paths[0]);i++)file_equal(protected_paths[i],sentinel,sizeof(sentinel));
}
static void seed(struct uf_package *a,struct uf_package *b) {
    baseline=uf_build("111111111111",0u);char path[256];
    for(unsigned i=0;i<baseline.count;i++) {
        snprintf(path,sizeof(path),"0:/KUI/%s",kui_update_catalog_path(i));file_write(path,uf_file(&baseline,i),UF_FILE_BYTES);
    }
    for(unsigned i=0;i<sizeof(sentinel);i++)sentinel[i]=(uint8_t)(i*71u+23u);
    for(unsigned i=0;i<sizeof(protected_paths)/sizeof(protected_paths[0]);i++)file_write(protected_paths[i],sentinel,sizeof(sentinel));
    file_write(PACKAGE_A,a->data,a->bytes);file_write(PACKAGE_B,b->data,b->bytes);
}
static enum kui_update_result inspect(const char *path,struct kui_update_package *p) {
    struct kui_app_status s;active=true;enum kui_update_result r=kui_update_inspect(path,p,&s,logger,cancel,NULL);active=false;
    CHECK(s.complete && s.passed==(r==KUI_UPDATE_OK));if(r==KUI_UPDATE_OK)CHECK(s.done==s.total);return r;
}
static enum kui_update_result install(const char *path,const struct kui_update_pin *pin) {
    struct kui_app_status s;active=true;enum kui_update_result r=kui_update_install(path,pin,&s,logger,cancel,NULL);active=false;
    CHECK(s.complete && s.passed==(r==KUI_UPDATE_OK));if(r==KUI_UPDATE_OK)CHECK(s.done==s.total);return r;
}
static struct kui_update_activation latest(void) {
    struct kui_update_activation a={0};unsigned slot=0;
    enum kui_update_result r=kui_update_activation_latest(&a,&slot,cancel);
    CHECK(r==KUI_UPDATE_OK || r==KUI_UPDATE_NOT_FOUND);if(r==KUI_UPDATE_NOT_FOUND)memset(&a,0,sizeof(a));return a;
}
static void faults_clear(void) {
    active=cancel_before=low_space=tear_activation=torn_activation=activation_read_fail=false;
    fail_write=fail_sync=cancel_after=-1;writes=syncs=0;activation_file=NULL;
}
static void runtime_fix(struct uf_package *p,unsigned i) {
    uint8_t *f=uf_file(p,i);uf_put32(f+32u,kui_crc32(0,f+64u,UF_PAYLOAD_BYTES));uf_put32(f+60u,kui_crc32(0,f,60u));uf_file_hash(p,i);
}
static void inspect_refusals(struct uf_package *good,const struct kui_update_pin *good_pin) {
    uint8_t *saved=malloc(good->bytes);CHECK(saved);memcpy(saved,good->data,good->bytes);
    struct kui_update_package p;unsigned gui=0;while(gui<good->count && strcmp(kui_update_catalog_path(gui),"runtime.kui"))++gui;CHECK(gui<good->count);
    for(unsigned fault=0;fault<10u;fault++) {
        memcpy(good->data,saved,good->bytes);uint8_t *f=uf_file(good,gui);
        if(fault==0u)f[200u]^=1u;
        if(fault==1u) {f[64u+32u]^=1u;runtime_fix(good,gui);}
        if(fault==2u) {memcpy(f+64u+80u,f+64u+32u,16u);runtime_fix(good,gui);}
        if(fault==3u) {memcpy(f+40u,"ffffffffffff",12u);uf_put32(f+60u,kui_crc32(0,f,60u));uf_file_hash(good,gui);}
        if(fault==4u) {f[32u]^=1u;uf_put32(f+60u,kui_crc32(0,f,60u));uf_file_hash(good,gui);}
        if(fault==5u)good->data[128u]^=1u;
        if(fault==6u) {
            CHECK(good->count>1u);uint8_t *module=uf_file(good,1u);memcpy(module+40u,"ffffffffffff",12u);
            uf_put32(module+60u,kui_crc32(0,module,60u));uf_file_hash(good,1u);
        }
        if(fault==7u) {f[64u+128u]^=1u;runtime_fix(good,gui);}
        if(fault==8u) {memcpy(f+64u+320u,f+64u+128u,KUI_UPDATE_HANDOFF_BYTES);runtime_fix(good,gui);}
        if(fault==9u) {f[64u+128u+16u]=1u;runtime_fix(good,gui);}
        file_write(PACKAGE_B,good->data,good->bytes);faults_clear();CHECK(inspect(PACKAGE_B,&p)!=KUI_UPDATE_OK);CHECK(!writes);
    }
    memcpy(good->data,saved,good->bytes);uf_file(good,gui)[400u]^=1u;runtime_fix(good,gui);
    file_write(PACKAGE_B,good->data,good->bytes);faults_clear();CHECK(install(PACKAGE_B,good_pin)==KUI_UPDATE_STALE);CHECK(!writes);
    memcpy(good->data,saved,good->bytes);file_write(PACKAGE_B,good->data,good->bytes);free(saved);
    faults_clear();cancel_before=true;CHECK(inspect(PACKAGE_B,&p)==KUI_UPDATE_CANCELLED);cancel_before=false;CHECK(!writes);
    faults_clear();low_space=true;CHECK(install(PACKAGE_B,good_pin)==KUI_UPDATE_SPACE);low_space=false;CHECK(!writes);
    protected_equal();
}
static void verify_bank(const char *build) {
    struct kui_update_package p;struct kui_app_status status;
    CHECK(kui_update_bank_verify(build,&p,&status,logger,cancel,NULL)==KUI_UPDATE_OK && status.passed);
    CHECK(!strcmp(p.build,build));
}
static enum kui_update_result space(const struct kui_update_package *p,uint64_t *needed,uint64_t *available) {
    active=true;enum kui_update_result r=kui_update_plan_space(p,needed,available,cancel);active=false;return r;
}
static void fill_free_to(uint64_t leave,bool append) {
    FATFS *volume=NULL;DWORD clusters=0u;CHECK(f_getfree("0:",&clusters,&volume)==FR_OK && volume);
    uint64_t cluster=(uint64_t)volume->csize*512u,available=(uint64_t)clusters*cluster;
    CHECK(leave<=available && leave%cluster==0u);
    FIL file;CHECK(f_open(&file,"0:/KUI/updates/filler.bin",FA_WRITE|(append?FA_OPEN_APPEND:FA_CREATE_ALWAYS))==FR_OK);
    uint8_t *buffer=malloc(65536u);CHECK(buffer);memset(buffer,0x5au,65536u);
    for(uint64_t remaining=available-leave;remaining;) {
        UINT bytes=remaining<65536u?(UINT)remaining:65536u,done=0u;
        CHECK(f_write(&file,buffer,bytes,&done)==FR_OK && done==bytes);remaining-=bytes;
    }
    CHECK(f_close(&file)==FR_OK);free(buffer);
    CHECK(f_getfree("0:",&clusters,&volume)==FR_OK && (uint64_t)clusters*cluster==leave);
}
static void retry_space_cases(struct uf_package *a,struct uf_package *b,const struct kui_update_pin *pb,
    const uint8_t *snapshot,const char *old_path,const uint8_t old_wire[128]) {
    memcpy(disk,snapshot,disk_bytes);mount_volume();faults_clear();
    struct kui_update_package p;CHECK(uf_decode(b,&p)==KUI_UPDATE_OK);
    uint64_t fresh=0u,available=0u,partial=0u,partial_available=0u;
    CHECK(space(&p,&fresh,&available)==KUI_UPDATE_OK && fresh && !writes);
    const uint64_t cluster=(uint64_t)fs.csize*512u;
    char path[160];
    for(unsigned i=0;i<6u;i++) {
        CHECK(kui_update_bank_path(BUILD_B,kui_update_catalog_path(i),path,sizeof(path)));
        file_write(path,uf_file(b,i),UF_FILE_BYTES/2u);
    }
    directory(KUI_UPDATE_SYSTEM "/" BUILD_B "/apps/disc");
    file_write(KUI_UPDATE_SYSTEM "/" BUILD_B "/package.bin","INTERRUPTED",11u);
    CHECK(space(&p,&partial,&partial_available)==KUI_UPDATE_OK);
    CHECK(partial<fresh);CHECK(partial_available<available);CHECK(!writes);
    const char *unknown=KUI_UPDATE_SYSTEM "/" BUILD_B "/personal.txt";
    file_write(unknown,sentinel,sizeof(sentinel));
    uint64_t with_unknown=0u,unknown_available=0u;
    CHECK(space(&p,&with_unknown,&unknown_available)==KUI_UPDATE_OK && with_unknown==partial && unknown_available<partial_available);
    /* Actual allocation leaves enough for the credited retry, but less than a
     * fresh bank. Unknown files are neither credited nor removed. */
    CHECK(partial+2u*cluster<fresh);fill_free_to(partial+2u*cluster,false);
    CHECK(space(&p,&with_unknown,&unknown_available)==KUI_UPDATE_OK && with_unknown==partial && unknown_available<fresh);
    faults_clear();CHECK(install(PACKAGE_B,pb)==KUI_UPDATE_OK);verify_bank(BUILD_B);
    CHECK(!strcmp(latest().selected,BUILD_B) && !strcmp(latest().previous,BUILD_A));
    file_equal(old_path,old_wire,128u);file_equal(unknown,sentinel,sizeof(sentinel));protected_equal();
    /* A complete selected bank is genuinely read-only: both real free space
     * and the reported planning requirement are zero. */
    fill_free_to(0u,true);faults_clear();
    CHECK(space(&p,&with_unknown,&unknown_available)==KUI_UPDATE_OK && !with_unknown && !unknown_available && !writes);
    CHECK(install(PACKAGE_B,pb)==KUI_UPDATE_OK && !writes);verify_bank(BUILD_B);
    file_equal(unknown,sentinel,sizeof(sentinel));protected_equal();unmount_volume();
    /* A wrong-type controlled leaf and an incomplete selected bank must stay
     * protected; neither case grants retry cleanup authority. */
    memcpy(disk,snapshot,disk_bytes);mount_volume();faults_clear();
    CHECK(kui_update_bank_path(BUILD_B,kui_update_catalog_path(0u),path,sizeof(path)));parents(path);directory(path);
    CHECK(space(&p,&with_unknown,&unknown_available)==KUI_UPDATE_PROTECTED && !writes);
    CHECK(install(PACKAGE_B,pb)==KUI_UPDATE_PROTECTED && !writes);
    CHECK(!strcmp(latest().selected,BUILD_A));protected_equal();unmount_volume();
    memcpy(disk,snapshot,disk_bytes);mount_volume();faults_clear();
    CHECK(f_unlink(KUI_UPDATE_SYSTEM "/" BUILD_A "/package.bin")==FR_OK);
    CHECK(uf_decode(a,&p)==KUI_UPDATE_OK);
    CHECK(space(&p,&with_unknown,&unknown_available)==KUI_UPDATE_PROTECTED && !writes);
    struct kui_update_pin pa;kui_update_package_pin(&p,&pa);
    CHECK(install(PACKAGE_A,&pa)==KUI_UPDATE_PROTECTED && !writes);
    CHECK(!strcmp(latest().selected,BUILD_A));file_equal(old_path,old_wire,128u);protected_equal();unmount_volume();
}
static void additive_cases(void) {
    struct uf_package p=uf_build("444444444444",1u);uf_inherit_modules(&p);
    struct kui_update_package decoded;CHECK(uf_decode(&p,&decoded)==KUI_UPDATE_OK);
    char path[192],leftover[192];FILINFO info;
    static const char *const withdrawn[]={"28cadccf0e71","33b04057db86"};
    for(unsigned i=0u;i<2u;i++) {
        struct uf_package old=uf_build(withdrawn[i],1u);
        file_write("0:/KUI/updates/withdrawn.kup",old.data,old.bytes);faults_clear();
        struct kui_update_package rejected;CHECK(inspect("0:/KUI/updates/withdrawn.kup",&rejected)!=KUI_UPDATE_OK && !writes);
        uf_free(&old);
    }
    /* Initial GUI copy must recover while old activation records remain on
     * disk. Preserve their sequence and do not offer the withdrawn GUI as
     * the new candidate's previous version. */
    struct kui_update_activation old_selection={.sequence=900u};
    strcpy(old_selection.selected,withdrawn[1]);strcpy(old_selection.previous,withdrawn[0]);
    old_selection.manifest_sha256[0]=0x42u;uint8_t old_wire[128];
    CHECK(kui_update_activation_encode(&old_selection,old_wire));file_write(KUI_UPDATE_ACTIVE0,old_wire,sizeof(old_wire));
    /* The absent optional loader must stay absent. A leftover bank-side file
     * covered by INHERIT_ROOT is not a replacement or cleanup destination. */
    const char *absent="0:/KUI/apps/games/cdda-toy-raw.kui";
    CHECK(f_unlink(absent)==FR_OK);
    CHECK(kui_update_bank_path(decoded.build,"apps/games/cdda-toy-raw.kui",leftover,sizeof(leftover)));
    file_write(leftover,sentinel,sizeof(sentinel));
    file_write("0:/KUI/updates/additive.kup",p.data,p.bytes);
    faults_clear();inherit_observe=true;inherited_opens=0u;
    CHECK(inspect("0:/KUI/updates/additive.kup",&decoded)==KUI_UPDATE_OK && !writes && !inherited_opens);
    uint64_t needed=0u,available=0u;CHECK(space(&decoded,&needed,&available)==KUI_UPDATE_OK && needed && !writes);
    struct kui_update_pin pin;kui_update_package_pin(&decoded,&pin);
    CHECK(install("0:/KUI/updates/additive.kup",&pin)==KUI_UPDATE_OK && writes && !inherited_opens);
    CHECK(!strcmp(latest().selected,"444444444444") && !latest().previous[0] && latest().sequence==901u);
    file_equal(KUI_UPDATE_ACTIVE0,old_wire,sizeof(old_wire));
    faults_clear();struct kui_app_status status;active=true;
    CHECK(kui_update_bank_verify("444444444444",&decoded,&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    active=false;CHECK(!writes && !inherited_opens);
    CHECK(f_stat(absent,&info)==FR_NO_FILE);file_equal(leftover,sentinel,sizeof(sentinel));
    for(unsigned i=1u;i<p.count;i++) {
        if(!strcmp(kui_update_catalog_path(i),"apps/games/cdda-toy-raw.kui"))continue;
        snprintf(path,sizeof(path),"0:/KUI/%s",kui_update_catalog_path(i));file_equal(path,uf_file(&baseline,i),UF_FILE_BYTES);
        CHECK(kui_update_bank_path(decoded.build,kui_update_catalog_path(i),path,sizeof(path)));
        CHECK(f_stat(path,&info)==FR_NO_FILE);
    }
    faults_clear();CHECK(install("0:/KUI/updates/additive.kup",&pin)==KUI_UPDATE_OK && !writes && !inherited_opens);
    /* An existing previous-version field naming a withdrawn GUI restores the
     * checked root baseline instead, without deleting activation records. */
    old_selection=latest();old_selection.sequence=902u;strcpy(old_selection.previous,withdrawn[1]);
    CHECK(kui_update_activation_encode(&old_selection,old_wire));file_write(KUI_UPDATE_ACTIVE0,old_wire,sizeof(old_wire));
    faults_clear();active=true;CHECK(kui_update_rollback(&status,logger,cancel,NULL)==KUI_UPDATE_OK);active=false;
    CHECK(!latest().selected[0] && !strcmp(latest().previous,"444444444444") && latest().sequence==903u && !inherited_opens);
    inherit_observe=false;
    /* Root/user bytes remain protected; restoring the deliberately absent
     * generated fixture is test setup, never updater work. */
    unsigned index=0u;while(index<baseline.count && strcmp(kui_update_catalog_path(index),"apps/games/cdda-toy-raw.kui"))++index;
    CHECK(index<baseline.count);file_write(absent,uf_file(&baseline,index),UF_FILE_BYTES);protected_equal();uf_free(&p);
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *input=fopen(argv[1],"rb");CHECK(input && !fseek(input,0,SEEK_END));long n=ftell(input);
    CHECK(n>0 && !fseek(input,0,SEEK_SET));disk_bytes=(size_t)n;disk=malloc(disk_bytes);
    CHECK(disk && fread(disk,1,disk_bytes,input)==disk_bytes && !fclose(input));mount_volume();
    struct uf_package a=uf_build(BUILD_A,0u),b=uf_build(BUILD_B,1u);seed(&a,&b);faults_clear();
    struct kui_update_package p;CHECK(inspect(PACKAGE_A,&p)==KUI_UPDATE_OK && !writes);
    struct kui_update_pin pa,pb;kui_update_package_pin(&p,&pa);CHECK(inspect(PACKAGE_B,&p)==KUI_UPDATE_OK && !writes);kui_update_package_pin(&p,&pb);
    inspect_refusals(&b,&pb);faults_clear();CHECK(install(PACKAGE_A,&pa)==KUI_UPDATE_OK);
    struct kui_update_activation selected=latest();CHECK(!strcmp(selected.selected,BUILD_A) && !selected.previous[0]);verify_bank(BUILD_A);protected_equal();
    /* Keep the exact old valid selector and root cohort while publishing B. */
    unsigned old_slot=0;CHECK(kui_update_activation_latest(&selected,&old_slot,cancel)==KUI_UPDATE_OK);
    uint8_t old_wire[128];CHECK(kui_update_activation_encode(&selected,old_wire));
    char old_path[64];snprintf(old_path,sizeof(old_path),"0:/KUI/system/active%u.bin",old_slot);file_equal(old_path,old_wire,sizeof(old_wire));
    unmount_volume();uint8_t *snapshot=malloc(disk_bytes);CHECK(snapshot);memcpy(snapshot,disk,disk_bytes);mount_volume();
    faults_clear();CHECK(install(PACKAGE_B,&pb)==KUI_UPDATE_OK);unsigned full_writes=writes,full_syncs=syncs;
    CHECK(full_writes && full_syncs && full_writes<1000u && full_syncs<1000u);selected=latest();
    CHECK(!strcmp(selected.selected,BUILD_B) && !strcmp(selected.previous,BUILD_A));verify_bank(BUILD_B);protected_equal();
    struct kui_app_status status;CHECK(kui_update_rollback(&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    CHECK(!strcmp(latest().selected,BUILD_A));CHECK(kui_update_select("",NULL,&status,logger,cancel,NULL)==KUI_UPDATE_OK);CHECK(!latest().selected[0]);protected_equal();
    unmount_volume();
    for(unsigned type=0;type<2u;type++)for(unsigned cut=0;cut<=(type?full_syncs:full_writes);cut++) {
        memcpy(disk,snapshot,disk_bytes);mount_volume();faults_clear();if(type)fail_sync=(int)cut;else fail_write=(int)cut;
        enum kui_update_result result=install(PACKAGE_B,&pb);faults_clear();unmount_volume();mount_volume();
        file_equal(old_path,old_wire,sizeof(old_wire));selected=latest();
        CHECK(!strcmp(selected.selected,BUILD_A) || !strcmp(selected.selected,BUILD_B));
        if(result==KUI_UPDATE_OK)CHECK(!strcmp(selected.selected,BUILD_B));
        verify_bank(selected.selected);protected_equal();unmount_volume();
    }
    memcpy(disk,snapshot,disk_bytes);mount_volume();faults_clear();tear_activation=true;
    CHECK(install(PACKAGE_B,&pb)!=KUI_UPDATE_OK && torn_activation);faults_clear();unmount_volume();mount_volume();
    CHECK(!strcmp(latest().selected,BUILD_A));file_equal(old_path,old_wire,sizeof(old_wire));protected_equal();unmount_volume();
    memcpy(disk,snapshot,disk_bytes);mount_volume();faults_clear();cancel_after=1;CHECK(install(PACKAGE_B,&pb)==KUI_UPDATE_CANCELLED);
    faults_clear();unmount_volume();mount_volume();CHECK(!strcmp(latest().selected,BUILD_A));file_equal(old_path,old_wire,sizeof(old_wire));protected_equal();unmount_volume();
    retry_space_cases(&a,&b,&pb,snapshot,old_path,old_wire);
    /* Selection admission fails closed before any new bank writes. */
    memcpy(disk,snapshot,disk_bytes);mount_volume();faults_clear();activation_read_fail=true;
    CHECK(install(PACKAGE_B,&pb)==KUI_UPDATE_IO && !writes);faults_clear();
    CHECK(!strcmp(latest().selected,BUILD_A));protected_equal();unmount_volume();
    memcpy(disk,snapshot,disk_bytes);mount_volume();faults_clear();selected=latest();selected.sequence=UINT64_MAX;
    uint8_t edge_wire[128];CHECK(kui_update_activation_encode(&selected,edge_wire));file_write(old_path,edge_wire,sizeof(edge_wire));
    CHECK(install(PACKAGE_B,&pb)==KUI_UPDATE_SEQUENCE && !writes);
    CHECK(latest().sequence==UINT64_MAX);file_equal(old_path,edge_wire,sizeof(edge_wire));protected_equal();unmount_volume();
    memcpy(disk,snapshot,disk_bytes);mount_volume();faults_clear();selected=latest();
    struct kui_update_activation conflicting={.sequence=selected.sequence};strcpy(conflicting.previous,BUILD_A);
    CHECK(kui_update_activation_encode(&conflicting,edge_wire));char other_path[64];
    snprintf(other_path,sizeof(other_path),"0:/KUI/system/active%u.bin",old_slot?0u:1u);file_write(other_path,edge_wire,sizeof(edge_wire));
    unsigned ignored;CHECK(kui_update_activation_latest(&selected,&ignored,cancel)==KUI_UPDATE_FORMAT);
    CHECK(install(PACKAGE_B,&pb)==KUI_UPDATE_FORMAT && !writes);file_equal(old_path,old_wire,sizeof(old_wire));protected_equal();unmount_volume();
    memcpy(disk,snapshot,disk_bytes);mount_volume();faults_clear();CHECK(install(PACKAGE_A,&pa)==KUI_UPDATE_OK && !writes);
    char corrupt_path[160];CHECK(kui_update_bank_path(BUILD_A,kui_update_catalog_path(0u),corrupt_path,sizeof(corrupt_path)));
    uint8_t corrupted[UF_FILE_BYTES];memcpy(corrupted,uf_file(&a,0u),sizeof(corrupted));corrupted[200u]^=1u;
    file_write(corrupt_path,corrupted,sizeof(corrupted));faults_clear();
    CHECK(install(PACKAGE_A,&pa)!=KUI_UPDATE_OK && !writes);file_equal(corrupt_path,corrupted,sizeof(corrupted));
    CHECK(!strcmp(latest().selected,BUILD_A));file_equal(old_path,old_wire,sizeof(old_wire));protected_equal();unmount_volume();
    for(unsigned missing=0u;missing<2u;missing++) {
        memcpy(disk,snapshot,disk_bytes);mount_volume();faults_clear();
        if(missing)CHECK(f_unlink("0:/KUI/runtime.kui")==FR_OK);
        else {memcpy(corrupted,uf_file(&baseline,0u),sizeof(corrupted));corrupted[200u]^=1u;file_write("0:/KUI/runtime.kui",corrupted,sizeof(corrupted));}
        active=true;enum kui_update_result result=kui_update_select("",NULL,&status,logger,cancel,NULL);active=false;
        CHECK(result!=KUI_UPDATE_OK && !writes);
        active=true;result=kui_update_rollback(&status,logger,cancel,NULL);active=false;
        CHECK(result!=KUI_UPDATE_OK && !writes);
        CHECK(!strcmp(latest().selected,BUILD_A));file_equal(old_path,old_wire,sizeof(old_wire));
        file_write("0:/KUI/runtime.kui",uf_file(&baseline,0u),UF_FILE_BYTES);protected_equal();unmount_volume();
    }
    /* Baseline validation streams4KiB: one valid marker spanning that boundary
     * succeeds; a second complete marker must refuse despite fresh CRCs. */
    memcpy(disk,snapshot,disk_bytes);mount_volume();faults_clear();
    uint8_t *wide=malloc(64u+8192u);CHECK(wide);memcpy(wide,uf_file(&baseline,0u),64u);
    for(unsigned i=0;i<8192u;i++)wide[64u+i]=(uint8_t)(i*47u+29u);
    static const uint8_t capability[16]=KUI_UPDATE_CAPABILITY_INITIALIZER;
    static const uint8_t handoff[KUI_UPDATE_HANDOFF_BYTES]=KUI_UPDATE_HANDOFF_INITIALIZER;
    memcpy(wide+64u+4090u,capability,sizeof(capability));
    memcpy(wide+64u+128u,handoff,sizeof(handoff));
    uf_put32(wide+16u,8192u);uf_put32(wide+28u,8192u);uf_put32(wide+32u,kui_crc32(0,wide+64u,8192u));
    uf_put32(wide+60u,kui_crc32(0,wide,60u));file_write("0:/KUI/runtime.kui",wide,64u+8192u);
    CHECK(kui_update_select("",NULL,&status,logger,cancel,NULL)==KUI_UPDATE_OK && status.passed && status.done==status.total);
    CHECK(!latest().selected[0]);CHECK(kui_update_select(BUILD_A,pa.manifest_sha256,&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    selected=latest();unsigned marker_slot;CHECK(kui_update_activation_latest(&selected,&marker_slot,cancel)==KUI_UPDATE_OK);
    CHECK(kui_update_activation_encode(&selected,edge_wire));char marker_path[64];snprintf(marker_path,sizeof(marker_path),"0:/KUI/system/active%u.bin",marker_slot);
    memcpy(wide+64u+100u,capability,sizeof(capability));uf_put32(wide+32u,kui_crc32(0,wide+64u,8192u));
    uf_put32(wide+60u,kui_crc32(0,wide,60u));file_write("0:/KUI/runtime.kui",wide,64u+8192u);faults_clear();
    active=true;enum kui_update_result duplicate=kui_update_select("",NULL,&status,logger,cancel,NULL);active=false;
    CHECK(duplicate!=KUI_UPDATE_OK && !writes);file_equal(marker_path,edge_wire,sizeof(edge_wire));
    CHECK(!strcmp(latest().selected,BUILD_A));file_write("0:/KUI/runtime.kui",uf_file(&baseline,0u),UF_FILE_BYTES);
    free(wide);protected_equal();additive_cases();unmount_volume();
    printf("PASS offline updater real filesystem: %u checks, %u write cuts, %u sync cuts, low-space partial retry, zero-free complete reuse, torn activation, cancellation, selector/baseline/cross-chunk-marker guards, protected user data and verified old-or-new selection\n",checks,full_writes+1u,full_syncs+1u);
    free(snapshot);uf_free(&a);uf_free(&b);uf_free(&baseline);free(disk);return 0;
}
