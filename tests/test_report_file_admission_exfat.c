/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse only the independent sparse disk constructor; the old VMU tests
 * remain available as an uncalled function. No formatter or admission code
 * constructs this filesystem. */
#define main unused_vmu_exfat_main
#include "test_vmu_fs_admission.c"
#undef main
#include "kui/report_file.h"
#include <stdlib.h>
#include "kui/hash.h"

static struct kui_report_file_descriptor report;
static struct kui_report_file_input request;
static void report_fresh(void) {
    fresh();
    file_entry(disk.dirs[1],"reports",16u,6u,4096u,3u);
    memset(disk.dirs[2],0,512u);
    file_entry(disk.dirs[2],"TC.TXT",32u,20u,KUI_REPORT_FILE_BYTES,3u);
    memset(disk.dirs[3],0,512u);
    request=(struct kui_report_file_input){.read_sector=read_sector,.context=&disk,
        .card_sectors=disk.input.card_sectors,.partition_first=disk.input.partition_first,
        .partition_end=disk.input.partition_end,.high_capacity=1u,.file_name="TC.TXT",.profile=3u};
    memcpy(request.cid,disk.input.cid,16u);
}
static int report_collect(void) {
    return kui_report_file_admit(&request,&report,&disk.work);
}
static void report_refusal(void) {
    memset(&report,0xa5,sizeof(report));
    int result=report_collect();assert(result && result==disk.work.stats.result);
    const uint8_t *p=(const void *)&report;
    for(size_t i=0;i<sizeof(report);i++)assert(!p[i]);
    assert(disk.calls<=KUI_VMU_FS_MAX_READS);
}
static void seal(struct kui_report_file_descriptor *d) {
    d->checksum=0u;d->checksum=kui_crc32_capture(0u,d,sizeof(*d));
}
static void reject_semantic(struct kui_report_file_descriptor d) {
    seal(&d);assert(!kui_report_file_descriptor_valid(&d));
}
static void descriptor_semantics(void) {
    report_fresh();assert(!report_collect());
    struct kui_report_file_descriptor d=report;seal(&d);
    assert(d.checksum==report.checksum && kui_report_file_descriptor_valid(&d));
    d=report;d.profile=0u;reject_semantic(d);
    d=report;d.card_sectors=0u;reject_semantic(d);
    d=report;d.card_sectors=(UINT64_C(1)<<32)+1u;reject_semantic(d);
    d=report;d.high_capacity=2u;reject_semantic(d);
    d=report;d.high_capacity=0u;d.card_sectors=(UINT64_C(1)<<23)+1u;reject_semantic(d);
    d=report;memset(d.cid,0,16u);reject_semantic(d);
    d=report;memset(d.cid,0xff,16u);reject_semantic(d);
    d=report;d.reserved[18]=1u;reject_semantic(d);
    d=report;d.extent_count=0u;reject_semantic(d);
    d=report;d.extent_count=17u;reject_semantic(d);
    d=report;d.extents[0].lba=0u;reject_semantic(d);
    d=report;d.extents[0].lba=(uint32_t)d.card_sectors;reject_semantic(d);
    d=report;d.extents[0].blocks=0u;reject_semantic(d);
    d=report;d.extents[0].blocks=63u;reject_semantic(d);
    d=report;d.extents[0].blocks=65u;reject_semantic(d);
    d=report;d.extents[1].lba=123u;reject_semantic(d);
    d=report;d.extent_count=2u;d.extents[0].blocks=32u;
    d.extents[1]=(struct kui_report_file_extent){d.extents[0].lba+31u,32u};reject_semantic(d);
    d.extents[1].lba=d.extents[0].lba+32u;reject_semantic(d); /* Must coalesce. */
    d=report;d.extent_count=16u;
    for(unsigned i=0;i<16u;i++)d.extents[i]=(struct kui_report_file_extent){100u+10u*i,4u};
    seal(&d);assert(kui_report_file_descriptor_valid(&d));
}
static void report_ownership(void) {
    report_fresh();assert(!report_collect());
    assert(kui_report_file_descriptor_valid(&report));
    assert(report.profile==3u && report.extent_count==1u);
    assert(report.extents[0].lba==lba(20u) && report.extents[0].blocks==64u);
    assert(!memcmp(report.cid,request.cid,16u));
    for(unsigned byte=0;byte<sizeof(report);byte++) {
        struct kui_report_file_descriptor corrupt=report;
        ((uint8_t *)&corrupt)[byte]^=1u;
        assert(!kui_report_file_descriptor_valid(&corrupt));
    }
    report_fresh();file_entry(disk.dirs[2],"TC.TXT",32u,20u,KUI_REPORT_FILE_BYTES,1u);
    for(unsigned i=20u;i<23u;i++)disk.fat[i]=i+1u;
    disk.fat[23u]=100u;
    for(unsigned i=100u;i<104u;i++){allocated(i,1);disk.fat[i]=i<103u?i+1u:UINT32_MAX;}
    assert(!report_collect() && report.extent_count==2u);
    assert(report.extents[0].blocks==32u && report.extents[1].lba==lba(100u));
    report_fresh();file_entry(disk.dirs[0]+160u,"VMU.VMD",32u,20u,16u,1u);report_refusal();
    report_fresh();file_entry(disk.dirs[0]+160u,"alias",32u,21u,16u,1u);report_refusal();
    report_fresh();file_entry(disk.dirs[0]+160u,"alias",32u,19u,8192u,3u);allocated(19u,1);report_refusal();
    report_fresh();allocated(100u,1);disk.fat[100u]=20u;
    file_entry(disk.dirs[0]+160u,"live",32u,100u,8192u,1u);report_refusal();
    report_fresh();disk.fat[100u]=20u;assert(!report_collect()); /* Unowned exFAT stale FAT word. */
    report_fresh();struct kui_vmu_fs_range protected={lba(23u)+2u,1u};
    request.forbidden=&protected;request.forbidden_count=1u;report_refusal();
    for(unsigned c=2u;c<=6u;c++){report_fresh();allocated(c,0);report_refusal();}
    report_fresh();allocated(23u,0);report_refusal();
    for(unsigned attributes=1u;attributes<=16u;attributes<<=1u) {
        report_fresh();file_entry(disk.dirs[2],"TC.TXT",32u|attributes,20u,KUI_REPORT_FILE_BYTES,3u);report_refusal();
    }
    report_fresh();file_entry(disk.dirs[2],"TC.TXT",32u,20u,KUI_REPORT_FILE_BYTES-1u,3u);report_refusal();
    report_fresh();file_entry(disk.dirs[2],"OTHER.TXT",32u,20u,KUI_REPORT_FILE_BYTES,3u);report_refusal();
    report_fresh();disk.upcase[194u]^=1u;report_refusal();
    report_fresh();disk.dirs[2][2u]^=1u;report_refusal();
    report_fresh();disk.boot[11u][3u]^=1u;report_refusal();
    report_fresh();disk.boot[0][106u]=4u;boot_checksum();report_refusal();
    report_fresh();assert(!report_collect());unsigned calls=disk.calls;
    for(unsigned failed=1u;failed<=calls;failed++) {
        report_fresh();disk.fail_at=failed;report_refusal();
    }
    const char *bad[]={"", "../TC.TXT", "tc.txt", "TOO-LONG-NAME.TXT", "TC.TXT/", "TC.TXT ", "TC..TXT"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        report_fresh();request.file_name=bad[i];report_refusal();assert(!disk.calls);
    }
    static const char complete_name[]="TC.TXT";
    for(unsigned size=0u;size<sizeof(complete_name)-1u;size++) {
        report_fresh();char *short_name=malloc(size+1u);assert(short_name);
        memcpy(short_name,complete_name,size);short_name[size]=0;
        request.file_name=short_name;report_refusal();assert(!disk.calls);free(short_name);
    }
}

/* The sink sees a sector device only while its terminal claim is held.
 * Every address is checked against the independently admitted file map. */
static struct {
    uint8_t bytes[KUI_REPORT_FILE_BYTES];
    unsigned begin,identity,read,write,sync,end,held,unhealthy;
    unsigned fail_kind,fail_at,corrupt_read;
} sink;
enum {FAIL_BEGIN=1,FAIL_IDENTITY,FAIL_READ,FAIL_WRITE,FAIL_SYNC,FAIL_END};
static uint32_t sink_offset(uint32_t sector) {
    uint32_t logical=0u;
    for(unsigned i=0;i<report.extent_count;i++) {
        if(sector>=report.extents[i].lba && sector-report.extents[i].lba<report.extents[i].blocks)
            return (logical+sector-report.extents[i].lba)*512u;
        logical+=report.extents[i].blocks;
    }
    assert(!"writer touched a non-report sector");return 0u;
}
static int failed(unsigned kind,unsigned count) {return sink.fail_kind==kind && count==sink.fail_at;}
static int sink_begin(void *context) {
    assert(context==&sink && !sink.held);++sink.begin;
    if(failed(FAIL_BEGIN,sink.begin))return -1;
    sink.held=1u;return 0;
}
static int sink_identity(void *context,uint8_t cid[16],uint64_t *sectors,uint32_t *capacity) {
    assert(context==&sink && sink.held);++sink.identity;
    memcpy(cid,report.cid,16u);*sectors=report.card_sectors;*capacity=report.high_capacity;
    if(failed(FAIL_IDENTITY,sink.identity))cid[0]^=1u;
    return 0;
}
static int sink_read(void *context,uint32_t sector,uint8_t out[512]) {
    assert(context==&sink && sink.held);++sink.read;
    if(failed(FAIL_READ,sink.read))return -1;
    memcpy(out,sink.bytes+sink_offset(sector),512u);
    if(sink.corrupt_read==sink.read)out[17]^=1u;
    return 0;
}
static int sink_write(void *context,uint32_t sector,const uint8_t in[512]) {
    assert(context==&sink && sink.held);++sink.write;
    uint32_t offset=sink_offset(sector);
    if(failed(FAIL_WRITE,sink.write))return -1;
    memcpy(sink.bytes+offset,in,512u);return 0;
}
static int sink_sync(void *context) {
    assert(context==&sink && sink.held);++sink.sync;
    return failed(FAIL_SYNC,sink.sync)?-1:0;
}
static int sink_end(void *context,bool healthy) {
    assert(context==&sink && sink.held);++sink.end;sink.held=0u;
    if(!healthy)++sink.unhealthy;
    return failed(FAIL_END,sink.end)?-1:0;
}
static struct kui_report_file_work sink_work;
static const struct kui_report_file_io io={&sink,sink_begin,sink_identity,sink_read,sink_write,sink_sync,sink_end};
static const char text[]="K-UI report fixture\nprofile: TC\nDATA bytes: 00012345\n";
static int save(bool frozen) {return kui_report_file_save(&report,frozen,text,sizeof(text)-1u,&io,&sink_work);}
static void sink_fresh(void) {memset(&sink,0,sizeof(sink));memset(sink.bytes,'?',sizeof(sink.bytes));}
static void writer_contracts(void) {
    report_fresh();assert(!report_collect());sink_fresh();
    assert(save(false)==KUI_REPORT_FILE_NOT_FROZEN);
    assert(!sink.begin && !sink.identity && !sink.read && !sink.write && !sink.sync && !sink.end);
    sink_fresh();struct kui_report_file_descriptor valid=report;report.extents[0].lba^=1u;
    assert(save(true)==KUI_REPORT_FILE_ARGUMENT && !sink.begin);report=valid;
    sink_fresh();assert(save(true)==KUI_REPORT_FILE_OK);
    assert(sink.begin==1u && sink.identity && sink.end==1u && !sink.unhealthy && !sink.held);
    assert(!memcmp(sink.bytes,text,sizeof(text)-1u));
    assert(sink.write>=65u && sink.read>=64u && sink.sync>=2u);
    char footer_text[513];memcpy(footer_text,sink.bytes+KUI_REPORT_TEXT_BYTES,512u);footer_text[512]=0;
    unsigned profile,bytes,crc;
    assert(sscanf(footer_text,"KUI REPORT COMPLETE V1\nPROFILE=%x\nBYTES=%x\nCRC32=%x",&profile,&bytes,&crc)==3);
    assert(profile==3u && bytes==sizeof(text)-1u && crc==kui_crc32_capture(0u,text,sizeof(text)-1u));
    unsigned writes=sink.write,reads=sink.read,syncs=sink.sync,identities=sink.identity;
    /* Every uncertain acquired I/O error releases exactly once and reports
     * failure. A refused claim never releases or accesses a data sector. */
    for(unsigned kind=FAIL_BEGIN;kind<=FAIL_END;kind++) {
        unsigned attempts=kind==FAIL_WRITE?writes:kind==FAIL_READ?reads:kind==FAIL_SYNC?syncs:
            kind==FAIL_IDENTITY?identities:1u;
        for(unsigned n=1u;n<=attempts;n++) {
            sink_fresh();sink.fail_kind=kind;sink.fail_at=n;
            assert(save(true)!=KUI_REPORT_FILE_OK);
            assert(sink.begin==1u && !sink.held);
            if(kind==FAIL_BEGIN)assert(!sink.end && !sink.write && !sink.read);
            else assert(sink.end==1u);
            if(kind==FAIL_IDENTITY && n==1u)assert(!sink.write && !sink.read);
        }
    }
    sink_fresh();
    const char invalid_text[]={ 'x',1,'y' };
    assert(kui_report_file_save(&report,true,invalid_text,sizeof(invalid_text),&io,&sink_work)==KUI_REPORT_FILE_ARGUMENT);
    assert(!sink.begin);
    assert(kui_report_file_save(&report,true,(const char *)&sink_work,1u,&io,&sink_work)==KUI_REPORT_FILE_ARGUMENT);
    assert(!sink.begin);
    assert(kui_report_file_save(&report,true,text,KUI_REPORT_TEXT_BYTES+1u,&io,&sink_work)==KUI_REPORT_FILE_ARGUMENT);
    assert(!sink.begin);
    sink_fresh();sink.corrupt_read=1u;
    assert(save(true)==KUI_REPORT_FILE_VERIFY && sink.end==1u && sink.unhealthy==1u && !sink.held);
    /* A fragmented descriptor still permits only its own logical file. */
    report_fresh();file_entry(disk.dirs[2],"TC.TXT",32u,20u,KUI_REPORT_FILE_BYTES,1u);
    for(unsigned i=20u;i<23u;i++)disk.fat[i]=i+1u;
    disk.fat[23u]=100u;
    for(unsigned i=100u;i<104u;i++){allocated(i,1);disk.fat[i]=i<103u?i+1u:UINT32_MAX;}
    assert(!report_collect() && report.extent_count==2u);sink_fresh();assert(save(true)==KUI_REPORT_FILE_OK);
    assert(!memcmp(sink.bytes,text,sizeof(text)-1u));
    /* A valid physical last sector is0xFFFFFFFF, not an error sentinel. */
    report_fresh();assert(!report_collect());report.card_sectors=UINT64_C(1)<<32;
    report.extents[0]=(struct kui_report_file_extent){UINT32_C(0xffffffc0),64u};
    seal(&report);assert(kui_report_file_descriptor_valid(&report));
    sink_fresh();assert(save(true)==KUI_REPORT_FILE_OK);
    report.extent_count=2u;report.extents[0]=(struct kui_report_file_extent){100u,32u};
    report.extents[1]=(struct kui_report_file_extent){UINT32_C(0xffffffe0),32u};
    seal(&report);assert(kui_report_file_descriptor_valid(&report));
    sink_fresh();assert(save(true)==KUI_REPORT_FILE_OK);
}

static struct {
    uint8_t text[1307];
    size_t bytes;
    unsigned calls,fail_at,short_at,change_when_held,change_on_readback;
} source;
static size_t source_read(void *context,size_t offset,uint8_t *out,size_t bytes) {
    assert(context==&source && bytes && bytes<=512u && offset<source.bytes && bytes<=source.bytes-offset);
    ++source.calls;
    if(source.fail_at==source.calls)return SIZE_MAX;
    if(source.short_at==source.calls)return bytes-1u;
    memcpy(out,source.text+offset,bytes);
    if((source.change_when_held && sink.held) || (source.change_on_readback && sink.sync>=2u))out[bytes-1u]^=1u;
    return bytes;
}
static void source_fresh(void) {
    memset(&source,0,sizeof(source));source.bytes=sizeof(source.text);
    for(size_t i=0;i<source.bytes;i++)source.text[i]=(uint8_t)('A'+i%26u);
    source.text[511]='\n';source.text[1023]='\n';source.text[source.bytes-1u]='\n';
}
static int save_source(bool frozen) {
    return kui_report_file_save_source(&report,frozen,source.bytes,source_read,&source,&io,&sink_work);
}
static void incomplete(void) {
    static const char pending[]="KUI REPORT INCOMPLETE V1\n";
    assert(!memcmp(sink.bytes+KUI_REPORT_TEXT_BYTES,pending,sizeof(pending)-1u));
}
static void streaming_contracts(void) {
    report_fresh();assert(!report_collect());source_fresh();sink_fresh();
    assert(save_source(false)==KUI_REPORT_FILE_NOT_FROZEN);
    assert(!source.calls && !sink.begin && !sink.read && !sink.write);
    source_fresh();sink_fresh();
    assert(kui_report_file_save_source(&report,true,KUI_REPORT_TEXT_BYTES+1u,source_read,&source,&io,&sink_work)==KUI_REPORT_FILE_ARGUMENT);
    assert(!source.calls && !sink.begin);
    struct kui_report_file_descriptor original=report;report.magic^=1u;
    assert(save_source(true)==KUI_REPORT_FILE_ARGUMENT && !source.calls && !sink.begin);report=original;
    assert(kui_report_file_save_source(&report,true,source.bytes,NULL,&source,&io,&sink_work)==KUI_REPORT_FILE_ARGUMENT);
    assert(!source.calls && !sink.begin);
    source_fresh();sink_fresh();
    assert(kui_report_file_save(&report,true,(const char *)source.text,source.bytes,&io,&sink_work)==KUI_REPORT_FILE_OK);
    uint8_t expected[KUI_REPORT_FILE_BYTES];memcpy(expected,sink.bytes,sizeof(expected));
    source_fresh();sink_fresh();assert(save_source(true)==KUI_REPORT_FILE_OK);
    assert(!memcmp(expected,sink.bytes,sizeof(expected)) && !sink.held && sink.end==1u && !sink.unhealthy);
    unsigned calls=source.calls;assert(calls>=9u);
    /* Refusing or shortening any callback must never publish completion for
     * a partial render, including failures after the SD claim was acquired. */
    for(unsigned short_read=0u;short_read<2u;short_read++)for(unsigned n=1u;n<=calls;n++) {
        source_fresh();sink_fresh();
        if(short_read)source.short_at=n;else source.fail_at=n;
        assert(save_source(true)==KUI_REPORT_FILE_SOURCE && !sink.held);
        if(sink.begin){assert(sink.end==1u && sink.unhealthy==1u);incomplete();}
        else assert(!sink.end && !sink.write && !sink.read);
    }
    source_fresh();sink_fresh();source.change_when_held=1u;
    assert(save_source(true)!=KUI_REPORT_FILE_OK && sink.end==1u && sink.unhealthy==1u && !sink.held);
    incomplete();
    source_fresh();sink_fresh();source.change_on_readback=1u;
    assert(save_source(true)!=KUI_REPORT_FILE_OK && sink.end==1u && sink.unhealthy==1u && !sink.held);
    incomplete();
    source_fresh();sink_fresh();source.bytes=0u;
    assert(save_source(true)==KUI_REPORT_FILE_OK && !source.calls);
}

int main(void) {
    descriptor_semantics();report_ownership();writer_contracts();streaming_contracts();
    puts("report exFAT ownership and terminal sink contracts OK");return 0;
}
