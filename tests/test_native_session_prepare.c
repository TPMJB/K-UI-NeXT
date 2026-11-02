/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_session_prepare.h"
#include "kui/native_session.h"
#include "kui/native_return.h"
#include "kui/media.h"
#include "kui/storage_policy.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t runtime[128];
static struct kui_retail_manifest member;
static unsigned fault,opens,closes,maps;
static bool cancelled;
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(n>>(i*8u));}
static bool cancel(void) {return cancelled;}
static void log_line(const char *message,...) {(void)message;}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    assert(!strcmp(path,KUI_RUNTIME_PATH) && mode==FA_READ);++opens;
    memset(file,0,sizeof(*file));file->obj.objsize=sizeof(runtime);return fault==1u?FR_DISK_ERR:FR_OK;
}
FRESULT f_read(FIL *file,void *data,UINT want,UINT *got) {
    assert(want<=sizeof(runtime)-file->fptr);
    *got=fault==2u?want-1u:want;
    memcpy(data,runtime+file->fptr,*got);file->fptr+=*got;return FR_OK;
}
FRESULT f_close(FIL *file) {(void)file;++closes;return fault==3u?FR_DISK_ERR:FR_OK;}
bool kui_native_session_map_file(FATFS *fs,const struct kui_volume *v,const char *path,
    struct kui_retail_extent *extents,unsigned cap,unsigned *count,uint64_t *bytes,
    const char **problem,kui_cancel_fn stop) {
    (void)fs;(void)v;(void)problem;assert(!strcmp(path,KUI_RUNTIME_PATH) && cap==KUI_NATIVE_RETURN_EXTENTS && !stop());
    extents[0]=(struct kui_retail_extent){0u,fault==4u?50u:2000u,1u};*count=1u;*bytes=sizeof(runtime);return true;
}
bool kui_native_session_map_gdi(FATFS *fs,const struct kui_volume *v,const char *path,uint32_t reader,
    struct kui_retail_manifest *out,char version[7],const char **problem,kui_log_fn log,kui_cancel_fn stop) {
    (void)fs;(void)v;(void)problem;(void)log;assert(reader==KUI_RETAIL_READER_ASYNC && !stop());
    assert(!strcmp(path,"/Games/Example Disc 2/disc.gdi"));++maps;
    *out=member;strcpy(version,fault==7u?"V2.000":"V1.000");
    if(fault==5u)strcpy(out->product,"OTHER");
    if(fault==6u)strcpy(out->region,"J");
    return fault!=8u;
}
static void fixture(void) {
    fault=opens=closes=maps=0u;cancelled=false;
    memset(runtime,0,sizeof(runtime));memcpy(runtime,"KUIRUN1\0",8u);
    put32(runtime+8u,1u);put32(runtime+12u,64u);put32(runtime+16u,64u);
    put32(runtime+20u,KUI_RUNTIME_ADDRESS);put32(runtime+24u,KUI_RUNTIME_ADDRESS);
    put32(runtime+28u,512u);memcpy(runtime+40u,"0123456789ab",12u);
    const struct kui_storage_boot_marker marker=KUI_STORAGE_BOOT_INITIALIZER;
    memcpy(runtime+64u,&marker,sizeof(marker));
    put32(runtime+32u,kui_crc32(0,runtime+64u,64u));put32(runtime+60u,kui_crc32(0,runtime,60u));
    memset(&member,0,sizeof(member));member.card_sectors=100000u;
    member.partition_start=100u;member.partition_end=100000u;
    member.track_count=1u;member.extent_count=1u;member.session_lba=45000u;
    member.boot_lba=45020u;member.boot_bytes=128u;member.storage_transport=KUI_STORAGE_SCI;
    member.reader=KUI_RETAIL_READER_ASYNC;member.flags=KUI_RETAIL_IMAGE_BOOT_CRC;
    strcpy(member.title,"EXAMPLE");strcpy(member.product,"T1234N");strcpy(member.region,"U");strcpy(member.bootfile,"1ST_READ.BIN");
    member.slots[0].track=(struct kui_retail_track){45000u,45100u,1u,1u,4u|KUI_RETAIL_TRACK_COOKED};
    member.slots[1].extent=(struct kui_retail_extent){0u,3000u,400u};
    assert(kui_retail_manifest_validate(&member)==KUI_GAME_OK);
}
static void blank(uint8_t *wire) {
    memset(wire,0,KUI_NATIVE_SESSION_WIRE_BYTES);memcpy(wire,KUI_NATIVE_SESSION_MAGIC,8u);
    put32(wire+8u,KUI_NATIVE_SESSION_VERSION);put32(wire+12u,KUI_NATIVE_SESSION_WIRE_BYTES);
}
int main(void) {
    uint8_t *wire=malloc(KUI_NATIVE_SESSION_WIRE_BYTES),*before=malloc(KUI_NATIVE_SESSION_WIRE_BYTES);assert(wire && before);
    struct kui_runtime_image package={.data=wire,.info={.payload_bytes=KUI_NATIVE_SESSION_WIRE_BYTES}};
    FATFS fs={0};struct kui_volume volume={.start=100u,.count=99900u};const char *problem=NULL;
    struct kui_native_session_request request={.count=2u,.selected=0u};
    strcpy(request.paths[0],"/Games/Example Disc 1/disc.gdi");strcpy(request.paths[1],"/Games/Example Disc 2/disc.gdi");
    fixture();blank(wire);
    assert(kui_native_session_prepare(&fs,&volume,request.paths[0],&request,&member,"V1.000",&package,&problem,log_line,cancel));
    assert(kui_native_session_wire_valid(wire,KUI_NATIVE_SESSION_WIRE_BYTES,true) && opens==1u && closes==1u && maps==1u);
    struct kui_native_return_plan plan;
    assert(kui_native_return_decode(&plan,wire+KUI_NATIVE_SESSION_RETURN_OFFSET,KUI_NATIVE_SESSION_RETURN_BYTES));
    assert(plan.file_bytes==sizeof(runtime) && plan.extents[0].card_lba==2000u);
    fixture();blank(wire);
    assert(kui_native_session_prepare(&fs,&volume,request.paths[0],NULL,&member,"V1.000",&package,&problem,log_line,cancel));
    assert(kui_native_session_wire_valid(wire,KUI_NATIVE_SESSION_WIRE_BYTES,true) && opens==1u && closes==1u && !maps);
    assert(wire[KUI_NATIVE_SESSION_FLAGS_OFFSET]==KUI_NATIVE_SESSION_FLAG_RETURN &&
           !wire[KUI_NATIVE_SESSION_COUNT_OFFSET] && !wire[KUI_NATIVE_SESSION_SELECTED_OFFSET]);
    for(unsigned mode=1u;mode<=8u;mode++) {
        fixture();fault=mode;blank(wire);memcpy(before,wire,KUI_NATIVE_SESSION_WIRE_BYTES);
        assert(!kui_native_session_prepare(&fs,&volume,request.paths[0],&request,&member,"V1.000",&package,&problem,log_line,cancel));
        assert(!memcmp(wire,before,KUI_NATIVE_SESSION_WIRE_BYTES));
    }
    fixture();runtime[127]^=1u;blank(wire);memcpy(before,wire,KUI_NATIVE_SESSION_WIRE_BYTES);
    assert(!kui_native_session_prepare(&fs,&volume,request.paths[0],&request,&member,"V1.000",&package,&problem,log_line,cancel));
    assert(!memcmp(wire,before,KUI_NATIVE_SESSION_WIRE_BYTES));
    fixture();blank(wire);cancelled=true;
    assert(!kui_native_session_prepare(&fs,&volume,request.paths[0],&request,&member,"V1.000",&package,&problem,log_line,cancel));
    fixture();blank(wire);
    member.slots[2]=member.slots[1];member.slots[0].track.first_extent=2u;
    member.slots[1].track=(struct kui_retail_track){45100u,45101u,3u,0u,0u};member.track_count=2u;
    assert(kui_retail_manifest_validate(&member)==KUI_GAME_OK);
    assert(!kui_native_session_prepare(&fs,&volume,request.paths[0],&request,&member,"V1.000",&package,&problem,log_line,cancel));
    assert(!opens && !maps);
    assert(!kui_native_session_prepare(&fs,&volume,request.paths[0],NULL,&member,"V1.000",&package,&problem,log_line,cancel));
    assert(!opens && !maps);
    fixture();memset(wire,0,KUI_NATIVE_SESSION_WIRE_BYTES);
    assert(kui_native_session_prepare(&fs,&volume,request.paths[0],&request,&member,"V1.000",&package,&problem,log_line,cancel));
    assert(!opens && !maps);
    free(wire);free(before);
    puts("PASS session preparation: checked return runtime, bounded mappings, revision/region/product gates, atomic failure and ordinary-module no-op");
    return 0;
}
