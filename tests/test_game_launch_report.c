/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_launch_report.h"
#include "kui/game_ram_patches.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Deliberately independent of kui_crc32 and the report encoder. A valid
 * fixture must remain readable when encoder and inspector change together. */
static uint32_t fixture_crc(const void *bytes,size_t size) {
    const uint8_t *p=bytes;uint32_t crc=UINT32_MAX;
    while(size--) {
        crc^=*p++;
        for(unsigned i=0;i<8;i++) crc=(crc>>1)^((crc&1)?UINT32_C(0xedb88320):0);
    }
    return ~crc;
}
static const struct kui_game_launch_report launch={
    .version="2.0-rc10 Tyrfing",.build_id="0123456789ab",
    .image_path="/games/toy commander/disc.gdi",.reader="background",
    .storage="SCI",.encoding="plain",.cdda_profile="general",
    .package_path="/kui/loaders/retail-sci.bin",.product="MK-51020",
    .title="Toy Commander",.bootfile="1ST_READ.BIN",.cdda=true,
    .ip_crc32=UINT32_C(0x1234abcd),.ip_bytes=32768,
    .executable_crc32=UINT32_C(0xfedcba98),.executable_bytes=1048577
};
static const char fixture_body[]=
    "KUI launch report v1\n"
    "sequence: 00000000000000000042\n"
    "state: prepared before handoff; game start unconfirmed\n"
    "version: 2.0-rc10 Tyrfing\n"
    "build: 0123456789ab\n"
    "image: /games/toy commander/disc.gdi\n"
    "reader: background\n"
    "storage: SCI\n"
    "encoding: plain\n"
    "cd music: on\n"
    "profile: general\n"
    "package: /kui/loaders/retail-sci.bin\n"
    "product: MK-51020\n"
    "title: Toy Commander\n"
    "bootfile: 1ST_READ.BIN\n"
    "ip bytes: 32768\n"
    "ip crc32: 1234ABCD\n"
    "source executable bytes: 1048577\n"
    "source executable crc32: FEDCBA98\n"
    "patch 1: off address=0x00000000 value=0x00000000\n"
    "patch 2: off address=0x00000000 value=0x00000000\n";
static size_t seal(char out[KUI_GAME_LAUNCH_REPORT_MAX],const char *body) {
    size_t size=strlen(body);assert(size+17<KUI_GAME_LAUNCH_REPORT_MAX);
    memcpy(out,body,size);
    int count=snprintf(out+size,KUI_GAME_LAUNCH_REPORT_MAX-size,"crc32: %08" PRIX32 "\n",fixture_crc(body,size));
    assert(count==16);return size+(size_t)count;
}
static size_t encoded(char out[KUI_GAME_LAUNCH_REPORT_MAX],const struct kui_game_launch_report *report,uint64_t sequence) {
    size_t size=0;memset(out,0,KUI_GAME_LAUNCH_REPORT_MAX);
    assert(kui_game_launch_report_encode(out,KUI_GAME_LAUNCH_REPORT_MAX,&size,report,sequence));
    assert(size && size<KUI_GAME_LAUNCH_REPORT_MAX);return size;
}
static void rejected(const void *record,size_t size) {
    uint64_t sequence=UINT64_C(0x123456789abcdef0);
    assert(!kui_game_launch_report_inspect(record,size,&sequence));
    assert(sequence==UINT64_C(0x123456789abcdef0));
}
/* Replace a body fragment and recompute CRC, so grammar tests do not merely
 * exercise the checksum check. This also permits length-changing mutations. */
static size_t changed(char out[KUI_GAME_LAUNCH_REPORT_MAX],const char *before,const char *after) {
    const char *at=strstr(fixture_body,before);assert(at);
    size_t prefix=(size_t)(at-fixture_body),left=strlen(after),tail=strlen(at+strlen(before));
    char body[KUI_GAME_LAUNCH_REPORT_MAX];assert(prefix+left+tail+1<sizeof(body));
    memcpy(body,fixture_body,prefix);memcpy(body+prefix,after,left);
    memcpy(body+prefix+left,at+strlen(before),tail+1);return seal(out,body);
}
static void independent_format(void) {
    char expected[KUI_GAME_LAUNCH_REPORT_MAX],record[KUI_GAME_LAUNCH_REPORT_MAX];
    assert(fixture_crc("123456789",9)==UINT32_C(0xcbf43926));
    size_t bytes=seal(expected,fixture_body),actual=encoded(record,&launch,42);
    assert(actual==bytes && !memcmp(expected,record,bytes));
    uint64_t sequence=0;assert(kui_game_launch_report_inspect(expected,bytes,&sequence) && sequence==42);
    /* Every body and footer byte is covered, including line terminators. */
    for(size_t i=0;i<bytes;i++) {
        record[i]=(char)(expected[i]^1);rejected(record,bytes);record[i]=expected[i];
    }
    for(size_t size=0;size<bytes;size++) rejected(expected,size);
    memcpy(record,expected,bytes);record[bytes]='\n';rejected(record,bytes+1);
    memcpy(record,expected,bytes);record[bytes]='\0';rejected(record,bytes+1);
    rejected(NULL,bytes);rejected(expected,SIZE_MAX);
    assert(!kui_game_launch_report_inspect(expected,bytes,NULL));
    actual=encoded(record,&launch,UINT64_MAX);
    assert(kui_game_launch_report_inspect(record,actual,&sequence) && sequence==UINT64_MAX);
    puts("PASS launch report: independent CRC/text fixture, complete byte coverage, truncation and sequence limits");
}
static void grammar(void) {
    static const struct {const char *before,*after;} cases[]={
        {"KUI launch report v1\n","KUI launch report v2\n"},
        {"00000000000000000042","00000000000000000000"},
        {"00000000000000000042","0000000000000000042"},
        {"00000000000000000042","18446744073709551616"},
        {"00000000000000000042","-0000000000000000042"},
        {"game start unconfirmed","game started successfully"},
        {"version: 2.0-rc10 Tyrfing\n","version: \n"},
        {"reader: background\n","reader: \n"},
        {"storage: SCI\n","storage: SCI\nunknown: field\n"},
        {"encoding: plain\n","encoding: plain\nencoding: plain\n"},
        {"reader: background\nstorage: SCI\n","storage: SCI\nreader: background\n"},
        {"cd music: on\n","cd music: true\n"},
        {"image: /games/toy commander/disc.gdi\n","image: /GAMES/toy commander/disc.gdi\n"},
        {"image: /games/toy commander/disc.gdi\n","image: /games/../disc.gdi\n"},
        {"image: /games/toy commander/disc.gdi\n","image: /games//disc.gdi\n"},
        {"image: /games/toy commander/disc.gdi\n","image: 1:/games/disc.gdi\n"},
        {"package: /kui/loaders/retail-sci.bin\n","package: /kui/loaders/\n"},
        {"title: Toy Commander\n","title: Toy\x01Commander\n"},
        {"title: Toy Commander\n","title: Toy\\nCommander\n"},
        {"title: Toy Commander\n","title: Toy\\x0aCommander\n"},
        {"title: Toy Commander\n","title: Toy\\xGGCommander\n"},
        {"title: Toy Commander\n","title: Toy\\x41Commander\n"},
        {"ip bytes: 32768\n","ip bytes: 4294967296\n"},
        {"ip bytes: 32768\n","ip bytes: 0\n"},
        {"ip bytes: 32768\n","ip bytes: -1\n"},
        {"ip crc32: 1234ABCD\n","ip crc32: 1234abcd\n"},
        {"ip crc32: 1234ABCD\n","ip crc32: 1234ABC\n"},
        {"source executable bytes: 1048577\n","source executable bytes: +1048577\n"},
        {"source executable crc32: FEDCBA98\n","source executable crc32: FEDCBA9G\n"},
        {"patch 1: off address=0x00000000 value=0x00000000\n","patch 1: maybe address=0x00000000 value=0x00000000\n"},
        {"patch 2: off address=0x00000000 value=0x00000000\n",""}
    };
    char record[KUI_GAME_LAUNCH_REPORT_MAX];
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) rejected(record,changed(record,cases[i].before,cases[i].after));
    /* A bounded, terminated but overlength field cannot bypass validation by
     * arriving from disk rather than the public encoder. */
    char replacement[160];memcpy(replacement,"version: ",9);memset(replacement+9,'v',65);
    replacement[74]='\n';replacement[75]=0;
    rejected(record,changed(record,"version: 2.0-rc10 Tyrfing\n",replacement));
    size_t bytes=seal(record,fixture_body);record[5]=0;rejected(record,bytes);
    puts("PASS launch report: independently rechecksummed malformed grammar, field order, escaping and numeric bounds");
}
static void encode_rejected(const struct kui_game_launch_report *report,uint64_t sequence,size_t capacity) {
    uint8_t record[KUI_GAME_LAUNCH_REPORT_MAX],before[KUI_GAME_LAUNCH_REPORT_MAX];
    memset(record,0xa7,sizeof(record));memcpy(before,record,sizeof(record));size_t size=123;
    assert(!kui_game_launch_report_encode(record,capacity,&size,report,sequence));
    assert(size==123 && !memcmp(record,before,sizeof(record)));
}
static void text_and_bounds(void) {
    char record[KUI_GAME_LAUNCH_REPORT_MAX],normal[KUI_GAME_LAUNCH_REPORT_MAX];
    size_t bytes=encoded(normal,&launch,42);
    struct kui_game_launch_report report=launch;
    report.image_path="0:\\GAMES\\\\Toy Commander\\disc.GDI";
    report.package_path="0:/KUI//LOADERS/RETAIL-SCI.BIN";
    assert(encoded(record,&report,42)==bytes && !memcmp(record,normal,bytes));
    report.title="Toy\nCommander\r\t\\\177";report.product="";
    bytes=encoded(record,&report,1);
    assert(strstr(record,"title: Toy\\x0ACommander\\x0D\\x09\\x5C\\x7F\n"));
    assert(strstr(record,"product: \n"));uint64_t sequence=0;
    assert(kui_game_launch_report_inspect(record,bytes,&sequence) && sequence==1);
    report.title="";assert(encoded(record,&report,1)>0);
    struct kui_game_ram_patches patches={.entries={
        {.enabled=true,.address=UINT32_C(0xac010000),.value=UINT32_C(0xdeadbeef)},
        {.enabled=false,.address=UINT32_C(0x8c010004),.value=UINT32_C(0xffffffff)}}};
    report=launch;report.patches=&patches;report.cdda=false;
    bytes=encoded(record,&report,2);
    assert(strstr(record,"cd music: off\n"));
    assert(strstr(record,"patch 1: on address=0xAC010000 value=0xDEADBEEF\n"));
    assert(strstr(record,"patch 2: off address=0x8C010004 value=0xFFFFFFFF\n"));
    assert(kui_game_launch_report_inspect(record,bytes,&sequence) && sequence==2);
    /* Worst-case expansion at every documented field bound. */
    char short_text[65],medium_text[33],long_text[129],path[512];
    memset(short_text,1,64);short_text[64]=0;memset(medium_text,2,32);medium_text[32]=0;
    memset(long_text,3,128);long_text[128]=0;path[0]='/';memset(path+1,'p',510);path[511]=0;
    report=(struct kui_game_launch_report){
        .version=short_text,.build_id=short_text,.reader=short_text,.cdda_profile=short_text,
        .product=short_text,.storage=medium_text,.encoding=medium_text,.title=long_text,
        .bootfile=long_text,.image_path=path,.package_path=path,
        .ip_bytes=UINT32_MAX,.executable_bytes=UINT32_MAX,.ip_crc32=UINT32_MAX,.executable_crc32=UINT32_MAX};
    bytes=encoded(record,&report,UINT64_MAX);assert(kui_game_launch_report_inspect(record,bytes,&sequence));
    encode_rejected(&report,1,bytes-1);encode_rejected(&report,1,0);encode_rejected(&report,0,sizeof(record));
    encode_rejected(NULL,1,sizeof(record));
    const char **fields[]={&report.version,&report.build_id,&report.reader,&report.cdda_profile,&report.product,
        &report.storage,&report.encoding,&report.title,&report.bootfile,&report.image_path,&report.package_path};
    const size_t limits[]={64,64,64,64,64,32,32,128,128,511,511};
    for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++) {
        const char *saved=*fields[i];char too_long[514];memset(too_long,'x',sizeof(too_long));
        if(i>=9) too_long[0]='/';
        too_long[limits[i]+1]=0;*fields[i]=too_long;
        encode_rejected(&report,1,sizeof(record));*fields[i]=NULL;
        encode_rejected(&report,1,sizeof(record));*fields[i]=saved;
    }
    static const char *const bad_paths[]={"","relative.gdi","1:/games/disc.gdi","/games/../disc.gdi",
        "/games/./disc.gdi","/games/","/games/ disc.gdi","/games/disc.gdi.","/games/disc?.gdi","/games/a\nb.gdi"};
    report=launch;
    for(unsigned i=0;i<sizeof(bad_paths)/sizeof(bad_paths[0]);i++) {
        report.image_path=bad_paths[i];encode_rejected(&report,1,sizeof(record));report.image_path=launch.image_path;
        report.package_path=bad_paths[i];encode_rejected(&report,1,sizeof(record));report.package_path=launch.package_path;
    }
    const char **required[]={&report.version,&report.build_id,&report.reader,&report.storage,&report.encoding,
        &report.cdda_profile,&report.bootfile};
    for(unsigned i=0;i<sizeof(required)/sizeof(required[0]);i++) {
        const char *saved=*required[i];*required[i]="";encode_rejected(&report,1,sizeof(record));*required[i]=saved;
    }
    size_t size=123;memset(record,0xa7,sizeof(record));
    assert(!kui_game_launch_report_encode(NULL,sizeof(record),&size,&launch,1) && size==123);
    assert(!kui_game_launch_report_encode(record,sizeof(record),NULL,&launch,1));
    for(size_t i=0;i<sizeof(record);i++) assert((unsigned char)record[i]==0xa7);
    puts("PASS launch report: canonical paths, safe text escaping, maximum field bounds and failure output atomicity");
}

/* A small FatFs model keeps the two independent card slots and records actual
 * open/read/write/sync/close boundaries. No fake save routine hides failures. */
struct image {bool exists;size_t size;uint8_t data[KUI_GAME_LAUNCH_REPORT_MAX+1];};
static struct image disk[2],snapshot[2];
static unsigned read_opens,write_opens,last_written,opened,closed,cancel_calls,cancel_at;
static bool parents[3],file_in_parent;
enum fault {NONE,READ_OPEN,READ_ERROR,READ_SHORT,READ_CLOSE,WRITE_OPEN,WRITE_ERROR,
    WRITE_SHORT,SYNC_ERROR,WRITE_CLOSE,VERIFY_OPEN,VERIFY_ERROR,VERIFY_CORRUPT,
    VERIFY_SHORT,VERIFY_CLOSE,PARENT_ERROR,MKDIR_ERROR};
static enum fault failure;
static unsigned index_of(const char *path) {
    if(!strcmp(path,KUI_GAME_LAUNCH_REPORT_PATH_A)) return 0;
    assert(!strcmp(path,KUI_GAME_LAUNCH_REPORT_PATH_B));return 1;
}
static unsigned parent_of(const char *path) {
    static const char *const names[]={"0:/KUI","0:/KUI/apps","0:/KUI/apps/games"};
    for(unsigned i=0;i<3;i++) if(!strcmp(path,names[i])) return i;
    assert(!"unexpected filesystem path");return 0;
}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    unsigned index=index_of(path);bool writing=(mode&FA_WRITE)!=0;
    if(writing) {
        ++write_opens;last_written=index;
        if(failure==WRITE_OPEN) return FR_DISK_ERR;
        disk[index].exists=true;disk[index].size=0;
    } else {
        ++read_opens;
        if(failure==READ_OPEN || (failure==VERIFY_OPEN && write_opens)) return FR_DISK_ERR;
        if(!disk[index].exists) return FR_NO_FILE;
    }
    memset(file,0,sizeof(*file));file->obj.id=(WORD)(index+1);file->obj.objsize=disk[index].size;
    file->flag=mode;++opened;return FR_OK;
}
FRESULT f_read(FIL *file,void *buffer,UINT wanted,UINT *got) {
    unsigned index=(unsigned)file->obj.id-1;assert(index<2 && !(file->flag&FA_WRITE));
    size_t at=(size_t)file->fptr;assert(at<=disk[index].size);size_t available=disk[index].size-at;
    UINT count=available<wanted?(UINT)available:wanted;
    if((failure==READ_SHORT || (failure==VERIFY_SHORT && write_opens)) && count) --count;
    memcpy(buffer,disk[index].data+at,count);file->fptr+=count;*got=count;
    return failure==READ_ERROR || (failure==VERIFY_ERROR && write_opens)?FR_DISK_ERR:FR_OK;
}
FRESULT f_write(FIL *file,const void *buffer,UINT wanted,UINT *written) {
    unsigned index=(unsigned)file->obj.id-1;assert(index<2 && (file->flag&FA_WRITE));
    UINT count=(failure==WRITE_SHORT || failure==WRITE_ERROR)?wanted/2:wanted;
    assert((size_t)file->fptr+count<=sizeof(disk[index].data));
    memcpy(disk[index].data+(size_t)file->fptr,buffer,count);file->fptr+=count;
    disk[index].size=(size_t)file->fptr;file->obj.objsize=disk[index].size;*written=count;
    return failure==WRITE_ERROR?FR_DISK_ERR:FR_OK;
}
FRESULT f_sync(FIL *file) {assert(file->flag&FA_WRITE);return failure==SYNC_ERROR?FR_DISK_ERR:FR_OK;}
FRESULT f_close(FIL *file) {
    unsigned index=(unsigned)file->obj.id-1;assert(index<2);++closed;
    if((file->flag&FA_WRITE) && failure==VERIFY_CORRUPT) disk[index].data[disk[index].size/2]^=1;
    if((file->flag&FA_WRITE) && failure==WRITE_CLOSE) return FR_DISK_ERR;
    if(!(file->flag&FA_WRITE) && (failure==READ_CLOSE || (failure==VERIFY_CLOSE && write_opens))) return FR_DISK_ERR;
    return FR_OK;
}
FRESULT f_stat(const TCHAR *path,FILINFO *info) {
    unsigned index=parent_of(path);memset(info,0,sizeof(*info));
    if(failure==PARENT_ERROR) return FR_DISK_ERR;
    if(!parents[index]) return FR_NO_PATH;
    info->fattrib=file_in_parent?0:AM_DIR;return FR_OK;
}
FRESULT f_mkdir(const TCHAR *path) {
    unsigned index=parent_of(path);if(failure==MKDIR_ERROR) return FR_DISK_ERR;
    parents[index]=true;return FR_OK;
}
static bool cancelled(void) {return ++cancel_calls>=cancel_at;}
static void clear_faults(void) {failure=NONE;read_opens=write_opens=opened=closed=cancel_calls=0;last_written=99;}
static void reset(void) {
    memset(disk,0,sizeof(disk));memset(parents,0,sizeof(parents));file_in_parent=false;cancel_at=1;clear_faults();
}
static void seed(unsigned slot,uint64_t sequence,const struct kui_game_launch_report *report) {
    disk[slot].size=encoded((char *)disk[slot].data,report,sequence);disk[slot].exists=true;
}
static uint64_t slot_sequence(unsigned slot) {
    uint64_t sequence=0;assert(kui_game_launch_report_inspect(disk[slot].data,disk[slot].size,&sequence));return sequence;
}
static void save_ok(void) {assert(kui_game_launch_report_save(&launch,NULL,NULL));assert(opened==closed);}
static void recover_and_alternate(void) {
    reset();save_ok();assert(last_written==0 && slot_sequence(0)==1);
    assert(parents[0] && parents[1] && parents[2]);snapshot[0]=disk[0];
    clear_faults();save_ok();assert(last_written==1 && slot_sequence(1)==2);
    assert(!memcmp(&snapshot[0],&disk[0],sizeof(disk[0])));snapshot[1]=disk[1];
    clear_faults();save_ok();assert(last_written==0 && slot_sequence(0)==3);
    assert(!memcmp(&snapshot[1],&disk[1],sizeof(disk[1])));
    /* Corrupt and torn newest slots must be replaced using the last complete
     * generation's sequence, while preserving that authoritative generation. */
    for(unsigned current=0;current<2;current++) for(unsigned torn=0;torn<2;torn++) {
        reset();seed(current,7,&launch);seed(1-current,8,&launch);snapshot[current]=disk[current];
        if(torn) disk[1-current].size/=2;else disk[1-current].data[0]^=1;
        clear_faults();save_ok();assert(last_written==1-current && slot_sequence(1-current)==8);
        assert(!memcmp(&snapshot[current],&disk[current],sizeof(disk[current])));
    }
    reset();disk[0].exists=disk[1].exists=true;disk[0].size=1;disk[1].size=sizeof(disk[1].data);
    save_ok();assert(last_written==0 && slot_sequence(0)==1);
    /* Identical ties choose A; different content at one sequence is refused. */
    reset();seed(0,9,&launch);disk[1]=disk[0];snapshot[0]=disk[0];
    save_ok();assert(last_written==1 && slot_sequence(1)==10);
    assert(!memcmp(&snapshot[0],&disk[0],sizeof(disk[0])));
    reset();seed(0,9,&launch);struct kui_game_launch_report other=launch;other.title="Different game";seed(1,9,&other);
    memcpy(snapshot,disk,sizeof(disk));
    assert(!kui_game_launch_report_save(&launch,NULL,NULL));
    assert(!write_opens && opened==closed && !memcmp(snapshot,disk,sizeof(disk)));
    puts("PASS launch report: independent slots, alternating generations, corrupt/torn recovery and equal-sequence conflict refusal");
}
static void storage_faults(void) {
    static const enum fault faults[]={READ_OPEN,READ_ERROR,READ_SHORT,READ_CLOSE,WRITE_OPEN,WRITE_ERROR,
        WRITE_SHORT,SYNC_ERROR,WRITE_CLOSE,VERIFY_OPEN,VERIFY_ERROR,VERIFY_CORRUPT,VERIFY_SHORT,VERIFY_CLOSE,
        PARENT_ERROR,MKDIR_ERROR};
    for(unsigned current=0;current<2;current++) for(unsigned i=0;i<sizeof(faults)/sizeof(faults[0]);i++) {
        reset();seed(current,7,&launch);seed(1-current,6,&launch);snapshot[current]=disk[current];failure=faults[i];
        assert(!kui_game_launch_report_save(&launch,NULL,NULL));
        assert(!memcmp(&snapshot[current],&disk[current],sizeof(disk[current])) && opened==closed);
        if(write_opens) assert(last_written==1-current);
    }
    reset();seed(0,UINT64_MAX,&launch);snapshot[0]=disk[0];
    assert(!kui_game_launch_report_save(&launch,NULL,NULL));
    assert(!write_opens && opened==closed && !memcmp(&snapshot[0],&disk[0],sizeof(disk[0])));
    reset();seed(0,7,&launch);parents[0]=true;file_in_parent=true;snapshot[0]=disk[0];
    assert(!kui_game_launch_report_save(&launch,NULL,NULL));
    assert(!write_opens && opened==closed && !memcmp(&snapshot[0],&disk[0],sizeof(disk[0])));
    /* Cancel first, after reads, before each missing directory, and immediately
     * before write-open. Every checkpoint preserves both existing slot bytes. */
    for(unsigned checkpoint=1;checkpoint<=6;checkpoint++) {
        reset();cancel_at=checkpoint;seed(0,7,&launch);memcpy(snapshot,disk,sizeof(disk));
        assert(!kui_game_launch_report_save(&launch,NULL,cancelled));
        assert(cancel_calls==checkpoint && !write_opens && opened==closed && !memcmp(snapshot,disk,sizeof(disk)));
    }
    reset();struct kui_game_launch_report invalid=launch;invalid.image_path="/games/../disc.gdi";
    assert(!kui_game_launch_report_save(&invalid,NULL,NULL));assert(!read_opens && !write_opens);
    assert(!kui_game_launch_report_save(NULL,NULL,NULL));assert(!read_opens && !write_opens);
    puts("PASS launch report: open/read/short/write/sync/close/reread faults, cancellation and sequence exhaustion preserve authority");
}
int main(void) {
    independent_format();grammar();text_and_bounds();recover_and_alternate();storage_faults();return 0;
}
