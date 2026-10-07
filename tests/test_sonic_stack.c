/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

/* Execute production C against shared P1/P2 RAM. SH wrappers are separately
 * compiled/audited; this host fixture verifies their actual 21-word frames.
 * All owner bytes are synthetic, with an independently solved CRC suffix. */
#include "../src/loader/retail_stage.c"

#define OWNER_BYTES 6751168u
#define OWNER_CRC 0x73f4277bu
#define RAM_BYTES 0x1000000u
#define FRAME_ADDRESS 0x8c00f300u
#define PRIVATE_BOTTOM 0x8ceb0000u
#define PRIVATE_TOP (PRIVATE_BOTTOM+SONIC_STACK_OWNER_BYTES)
#define BOOK_BOTTOM 0x8ceb9000u
#define STATE_ADDRESS 0x8ceba000u
#define STAGE_END 0x8cec0000u
#define TEST_CCR 0x00000909u
#define MAP_ADDRESS 0x8c00b080u
#define ASYNC_MAP_ADDRESS 0x8c00b980u
static uint8_t *owner;
static uint8_t reader_blob[256];
static uint32_t entry_frame[21];
static jmp_buf terminal;
static int expect_terminal;
static struct {
    uint32_t allocation[4];unsigned publications,reads;
    uint32_t address[8];size_t bytes[8];
    unsigned wait_point;int timeout;
    uint32_t p2_override,p2_value;unsigned p2_reads;
} hooks;
static struct {
    unsigned restores,lines,values,pauses;
    uint32_t detail;
    const char *line[12],*legend[5];uint32_t row[5][5];
} calls;
static uint8_t video[2048],saved_video[2048];

void kui_retail_sonic_g2_fifo(void) {abort();}
void kui_retail_sonic_g2_busy(void) {abort();}
void kui_retail_sonic_scope_entry(void) {abort();}
void kui_retail_sonic_scope_return(void) {abort();}
void kui_retail_sonic_asset_guard(void) {abort();}
uint32_t kui_retail_sonic_stack_allocation(unsigned allocation) {
    assert(allocation<4u);return hooks.allocation[allocation];
}
void kui_retail_sonic_stack_publish(uint32_t address,size_t bytes) {
    assert(hooks.publications<8u);
    hooks.address[hooks.publications]=address;
    hooks.bytes[hooks.publications++]=bytes;
}
uint32_t kui_retail_sonic_stack_read(uint32_t address) {
    if(address>=0xac000000u && address<0xad000000u) {
        assert(!(address&3u) && !calls.restores);++hooks.p2_reads;
        return address==hooks.p2_override?hooks.p2_value:
            *(volatile const uint32_t *)(uintptr_t)address;
    }
    assert(address==0xa05f688cu);
    unsigned n=hooks.reads++;
    uint32_t mask=hooks.wait_point==0u?32u:1u;
    return hooks.timeout || n<2u?mask:(mask==32u?1u:32u);
}
void retail_display_restore(const struct retail_display_state *state) {
    (void)state;++calls.restores;memset(video,0,sizeof(video));
}
void retail_display_line(const char *line) {
    assert(calls.lines<12u);calls.line[calls.lines++]=line;
    video[calls.lines]=(uint8_t)line[0];
    if(!strcmp(line,"STORAGE WAS READ ONLY")) {
        assert(expect_terminal);longjmp(terminal,1);
    }
}
void retail_display_hex(const char *legend,uint32_t value) {
    if(!strcmp(legend,"DETAIL")) calls.detail=value;
}
void retail_display_values(const char *legend,const uint32_t *values,unsigned count) {
    assert(count==5u && calls.values<5u);calls.legend[calls.values]=legend;
    memcpy(calls.row[calls.values++],values,5u*sizeof(uint32_t));
}
void retail_display_pause(uint32_t frames) {assert(frames==30u);++calls.pauses;}

static uint32_t independent_crc(uint32_t previous,const uint8_t *bytes,size_t count) {
    uint32_t crc=~previous;
    for(size_t i=0;i<count;i++) {
        crc^=bytes[i];
        for(unsigned bit=0;bit<8u;bit++) crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return ~crc;
}
static void force_crc(uint8_t *bytes,size_t count,uint32_t target) {
    uint8_t suffix[4]={0};uint32_t basis[32]={0},combination[32]={0};
    uint32_t prefix=independent_crc(0,bytes,count-4u);
    uint32_t base=independent_crc(prefix,suffix,sizeof(suffix));
    for(unsigned i=0;i<32u;i++) {
        memset(suffix,0,sizeof(suffix));suffix[i/8u]=(uint8_t)(1u<<(i%8u));
        uint32_t value=independent_crc(prefix,suffix,sizeof(suffix))^base,mask=1u<<i;
        for(int bit=31;bit>=0;bit--) if(value&(1u<<bit)) {
            if(basis[bit]) {value^=basis[bit];mask^=combination[bit];}
            else {basis[bit]=value;combination[bit]=mask;break;}
        }
    }
    uint32_t wanted=target^base,solution=0;
    for(int bit=31;bit>=0;bit--) if(wanted&(1u<<bit)) {
        assert(basis[bit]);wanted^=basis[bit];solution^=combination[bit];
    }
    assert(!wanted);
    for(unsigned i=0;i<4u;i++) bytes[count-4u+i]=(uint8_t)(solution>>(i*8u));
    assert(independent_crc(0,bytes,count)==target);
}
static uint32_t *fixture(int uncached_frame) {
    memset((void *)(uintptr_t)0x8c000000u,0x55,RAM_BYTES);
    memset(&hooks,0,sizeof(hooks));memset(&calls,0,sizeof(calls));
    hooks.allocation[0]=PRIVATE_BOTTOM;hooks.allocation[1]=BOOK_BOTTOM;
    hooks.allocation[2]=STATE_ADDRESS;hooks.allocation[3]=STAGE_END;
    memset(&kui_retail_sonic_stack_state,0,sizeof(kui_retail_sonic_stack_state));
    uint8_t *boot=(uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS;
    memcpy(boot,owner,OWNER_BYTES);memcpy(original_entry,owner,sizeof(original_entry));
    memset(boot,0xcc,ENTRY_PATCH_BYTES);
    exec_bytes=OWNER_BYTES;boot_crc=OWNER_CRC;memset(&manifest,0,sizeof(manifest));
    manifest.ip_crc32=0x22de24d8u;manifest.boot_crc32=OWNER_CRC;
    manifest.card_sectors=0x100000u;manifest.partition_start=1u;manifest.partition_end=0x100000u;
    manifest.track_count=1u;manifest.extent_count=1u;manifest.session_lba=45000u;
    manifest.boot_lba=45016u;manifest.boot_bytes=OWNER_BYTES;
    strcpy(manifest.title,"Synthetic owner");strcpy(manifest.bootfile,"1ST_READ.BIN");
    manifest.slots[0].track=(struct kui_retail_track){45000u,49000u,1u,1u,4u|KUI_RETAIL_TRACK_COOKED};
    manifest.slots[1].extent=(struct kui_retail_extent){0u,1u,16000u};
    manifest.storage_transport=KUI_STORAGE_SCIF;manifest.reader=KUI_RETAIL_READER_STANDARD;
    resident_blob=reader_blob;resident_bytes=sizeof(reader_blob);
    resident_limit=KUI_RETAIL_STANDARD_LIMIT;
    memcpy((void *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS,reader_blob,sizeof(reader_blob));
    uint32_t address=FRAME_ADDRESS|(uncached_frame?0x20000000u:0u);
    uint32_t *frame=(uint32_t *)(uintptr_t)address;
    for(unsigned i=0;i<21u;i++) frame[i]=0x6d000000u+i*0x10203u;
    frame[3]=KUI_RETAIL_BOOT_VBR;frame[4]=0x60000001u;frame[5]=0x8c094c56u;
    frame[20]=1u;frame[22]=4096u;
    memcpy(entry_frame,frame,sizeof(entry_frame));
    for(size_t i=0;i<sizeof(video);i++) video[i]=(uint8_t)(i*7u+31u);
    memcpy(saved_video,video,sizeof(video));
    return frame;
}
static void prepare_resident(void) {
    uint32_t begin=KUI_RETAIL_RESIDENT_ADDRESS+(uint32_t)resident_bytes+32u,end=resident_limit-32u;
    memcpy(reader_blob+0x18u,&begin,4u);memcpy(reader_blob+0x1cu,&end,4u);
    memcpy((void *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS,reader_blob,sizeof(reader_blob));
    size_t bytes=manifest.reader==KUI_RETAIL_READER_STANDARD?sizeof(manifest):
        offsetof(struct kui_retail_manifest,slots)+KUI_RETAIL_ASYNC_SLOTS*sizeof(union kui_retail_slot);
    uint32_t address=manifest.reader==KUI_RETAIL_READER_STANDARD?MAP_ADDRESS:ASYNC_MAP_ADDRESS;
    assert(kui_retail_manifest_validate(&manifest)==KUI_GAME_OK);
    memcpy((void *)(uintptr_t)address,&manifest,bytes);
}
static void quiet(void) {
    assert(!calls.restores && !calls.lines && !calls.values);
    assert(!memcmp(video,saved_video,sizeof(video)));
}
static void patches(unsigned restored) {
    const uint8_t *boot=(const uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS;
    const unsigned sorted[4]={2,3,1,0};uint32_t at=0;
    for(unsigned i=0;i<4u;i++) {
        unsigned p=sorted[i];uint32_t offset=sonic_stack_address[p]-KUI_RETAIL_EXEC_ADDRESS;
        assert(!memcmp(boot+at,owner+at,offset-at));
        if(restored&(1u<<p)) assert(!memcmp(boot+offset,owner+offset,12u));
        else {
            const uint16_t stub[4]={0x2f06u,0xd001u,0x402bu,0x0009u};
            uint32_t literal;memcpy(&literal,boot+offset+8u,4u);
            assert(!memcmp(boot+offset,stub,sizeof(stub)));
            assert(literal==((uint32_t)(uintptr_t)sonic_stack_target[p]|0x20000000u));
        }
        at=offset+12u;
    }
    assert(!memcmp(boot+at,owner+at,OWNER_BYTES-at));
}
static int has_line(const char *line) {
    for(unsigned i=0;i<calls.lines;i++) if(!strcmp(calls.line[i],line)) return 1;
    return 0;
}
static void arm(uint32_t *frame) {
    kui_retail_stage_relay(frame,TEST_CCR);
    assert(hooks.publications==4u && calls.pauses==1u);
    for(unsigned i=0;i<4u;i++) {
        assert(hooks.address[i]==sonic_stack_address[i] && hooks.bytes[i]==12u);
    }
    assert(!memcmp(frame,entry_frame,sizeof(entry_frame)));quiet();patches(0);
}
static uint32_t checkpoint(uint32_t *frame,unsigned point) {
    hooks.wait_point=point;hooks.reads=0u;
    return kui_retail_sonic_stack_checkpoint(frame,point,TEST_CCR,&kui_retail_sonic_stack_state);
}
static void wait_ready(uint32_t *frame,unsigned point,unsigned restored) {
    uint32_t raw=(uint32_t)(uintptr_t)frame;
    assert(checkpoint(frame,point)==raw && hooks.reads==3u);
    assert(!memcmp((const void *)(uintptr_t)FRAME_ADDRESS,entry_frame,sizeof(entry_frame)));
    assert(kui_retail_sonic_stack_state.ccr[point]==TEST_CCR);
    assert(kui_retail_sonic_stack_state.resume==sonic_stack_address[point]);
    quiet();patches(restored);
}
static uint32_t *scope_entry(uint32_t *frame) {
    uint32_t prepared=checkpoint(frame,2u);
    assert(prepared==PRIVATE_TOP-84u);
    uint32_t *copy=(uint32_t *)(uintptr_t)prepared;
    for(unsigned i=0;i<21u;i++) {
        assert(copy[i]==(i==5u?((uint32_t)(uintptr_t)kui_retail_sonic_scope_return|0x20000000u):entry_frame[i]));
    }
    assert(!memcmp(frame,entry_frame,sizeof(entry_frame)));
    assert(kui_retail_sonic_stack_state.original_sp==(uint32_t)(uintptr_t)frame+84u);
    assert(kui_retail_sonic_stack_state.active && !kui_retail_sonic_stack_state.completed);
    quiet();patches(kui_retail_sonic_stack_state.restored);
    return copy;
}
static void expect_stop_checkpoint(uint32_t *frame,unsigned point,const char *reason) {
    expect_terminal=1;
    if(!setjmp(terminal)) {checkpoint(frame,point);abort();}
    assert(calls.restores==1u && has_line("SONIC STACK TEST") && has_line(reason));
    assert(has_line("POWER OFF AND ON TO RETURN") && has_line("STORAGE WAS READ ONLY"));
    assert(calls.values==5u && !strcmp(calls.legend[2],"FIFO READS BUSY READS G2 NOW") &&
        !strcmp(calls.legend[3],"READER LOW HIGH BEFORE AFTER RESTORED") &&
        !strcmp(calls.legend[4],"FILE STATUS HANDLE BYTES LOW LIMIT"));
    assert(!memcmp((const void *)(uintptr_t)FRAME_ADDRESS,entry_frame,sizeof(entry_frame)));
}
static void expect_stop_after(uint32_t *frame,const char *reason) {
    expect_terminal=1;
    if(!setjmp(terminal)) {kui_retail_sonic_stack_after(frame,TEST_CCR,&kui_retail_sonic_stack_state);abort();}
    assert(calls.restores==1u && has_line("SONIC STACK TEST") && has_line(reason));
    if(strcmp(reason,"IMMUTABLE READER CHANGED"))
        assert(calls.values==5u && !strcmp(calls.legend[2],"FIFO READS BUSY READS G2 NOW") &&
            !strcmp(calls.legend[3],"READER LOW HIGH BEFORE AFTER RESTORED") &&
            !strcmp(calls.legend[4],"FILE STATUS HANDLE BYTES LOW LIMIT"));
}
enum scenario {SUCCESS_P1,SUCCESS_P2,GATE_SIZE,GATE_IP,GATE_CRC,
    BAD_RELAY_OWNER,BAD_RELAY_READER,BOUNDS_LOW,BOUNDS_HIGH,BOUNDS_ALIGN,BOUNDS_OVERLAP,
    BAD_CODE_ZERO,BAD_CODE_TOO_HIGH,BAD_SNAPSHOT_BOUNDS,FIFO_TIMEOUT,BUSY_TIMEOUT,DUPLICATE,INVALID_POINT,INVALID_FRAME,
    NESTED_SCOPE,BAD_RETURN_SP,BAD_RETURN_CCR,BAD_RETURN_BOUNDS,BAD_RETURN_CODE_END,GUARD_CHANGED,
    SNAPSHOT_CHANGED,SNAPSHOT_MULTI,SNAPSHOT_CODE,SNAPSHOT_IP,SNAPSHOT_P2,SNAPSHOT_ALL,
    SUCCESS_ASYNC,SUCCESS_EAGER,MAP_AT_BEGIN,MAP_AT_END,MAP_MISSING,MAP_DUPLICATE,MAP_MISALIGNED,
    MAP_TRACK_ZERO,MAP_TRACK_HIGH,MAP_EXTENT_ZERO,MAP_ASYNC_COUNT,MAP_ASYNC_TRANSPORT,
    BSS_BELOW_CODE,BSS_HIGH,BSS_ALIGN,BSS_TOO_SHORT,BSS_LIVE_MISMATCH,
    MAP_CHANGED_STANDARD,MAP_CHANGED_ASYNC,CODE_CHANGED_P2,MAP_CHANGED_P2,MAP_WITH_EARLIER_MUTABLE,
    RETURN_MAP_SHIFT,RETURN_MAP_BYTES,RETURN_BSS_BEGIN,RETURN_BSS_END,RETURN_CODE_SHRINK,RETURN_LIMIT,
    SECOND_RETURN,SCOPE_WITHOUT_WAITS,SCOPE_WITHOUT_ASSET,ASSET_SUCCESS,ASSET_ROUNDUP,
    ASSET_MAX,ASSET_ZERO,ASSET_OVERSIZE,ASSET_BOOL_FALSE,ASSET_BAD_BOOL,
    ASSET_HANDLE_ZERO,ASSET_FRAME_HIGH,ASSET_NEGATIVE};
static void run_case(enum scenario which) {
    uint32_t *frame=fixture(which==SUCCESS_P2);
    struct sonic_stack_state *s=&kui_retail_sonic_stack_state;
    if(which==SNAPSHOT_MULTI || which==SNAPSHOT_CODE || which==SNAPSHOT_IP || which==SNAPSHOT_ALL)
        manifest.storage_transport=KUI_STORAGE_SCI;
    if(which==SNAPSHOT_CODE || which==SNAPSHOT_ALL || which==SUCCESS_ASYNC ||
       which==MAP_CHANGED_ASYNC || which==MAP_ASYNC_COUNT || which==MAP_ASYNC_TRANSPORT) {
        manifest.storage_transport=KUI_STORAGE_SCI;
        manifest.reader=KUI_RETAIL_READER_ASYNC;resident_limit=KUI_RETAIL_ASYNC_LIMIT;
    }
    if(which==SNAPSHOT_IP || which==SUCCESS_EAGER) {
        manifest.storage_transport=KUI_STORAGE_SCI;
        manifest.reader=KUI_RETAIL_READER_ASYNC_EAGER;resident_limit=KUI_RETAIL_ASYNC_LIMIT;
    }
    if(which==SNAPSHOT_P2) manifest.storage_transport=KUI_STORAGE_IDE;
    prepare_resident();
    if(which>=BSS_BELOW_CODE && which<=BSS_TOO_SHORT) {
        uint32_t begin=KUI_RETAIL_RESIDENT_ADDRESS+(uint32_t)resident_bytes+32u,end=resident_limit-32u;
        if(which==BSS_BELOW_CODE) begin=KUI_RETAIL_RESIDENT_ADDRESS+(uint32_t)resident_bytes-4u;
        if(which==BSS_HIGH) end=resident_limit+4u;
        if(which==BSS_ALIGN) ++begin;
        if(which==BSS_TOO_SHORT) end=begin+4u;
        memcpy(reader_blob+0x18u,&begin,4u);memcpy(reader_blob+0x1cu,&end,4u);
        memcpy((void *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS,reader_blob,sizeof(reader_blob));
    }
    if(which>=GATE_SIZE && which<=GATE_CRC) {
        if(which==GATE_SIZE) --exec_bytes;
        if(which==GATE_IP) manifest.ip_crc32^=1u;
        if(which==GATE_CRC) boot_crc^=1u;
        /* The verified relay gate is independently covered below; invoke
         * the exact-owner opt-in gate directly for all mismatch choices. */
        sonic_stack_arm();assert(!hooks.publications && !s->armed);quiet();
        return;
    }
    if(which>=BAD_RELAY_OWNER && which<=BAD_SNAPSHOT_BOUNDS) {
        if(which==BAD_RELAY_OWNER) ((uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS)[200]^=1u;
        if(which==BAD_RELAY_READER) ((uint8_t *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS)[200]^=1u;
        if(which==BOUNDS_LOW) hooks.allocation[0]=KUI_RETAIL_STAGE_ADDRESS-32u;
        if(which==BOUNDS_HIGH) hooks.allocation[3]=0x8cf00020u;
        if(which==BOUNDS_ALIGN) ++hooks.allocation[0];
        if(which==BOUNDS_OVERLAP) hooks.allocation[1]=PRIVATE_BOTTOM;
        if(which==BAD_CODE_ZERO) resident_bytes=0u;
        if(which==BAD_CODE_TOO_HIGH) resident_bytes=resident_limit-KUI_RETAIL_RESIDENT_ADDRESS+1u;
        if(which==BAD_SNAPSHOT_BOUNDS) resident_limit=KUI_RETAIL_ASYNC_LIMIT+1u;
        expect_terminal=1;
        if(!setjmp(terminal)) {
            if(which==BAD_CODE_ZERO || which==BAD_CODE_TOO_HIGH) sonic_stack_arm();
            else kui_retail_stage_relay(frame,TEST_CCR);
            abort();
        }
        assert(calls.restores==1u && !hooks.publications && !calls.pauses);
        if(which==BAD_CODE_ZERO || which==BAD_CODE_TOO_HIGH)
            assert(has_line("READER SNAPSHOT BOUNDS INVALID"));
        assert(!memcmp(frame,entry_frame,sizeof(entry_frame)));
        return;
    }
    arm(frame);
    if(which>=ASSET_SUCCESS) {
        if(which==ASSET_ROUNDUP) frame[22]=4097u;
        if(which==ASSET_MAX) frame[22]=0x100000u;
        if(which==ASSET_ZERO) frame[22]=0u;
        if(which==ASSET_OVERSIZE) frame[22]=0x100001u;
        if(which==ASSET_BOOL_FALSE) frame[20]=0u;
        if(which==ASSET_BAD_BOOL) frame[20]=2u;
        if(which==ASSET_HANDLE_ZERO) frame[6]=0u;
        if(which==ASSET_NEGATIVE) frame[22]=0xffffffffu;
        memcpy(entry_frame,frame,sizeof(entry_frame));
        if(which==ASSET_FRAME_HIGH) {
            uint32_t *high=(uint32_t *)(uintptr_t)(KUI_RETAIL_EXEC_ADDRESS-88u);
            memcpy(high,entry_frame,sizeof(entry_frame));
            expect_stop_checkpoint(high,3u,"ASSET SIZE FRAME INVALID");return;
        }
        if(which>=ASSET_ZERO) {
            expect_stop_checkpoint(frame,3u,"FIRST ASSET EXCEEDS TEST RAM BOUNDS");
            assert(hooks.publications==5u && s->restored==8u);patches(8u);return;
        }
        assert(checkpoint(frame,3u)==(uint32_t)(uintptr_t)frame);
        assert(s->asset_status==1u && s->asset_handle==frame[6] && s->asset_bytes==frame[22]);
        assert(!memcmp(frame,entry_frame,sizeof(entry_frame)));
        assert(s->restored==8u && hooks.publications==5u);quiet();patches(8u);return;
    }
    if(which==FIFO_TIMEOUT || which==BUSY_TIMEOUT) {
        unsigned point=which==FIFO_TIMEOUT?0u:1u;hooks.timeout=1;
        expect_stop_checkpoint(frame,point,"LATE AUDIO G2 WAIT TIMED OUT");
        assert(s->reads[point]==SONIC_STACK_WAIT_BUDGET);
        assert(s->last[point]==(point==0u?32u:1u));
        assert(s->ccr[point]==TEST_CCR && !s->active);
        patches(1u<<point);return;
    }
    if(which==INVALID_POINT || which==INVALID_FRAME) {
        uint32_t *entered=which==INVALID_FRAME?
            (uint32_t *)(uintptr_t)((uint32_t)(uintptr_t)frame+1u):frame;
        expect_stop_checkpoint(entered,which==INVALID_POINT?4u:0u,"STACK CHECKPOINT STATE INVALID");
        return;
    }
    if(which==DUPLICATE) {
        wait_ready(frame,0u,1u);
        expect_stop_checkpoint(frame,0u,"STACK CHECKPOINT STATE INVALID");
        assert(hooks.publications==5u);return;
    }
    if(which!=SCOPE_WITHOUT_WAITS) {
        wait_ready(frame,0u,1u);wait_ready(frame,1u,3u);
    }
    if(which==SCOPE_WITHOUT_ASSET) {
        expect_stop_checkpoint(frame,2u,"SCOPED CALL BEFORE ASSET GUARD");return;
    }
    assert(checkpoint(frame,3u)==(uint32_t)(uintptr_t)frame);
    uint32_t map_address=manifest.reader==KUI_RETAIL_READER_STANDARD?MAP_ADDRESS:ASYNC_MAP_ADDRESS;
    size_t map_bytes=manifest.reader==KUI_RETAIL_READER_STANDARD?sizeof(manifest):
        offsetof(struct kui_retail_manifest,slots)+KUI_RETAIL_ASYNC_SLOTS*sizeof(union kui_retail_slot);
    if(which==MAP_AT_BEGIN || which==MAP_AT_END) {
        memset((void *)(uintptr_t)map_address,0x55,map_bytes);
        map_address=which==MAP_AT_BEGIN?KUI_RETAIL_RESIDENT_ADDRESS+(uint32_t)resident_bytes+32u:
            resident_limit-32u-(uint32_t)map_bytes;
        memcpy((void *)(uintptr_t)map_address,&manifest,map_bytes);
    }
    if(which==MAP_MISSING) memset((void *)(uintptr_t)map_address,0x55,map_bytes);
    if(which==MAP_DUPLICATE) memcpy((void *)(uintptr_t)0x8c009000u,&manifest,map_bytes);
    if(which==MAP_MISALIGNED) {
        memmove((void *)(uintptr_t)(map_address+1u),(const void *)(uintptr_t)map_address,map_bytes);
        *(uint8_t *)(uintptr_t)map_address=0x55u;
    }
    if(which==MAP_TRACK_ZERO) manifest.track_count=0u;
    if(which==MAP_TRACK_HIGH) manifest.track_count=KUI_RETAIL_IMAGE_TRACKS+1u;
    if(which==MAP_EXTENT_ZERO) manifest.extent_count=0u;
    if(which==MAP_ASYNC_COUNT) {manifest.track_count=KUI_RETAIL_ASYNC_SLOTS;manifest.extent_count=1u;}
    if(which==MAP_ASYNC_TRANSPORT) manifest.storage_transport=KUI_STORAGE_IDE;
    if(which==BSS_LIVE_MISMATCH) *(uint32_t *)(uintptr_t)(KUI_RETAIL_RESIDENT_ADDRESS+0x18u)+=4u;
    if(which>=MAP_MISSING && which<=BSS_LIVE_MISMATCH) {
        const char *reason=which<=MAP_MISALIGNED?"RESIDENT MAP NOT UNIQUE":
            which<=MAP_ASYNC_TRANSPORT?"RESIDENT MAP SHAPE INVALID":"RESIDENT BSS BOUNDS INVALID";
        expect_stop_checkpoint(frame,2u,reason);
        assert(!s->active && !s->completed && hooks.publications==8u);return;
    }
    uint8_t before_reader[SONIC_STACK_RESIDENT_BYTES];
    size_t reader_bytes=s->resident_end-SONIC_STACK_SNAPSHOT_BEGIN;
    memcpy(before_reader,(const void *)(uintptr_t)SONIC_STACK_SNAPSHOT_BEGIN,reader_bytes);
    uint32_t *returned=scope_entry(frame);
    assert(s->manifest_address==map_address && s->manifest_bytes==map_bytes && s->manifest_matches==1u);
    if(which==NESTED_SCOPE) {
        expect_stop_checkpoint(frame,2u,"STACK CHECKPOINT STATE INVALID");return;
    }
    for(unsigned i=0;i<21u;i++) returned[i]=0x72000000u+i*0x030201u;
    returned[4]=0x60000201u;returned[20]=0xfeedbabeu;
    uint32_t actual[21];memcpy(actual,returned,sizeof(actual));
    /* Simulate the large owner local allocation on the dedicated stack. */
    memset((void *)(uintptr_t)(PRIVATE_TOP-0x4200u),0xa7,0x4004u);
    assert(!memcmp(before_reader,(const void *)(uintptr_t)SONIC_STACK_SNAPSHOT_BEGIN,reader_bytes));
    if(which==BAD_RETURN_SP) {expect_stop_after(returned-1,"SCOPED RETURN FRAME INVALID");return;}
    if(which==BAD_RETURN_CCR) {
        expect_terminal=1;
        if(!setjmp(terminal)) {kui_retail_sonic_stack_after(returned,TEST_CCR^1u,s);abort();}
        assert(has_line("SCOPED RETURN FRAME INVALID"));return;
    }
    if(which==BAD_RETURN_BOUNDS) {s->owner_bottom-=32u;expect_stop_after(returned,"SCOPED RETURN FRAME INVALID");return;}
    if(which==BAD_RETURN_CODE_END) {s->code_end=s->resident_end+1u;expect_stop_after(returned,"SCOPED RETURN FRAME INVALID");return;}
    if(which>=RETURN_MAP_SHIFT && which<=RETURN_LIMIT) {
        if(which==RETURN_MAP_SHIFT) {s->manifest_address+=4u;*(uint8_t *)(uintptr_t)map_address^=1u;}
        if(which==RETURN_MAP_BYTES) s->manifest_bytes-=4u;
        if(which==RETURN_BSS_BEGIN) s->bss_begin+=4u;
        if(which==RETURN_BSS_END) s->bss_end-=4u;
        if(which==RETURN_CODE_SHRINK) s->code_end-=4u;
        if(which==RETURN_LIMIT) s->resident_end-=4u;
        expect_stop_after(returned,"SCOPED RETURN FRAME INVALID");return;
    }
    if(which==GUARD_CHANGED) {
        *(uint32_t *)(uintptr_t)PRIVATE_BOTTOM^=1u;
        expect_stop_after(returned,"PRIVATE STACK GUARD CHANGED");return;
    }
    if(which>=MAP_CHANGED_STANDARD && which<=MAP_WITH_EARLIER_MUTABLE) {
        uint32_t changed=map_address+(uint32_t)map_bytes-1u;
        int p2_only=which==CODE_CHANGED_P2 || which==MAP_CHANGED_P2;
        if(p2_only) {
            uint32_t at=which==CODE_CHANGED_P2?SONIC_STACK_SNAPSHOT_BEGIN+128u:map_address;
            uint32_t old=*(uint32_t *)(uintptr_t)at;
            hooks.p2_override=at|0x20000000u;hooks.p2_value=old^0x00010000u;changed=at+2u;
        } else *(uint8_t *)(uintptr_t)changed^=1u;
        if(which==MAP_WITH_EARLIER_MUTABLE) *(uint8_t *)(uintptr_t)(s->bss_begin+4u)^=1u;
        expect_stop_after(returned,"IMMUTABLE READER CHANGED");
        assert(calls.detail==changed && s->mismatch_address==changed);
        assert(s->code_crc==s->return_code_crc && !memcmp(frame,entry_frame,sizeof(entry_frame)));
        assert(s->changed_bytes==(p2_only?0u:which==MAP_WITH_EARLIER_MUTABLE?2u:1u));
        if(p2_only) assert(s->resident_crc==s->return_crc && s->word_p1!=s->word_p2);
        if(which==MAP_WITH_EARLIER_MUTABLE)
            assert(s->first_changed==s->bss_begin+4u && s->word_address==(changed&~3u));
        return;
    }
    if(which>=SNAPSHOT_CHANGED && which<=SNAPSHOT_ALL) {
        uint8_t *live=(uint8_t *)(uintptr_t)SONIC_STACK_SNAPSHOT_BEGIN;
        size_t first=reader_bytes-1u,last=first;uint32_t count=1u;
        if(which==SNAPSHOT_MULTI) {first=1089u;last=reader_bytes-2u;count=3u;}
        if(which==SNAPSHOT_CODE) first=last=KUI_RETAIL_RESIDENT_ADDRESS-SONIC_STACK_SNAPSHOT_BEGIN+33u;
        if(which==SNAPSHOT_IP) first=last=3u;
        if(which==SNAPSHOT_P2) first=last=1089u;
        if(which==SNAPSHOT_ALL) {first=0u;last=reader_bytes-1u;count=(uint32_t)reader_bytes;}
        if(which==SNAPSHOT_ALL) for(size_t i=0;i<reader_bytes;i++) live[i]^=0xffu;
        else {
            live[first]^=1u;
            if(which==SNAPSHOT_MULTI) {live[first+2u]^=3u;live[last]^=7u;}
        }
        uint32_t word_address=(SONIC_STACK_SNAPSHOT_BEGIN+(uint32_t)first)&~3u;
        uint32_t old_word,new_word;
        memcpy(&old_word,before_reader+(word_address-SONIC_STACK_SNAPSHOT_BEGIN),4u);
        memcpy(&new_word,live+(word_address-SONIC_STACK_SNAPSHOT_BEGIN),4u);
        if(which==SNAPSHOT_P2) {hooks.p2_override=word_address|0x20000000u;hooks.p2_value=0xcafef00du;}
        int fatal=which==SNAPSHOT_CODE || which==SNAPSHOT_IP || which==SNAPSHOT_ALL;
        if(fatal) {
            expect_stop_after(returned,"IMMUTABLE READER CHANGED");
            assert(calls.detail==SONIC_STACK_SNAPSHOT_BEGIN+(uint32_t)first);
            assert(calls.values==5u && !strcmp(calls.legend[0],"POINT FRAME SR PR CCR") &&
                !strcmp(calls.legend[1],"STACK LOW HIGH OLDSP ACTIVE DONE"));
            assert(!strcmp(calls.legend[2],"TYPE READER CHANGES LAST RESTORED") &&
                !strcmp(calls.legend[3],"CODE END CODE OLD CODE NEW FULL OLD FULL NEW") &&
                !strcmp(calls.legend[4],"WORD AT OLD P1 P2 G2 NOW"));
            assert(!memcmp(frame,entry_frame,sizeof(entry_frame)));
        } else {
            uint32_t restored=kui_retail_sonic_stack_after(returned,TEST_CCR,s);
            assert(restored==(uint32_t)(uintptr_t)frame && restored+84u==s->original_sp);
            for(unsigned i=0;i<21u;i++) assert(frame[i]==(i==5u?entry_frame[5]:actual[i]));
            assert(s->completed && !s->active && !s->mismatch_address);quiet();
        }
        assert(s->transport==manifest.storage_transport && s->reader==manifest.reader);
        assert(s->changed_bytes==count && s->last_changed==SONIC_STACK_SNAPSHOT_BEGIN+(uint32_t)last &&
            s->restored==15u);
        size_t code_bytes=KUI_RETAIL_RESIDENT_ADDRESS+resident_bytes-SONIC_STACK_SNAPSHOT_BEGIN;
        uint32_t old_code=independent_crc(0,before_reader,code_bytes),
            new_code=independent_crc(0,live,code_bytes);
        assert(s->code_end==KUI_RETAIL_RESIDENT_ADDRESS+resident_bytes &&
            s->code_crc==old_code && s->return_code_crc==new_code &&
            s->resident_crc==independent_crc(0,before_reader,reader_bytes) &&
            s->return_crc==independent_crc(0,live,reader_bytes));
        if(fatal) assert(old_code!=new_code);else assert(old_code==new_code);
        assert(s->word_address==word_address && s->word_old==old_word && s->word_p1==new_word &&
            s->word_p2==(which==SNAPSHOT_P2?0xcafef00du:new_word));
        assert(hooks.p2_reads>1u && s->resident_crc!=s->return_crc);return;
    }
    uint32_t restored=kui_retail_sonic_stack_after(returned,TEST_CCR,s);
    assert(restored==(uint32_t)(uintptr_t)frame && restored+84u==s->original_sp);
    for(unsigned i=0;i<21u;i++) assert(frame[i]==(i==5u?entry_frame[5]:actual[i]));
    assert(frame[20]==0xfeedbabeu && frame[4]==0x60000201u);
    assert(s->completed && !s->active && s->return_ccr==TEST_CCR);
    assert(!memcmp(returned,actual,sizeof(actual)));quiet();
    assert(!memcmp(before_reader,(const void *)(uintptr_t)SONIC_STACK_SNAPSHOT_BEGIN,reader_bytes));
    patches(15u);
    if(which==SECOND_RETURN) expect_stop_after(returned,"SCOPED RETURN FRAME INVALID");
}
int main(void) {
    FILE *backing=tmpfile();assert(backing && !ftruncate(fileno(backing),RAM_BYTES));
    const uint32_t aliases[2]={0x8c000000u,0xac000000u};
    for(unsigned i=0;i<2u;i++) {
        void *p=mmap((void *)(uintptr_t)aliases[i],RAM_BYTES,PROT_READ|PROT_WRITE,
            MAP_SHARED|MAP_FIXED_NOREPLACE,fileno(backing),0);
        if(p==MAP_FAILED) {perror("map shared Sonic scoped-stack RAM");return 1;}
    }
    assert(!fclose(backing));owner=malloc(OWNER_BYTES);assert(owner);
    for(size_t i=0;i<OWNER_BYTES;i++) owner[i]=(uint8_t)(i*73u+(i>>8)*19u+5u);
    force_crc(owner,OWNER_BYTES,OWNER_CRC);
    for(size_t i=0;i<sizeof(reader_blob);i++) reader_blob[i]=(uint8_t)(i*17u+19u);
    unsigned count=0;
    for(enum scenario which=SUCCESS_P1;which<=ASSET_NEGATIVE;which++) {
        pid_t child=fork();assert(child>=0);
        if(!child) {run_case(which);_Exit(0);}
        int status;assert(waitpid(child,&status,0)==child);
        if(!WIFEXITED(status) || WEXITSTATUS(status)) {
            fprintf(stderr,"Sonic stack scenario %u failed: %d\n",which,status);return 1;
        }
        ++count;
    }
    free(owner);
    for(unsigned i=0;i<2u;i++) assert(!munmap((void *)(uintptr_t)aliases[i],RAM_BYTES));
    printf("PASS Sonic scoped stack: %u synthetic cases, four one-shot RAM entries, bounded waits, actual entry/return frames and resident snapshot\n",count);
    return 0;
}
