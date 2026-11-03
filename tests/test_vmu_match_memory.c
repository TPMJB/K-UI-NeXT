/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthetic metadata/payload only; actual FatFs persistence and injected
 * publication failures exercise this independent remembered-choice store. */
#define _POSIX_C_SOURCE 200809L
#include "kui/vmu_match_memory.h"
#include "kui/hash.h"
#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static FILE *disk;
static uint64_t disk_blocks;
static FATFS fs;
static uint8_t image[KUI_VMU_IMAGE_BYTES];
static struct kui_vmu_image_workspace workspace;
static struct kui_vmu_import_plan plan,other;
static struct kui_vmu_import_match_group group,other_group;
static const struct kui_vmu_match_memory_target alpha={"KUI-0001","Synthetic Adventure"};
static const struct kui_vmu_match_memory_target beta={"KUI-0002","Synthetic Adventure II"};
static const struct kui_vmu_match_memory_target gamma={"KUI-0003","Synthetic Adventure III"};
static unsigned writes,renames;
enum fault {NONE,WRITE_FAIL,WRITE_SHORT,SYNC_FAIL,READBACK_FAIL,READBACK_CORRUPT,
    RENAME_FAIL,UNLINK_FAIL,FINAL_READ_FAIL};
static enum fault fault;
static bool reading_part,published;

DSTATUS disk_initialize(BYTE drive) {return drive || !disk?STA_NOINIT:0;}
DSTATUS disk_status(BYTE drive) {return disk_initialize(drive);}
static bool disk_range(BYTE drive,LBA_t start,UINT count) {
    return !drive && disk && count && start<disk_blocks && count<=disk_blocks-start &&
        !fseeko(disk,(off_t)start*512,SEEK_SET);
}
DRESULT disk_read(BYTE drive,BYTE *out,LBA_t start,UINT count) {
    if(!disk_range(drive,start,count)) return RES_PARERR;
    return fread(out,512,count,disk)==count?RES_OK:RES_ERROR;
}
DRESULT disk_write(BYTE drive,const BYTE *in,LBA_t start,UINT count) {
    if(!disk_range(drive,start,count)) return RES_PARERR;
    return fwrite(in,512,count,disk)==count?RES_OK:RES_ERROR;
}
DRESULT disk_ioctl(BYTE drive,BYTE operation,void *out) {
    if(drive || !disk) return RES_NOTRDY;
    switch(operation) {
    case CTRL_SYNC:return !fflush(disk) && !fsync(fileno(disk))?RES_OK:RES_ERROR;
    case GET_SECTOR_COUNT:*(LBA_t *)out=(LBA_t)disk_blocks;return RES_OK;
    case GET_SECTOR_SIZE:*(WORD *)out=512;return RES_OK;
    case GET_BLOCK_SIZE:*(DWORD *)out=1;return RES_OK;
    default:return RES_PARERR;
    }
}
DWORD get_fattime(void) {return ((2026u-1980u)<<25)|(11u<<21)|(1u<<16);}
FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __real_f_write(FIL *,const void *,UINT,UINT *);
FRESULT __real_f_sync(FIL *);
FRESULT __real_f_rename(const TCHAR *,const TCHAR *);
FRESULT __real_f_unlink(const TCHAR *);
FRESULT __wrap_f_open(FIL *file,const TCHAR *path,BYTE mode) {
    reading_part=strstr(path,".part")!=NULL;
    if(reading_part && (mode&FA_WRITE)) assert(mode==(FA_WRITE|FA_CREATE_NEW));
    return __real_f_open(file,path,mode);
}
FRESULT __wrap_f_write(FIL *file,const void *data,UINT bytes,UINT *done) {
    ++writes;assert(reading_part && bytes==208u);
    if(fault==WRITE_FAIL) {fault=NONE;*done=0;return FR_DISK_ERR;}
    if(fault==WRITE_SHORT) {fault=NONE;return __real_f_write(file,data,bytes/2u,done);}
    return __real_f_write(file,data,bytes,done);
}
FRESULT __wrap_f_sync(FIL *file) {
    if(fault==SYNC_FAIL) {fault=NONE;return FR_DISK_ERR;}
    return __real_f_sync(file);
}
FRESULT __wrap_f_read(FIL *file,void *data,UINT bytes,UINT *got) {
    if((reading_part && fault==READBACK_FAIL) ||
       (!reading_part && published && fault==FINAL_READ_FAIL)) {
        fault=NONE;*got=0;return FR_DISK_ERR;
    }
    FRESULT result=__real_f_read(file,data,bytes,got);
    if(reading_part && fault==READBACK_CORRUPT && result==FR_OK && *got) {
        fault=NONE;((uint8_t *)data)[*got/2u]^=1u;
    }
    return result;
}
FRESULT __wrap_f_rename(const TCHAR *from,const TCHAR *to) {
    ++renames;assert(strstr(from,".part") && strstr(to,".bin"));
    if(fault==RENAME_FAIL) {fault=NONE;return FR_DISK_ERR;}
    FRESULT result=__real_f_rename(from,to);if(result==FR_OK) published=true;
    return result;
}
FRESULT __wrap_f_unlink(const TCHAR *path) {
    if(fault==UNLINK_FAIL && strstr(path,".bin")) {fault=NONE;return FR_DISK_ERR;}
    return __real_f_unlink(path);
}
static void put16(uint8_t *out,unsigned n) {out[0]=(uint8_t)n;out[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *out,uint32_t n) {
    for(unsigned i=0;i<4u;i++) out[i]=(uint8_t)(n>>(i*8u));
}
static void make_plan(bool relocated,struct kui_vmu_import_plan *out,
    struct kui_vmu_import_match_group *members) {
    for(unsigned i=0;i<256u;i++) assert(!kui_vmu_image_blank_block((uint16_t)i,image+i*512u));
    for(unsigned i=0;i<2u;i++) {
        unsigned slot=relocated?(i?3u:20u):(i?12u:8u);
        unsigned first=relocated?(i?6u:5u):(i?180u:190u);
        unsigned second=relocated?77u:191u;
        unsigned blocks=i?1u:2u,header=i?first:second;
        uint8_t *entry=image+(253u-slot/16u)*512u+(slot%16u)*32u;
        entry[0]=0x33u;entry[1]=(uint8_t)relocated;put16(entry+2u,first);
        memcpy(entry+4u,i?"GENERIC.001":"GENERIC.SYS",11u);
        memset(entry+16u,relocated?0x31:0x22,8u);put16(entry+24u,blocks);
        put16(entry+26u,i?0u:1u);memset(entry+28u,relocated?0x64:0x13,4u);
        memset(image+first*512u,relocated?0x5a:0x3c,512u);
        put16(image+254u*512u+first*2u,i?0xfffau:second);
        if(!i) {
            memset(image+second*512u,relocated?0xa5:0xc3,512u);
            put16(image+254u*512u+second*2u,0xfffa);
        }
        uint8_t *h=image+header*512u;memset(h,' ',64u);
        memcpy(h,i?"Save data":"System data",i?9u:11u);
        memcpy(h+16u,"Unidentified adventure data",27u);memcpy(h+48u,"SEGA",4u);
    }
    assert(!kui_vmu_import_plan_inspect(image,sizeof(image),NULL,NULL,&workspace,out));
    assert(out->entry_count==2u && out->summary.used_blocks==3u);
    memset(members,0,sizeof(*members));members->members[0]=3u;members->files=2u;members->blocks=3u;
}
static void key_path(const struct kui_vmu_import_plan *p,
    const struct kui_vmu_import_match_group *g,const char *suffix,char out[96]) {
    uint8_t key[32];char hex[65];assert(!kui_vmu_match_memory_key(p,g,key));
    kui_hex(key,32u,hex);int n=snprintf(out,96,"%s/%s%s",KUI_VMU_MATCH_MEMORY_ROOT,hex,suffix);
    assert(n>0 && n<96);
}
static void remount(void) {
    assert(f_mount(NULL,"0:",0)==FR_OK);assert(f_mount(&fs,"0:",1)==FR_OK);
}
static void expect(const struct kui_vmu_import_plan *p,
    const struct kui_vmu_import_match_group *g,const struct kui_vmu_match_memory_target *wanted,int status) {
    struct kui_vmu_match_memory_target got;memset(&got,0xa5,sizeof(got));
    assert(kui_vmu_match_memory_load(p,g,&got)==status);
    if(wanted) assert(!strcmp(got.product,wanted->product) && !strcmp(got.title,wanted->title));
    else {struct kui_vmu_match_memory_target empty={0};assert(!memcmp(&empty,&got,sizeof(got)));}
}
static void raw_write(const char *path,const void *data,size_t size) {
    FIL file;UINT done;assert(__real_f_open(&file,path,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
    assert(__real_f_write(&file,data,(UINT)size,&done)==FR_OK && done==size);
    assert(__real_f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static void raw_read(const char *path,uint8_t bytes[208]) {
    FIL file;UINT got;assert(__real_f_open(&file,path,FA_READ)==FR_OK);
    assert(f_size(&file)==208u && __real_f_read(&file,bytes,208u,&got)==FR_OK && got==208u);
    assert(f_close(&file)==FR_OK);
}
static void pure_keys(void) {
    uint8_t key[32],second[32],unchanged[32];
    assert(!kui_vmu_match_memory_key(&plan,&group,key));make_plan(true,&other,&other_group);
    assert(!kui_vmu_match_memory_key(&other,&other_group,second) && !memcmp(key,second,32u));
    assert(strcmp(plan.entries[0].name,other.entries[0].name));
    /* Every included field changes the key; payload/timestamps/allocation and
     * directory order already differ in the independently admitted image. */
    for(unsigned field=0;field<7u;field++) {
        other=plan;other_group=group;struct kui_vmu_import_entry *e=&other.entries[0];
        switch(field) {
        case 0:e->name[0]='X';break;
        case 1:e->application[0]='X';break;
        case 2:e->short_description[0]='X';break;
        case 3:e->long_description[0]='X';break;
        case 4:e->type=0xccu;break;
        case 5:++e->blocks;++other_group.blocks;break;
        default:e->header_offset=0;break;
        }
        assert(!kui_vmu_match_memory_key(&other,&other_group,second) && memcmp(key,second,32u));
    }
    other=plan;other.entries[0].slot=73u;other.entries[0].hints=0xffu;
    assert(!kui_vmu_match_memory_key(&other,&group,second) && !memcmp(key,second,32u));
    other_group=group;other_group.members[0]=1u;other_group.files=1u;other_group.blocks=2u;
    assert(!kui_vmu_match_memory_key(&plan,&other_group,second) && memcmp(key,second,32u));
    memset(unchanged,0xa5,sizeof(unchanged));memcpy(second,unchanged,sizeof(second));
    for(unsigned bad=0;bad<6u;bad++) {
        other=plan;other_group=group;
        switch(bad) {
        case 0:other_group.files=0;break;
        case 1:++other_group.blocks;break;
        case 2:other_group.members[2]=1u;break;
        case 3:memset(other.entries[0].name,'X',sizeof(other.entries[0].name));break;
        case 4:other.entries[0].header_offset=other.entries[0].blocks;break;
        default:other.entry_count=209u;break;
        }
        assert(kui_vmu_match_memory_key(&other,&other_group,second)==KUI_VMU_MATCH_MEMORY_INVALID);
        assert(!memcmp(second,unchanged,sizeof(second)));
    }
    struct kui_vmu_match_memory_target bad=alpha;memset(bad.title,'X',sizeof(bad.title));
    assert(kui_vmu_match_memory_remember(&plan,&group,&bad)==KUI_VMU_MATCH_MEMORY_INVALID);
}
static void roundtrip(void) {
    expect(&plan,&group,NULL,KUI_VMU_MATCH_MEMORY_MISSING);
    assert(!kui_vmu_match_memory_remember(&plan,&group,&alpha));remount();
    expect(&plan,&group,&alpha,KUI_VMU_MATCH_MEMORY_OK);
    unsigned old_writes=writes,old_renames=renames;
    assert(!kui_vmu_match_memory_remember(&plan,&group,&alpha));
    assert(writes==old_writes && renames==old_renames);
    make_plan(true,&other,&other_group);expect(&other,&other_group,&alpha,KUI_VMU_MATCH_MEMORY_OK);
    other.entries[0].application[0]='X';expect(&other,&other_group,NULL,KUI_VMU_MATCH_MEMORY_MISSING);
    assert(!kui_vmu_match_memory_remember(&other,&other_group,&beta));remount();
    expect(&plan,&group,&alpha,KUI_VMU_MATCH_MEMORY_OK);expect(&other,&other_group,&beta,KUI_VMU_MATCH_MEMORY_OK);
    assert(!kui_vmu_match_memory_forget(&other,&other_group));remount();
    expect(&other,&other_group,NULL,KUI_VMU_MATCH_MEMORY_MISSING);
    expect(&plan,&group,&alpha,KUI_VMU_MATCH_MEMORY_OK);
}
static void corrections(void) {
    assert(!kui_vmu_match_memory_forget(&plan,&group));assert(!writes);
    assert(!kui_vmu_match_memory_remember(&plan,&group,&alpha));
    assert(!kui_vmu_match_memory_remember(&plan,&group,&beta));remount();
    expect(&plan,&group,&beta,KUI_VMU_MATCH_MEMORY_OK);
    assert(!kui_vmu_match_memory_remember(&plan,&group,&gamma));remount();
    expect(&plan,&group,&gamma,KUI_VMU_MATCH_MEMORY_OK);
    assert(!kui_vmu_match_memory_forget(&plan,&group));remount();
    expect(&plan,&group,NULL,KUI_VMU_MATCH_MEMORY_MISSING);
    unsigned old_writes=writes;assert(!kui_vmu_match_memory_forget(&plan,&group));assert(old_writes==writes);
    assert(!kui_vmu_match_memory_remember(&plan,&group,&alpha));remount();
    expect(&plan,&group,&alpha,KUI_VMU_MATCH_MEMORY_OK);
    char path[96];FILINFO info;key_path(&plan,&group,"-a.bin",path);
    assert(f_stat(path,&info)==FR_OK && info.fsize==208u);key_path(&plan,&group,"-b.bin",path);
    assert(f_stat(path,&info)==FR_OK && info.fsize==208u);key_path(&plan,&group,".part",path);
    assert(f_stat(path,&info)==FR_NO_FILE);
}
static void stale_part(void) {
    assert(!kui_vmu_match_memory_remember(&plan,&group,&alpha));
    char path[96];key_path(&plan,&group,".part",path);raw_write(path,"unfinished",10u);remount();
    expect(&plan,&group,&alpha,KUI_VMU_MATCH_MEMORY_OK);
    assert(!kui_vmu_match_memory_remember(&plan,&group,&beta));remount();
    expect(&plan,&group,&beta,KUI_VMU_MATCH_MEMORY_OK);
    key_path(&plan,&group,"-a.bin",path);assert(__real_f_unlink(path)==FR_OK);
    key_path(&plan,&group,"-b.bin",path);assert(__real_f_unlink(path)==FR_OK);
    key_path(&plan,&group,".part",path);raw_write(path,"interrupted",11u);remount();
    expect(&plan,&group,NULL,KUI_VMU_MATCH_MEMORY_MISSING);
    assert(!kui_vmu_match_memory_remember(&plan,&group,&gamma));remount();
    expect(&plan,&group,&gamma,KUI_VMU_MATCH_MEMORY_OK);
}
static void interrupted(enum fault failure,bool first) {
    if(!first) {
        assert(!kui_vmu_match_memory_remember(&plan,&group,&alpha));
        assert(!kui_vmu_match_memory_remember(&plan,&group,&beta));
    }
    published=false;fault=failure;
    int result=kui_vmu_match_memory_remember(&plan,&group,&gamma);
    assert(result==(failure==READBACK_CORRUPT?KUI_VMU_MATCH_MEMORY_CORRUPT:KUI_VMU_MATCH_MEMORY_IO));
    assert(fault==NONE);remount();
    const struct kui_vmu_match_memory_target *expected=failure==FINAL_READ_FAIL?&gamma:first?NULL:&beta;
    expect(&plan,&group,expected,expected?KUI_VMU_MATCH_MEMORY_OK:KUI_VMU_MATCH_MEMORY_MISSING);
    assert(!kui_vmu_match_memory_remember(&plan,&group,&gamma));remount();
    expect(&plan,&group,&gamma,KUI_VMU_MATCH_MEMORY_OK);
}
static void corrupt(const char *kind) {
    assert(!kui_vmu_match_memory_remember(&plan,&group,&alpha));
    assert(!kui_vmu_match_memory_remember(&plan,&group,&beta));
    other=plan;other_group=group;other.entries[0].application[0]='X';
    assert(!kui_vmu_match_memory_remember(&other,&other_group,&gamma));
    char path[96];key_path(&plan,&group,!strcmp(kind,"corrupt-older")?"-a.bin":"-b.bin",path);
    uint8_t before[208],bytes[208];raw_read(path,bytes);
    if(!strcmp(kind,"same-generation-conflict")) put32(bytes+8u,1u);
    else if(!strcmp(kind,"wrong-key")) bytes[12]^=1u;
    else if(!strcmp(kind,"reserved")) bytes[190]=1u;
    else bytes[80]^=1u;
    if(!strcmp(kind,"same-generation-conflict") || !strcmp(kind,"wrong-key") || !strcmp(kind,"reserved"))
        put32(bytes+204u,kui_crc32(0,bytes,204u));
    size_t size=!strcmp(kind,"truncated")?41u:sizeof(bytes);raw_write(path,bytes,size);remount();
    expect(&plan,&group,NULL,KUI_VMU_MATCH_MEMORY_CORRUPT);
    unsigned old_writes=writes;
    assert(kui_vmu_match_memory_remember(&plan,&group,&gamma)==KUI_VMU_MATCH_MEMORY_CORRUPT);
    assert(kui_vmu_match_memory_forget(&plan,&group)==KUI_VMU_MATCH_MEMORY_CORRUPT);
    assert(old_writes==writes);
    if(size==sizeof(bytes)) {memcpy(before,bytes,sizeof(before));raw_read(path,bytes);assert(!memcmp(before,bytes,sizeof(bytes)));}
    expect(&other,&other_group,&gamma,KUI_VMU_MATCH_MEMORY_OK);
}
static void directory_collision(bool staging) {
    assert(!kui_vmu_match_memory_remember(&plan,&group,&alpha));
    char path[96];key_path(&plan,&group,staging?".part":"-b.bin",path);
    assert(f_mkdir(path)==FR_OK);remount();
    if(staging) {
        expect(&plan,&group,&alpha,KUI_VMU_MATCH_MEMORY_OK);
        assert(kui_vmu_match_memory_remember(&plan,&group,&beta)==KUI_VMU_MATCH_MEMORY_IO);
        expect(&plan,&group,&alpha,KUI_VMU_MATCH_MEMORY_OK);
    } else {
        expect(&plan,&group,NULL,KUI_VMU_MATCH_MEMORY_CORRUPT);
        assert(kui_vmu_match_memory_remember(&plan,&group,&beta)==KUI_VMU_MATCH_MEMORY_CORRUPT);
    }
    FILINFO info;assert(f_stat(path,&info)==FR_OK && (info.fattrib&AM_DIR));
}
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    struct stat st;if(stat(argv[1],&st) || !S_ISREG(st.st_mode) || st.st_size%512) return 2;
    disk=fopen(argv[1],"r+b");if(!disk) return 2;disk_blocks=(uint64_t)st.st_size/512u;
    assert(f_mount(&fs,"0:",1)==FR_OK);make_plan(false,&plan,&group);
    const char *kind=argv[2];
    if(!strcmp(kind,"keys")) pure_keys();
    else if(!strcmp(kind,"roundtrip")) roundtrip();
    else if(!strcmp(kind,"corrections")) corrections();
    else if(!strcmp(kind,"stale-part")) stale_part();
    else if(!strcmp(kind,"staging-directory")) directory_collision(true);
    else if(!strcmp(kind,"published-directory")) directory_collision(false);
    else if(!strcmp(kind,"first-write-fail")) interrupted(WRITE_FAIL,true);
    else if(!strcmp(kind,"write-fail")) interrupted(WRITE_FAIL,false);
    else if(!strcmp(kind,"write-short")) interrupted(WRITE_SHORT,false);
    else if(!strcmp(kind,"sync-fail")) interrupted(SYNC_FAIL,false);
    else if(!strcmp(kind,"readback-fail")) interrupted(READBACK_FAIL,false);
    else if(!strcmp(kind,"readback-corrupt")) interrupted(READBACK_CORRUPT,false);
    else if(!strcmp(kind,"rename-fail")) interrupted(RENAME_FAIL,false);
    else if(!strcmp(kind,"unlink-fail")) interrupted(UNLINK_FAIL,false);
    else if(!strcmp(kind,"final-read-fail")) interrupted(FINAL_READ_FAIL,false);
    else if(!strcmp(kind,"corrupt-newer") || !strcmp(kind,"corrupt-older") || !strcmp(kind,"truncated") ||
        !strcmp(kind,"same-generation-conflict") || !strcmp(kind,"wrong-key") || !strcmp(kind,"reserved")) corrupt(kind);
    else return 2;
    assert(f_mount(NULL,"0:",0)==FR_OK && !fclose(disk));
    printf("PASS VMU match memory: %s\n",kind);return 0;
}
