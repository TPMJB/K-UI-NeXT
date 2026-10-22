/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

/* The diagnostic C and relay run unmodified, using the hardware's shared
 * cached/uncached RAM aliases. Assembly entrypoints are addressed by the RAM
 * patches but never executed on this host. No owner instruction bytes are
 * included: the executable is generated below, with an independent CRC fixup. */
#include "../src/loader/retail_stage.c"

#define OWNER_BYTES 6751168u
#define OWNER_IP_CRC 0x22de24d8u
#define OWNER_EXEC_CRC 0x73f4277bu
#define RAM_BYTES 0x1000000u
#define FRAME_ADDRESS 0x8c00fea0u
#define PROTECTED_STACK 0x8cefe000u
#define TEST_CCR 0x00000909u
#define READ_LIMIT 3000000u
static const uint32_t points[4]={0x8c6082d4u,0x8c6083b0u,0x8c6085bcu,0x8c603d78u};
static const uint32_t registers[3]={0xa05f810cu,0xa05f688cu,0xa05f6900u};
static const uint32_t scan_samples[5]={0x000201ffu,0x100001ffu,0x00000200u,0x12340200u,0x80100101u};
static const uint32_t g2_samples[3]={0x20u,0x1020u,0x100u};
static const uint32_t pvr_samples[3]={0u,0xfffffff7u,0x808u};
static uint8_t *owner;
static uint8_t reader_blob[256];
static uint32_t saved_frame[21];
static jmp_buf terminal;
static bool expect_terminal;
static struct {
    uint32_t regs[14];uint8_t vram[4096];
} video,owner_video;
static struct {
    unsigned reads[3],published;
    uint32_t publish_address[16];size_t publish_bytes[16];
    uint32_t stack;
    int timeout;
} hooks;
static struct {
    unsigned restores,lines,hexes,values,pauses;
    const char *line[16],*hex[8],*legend[8];
    uint32_t hex_value[8],value[8][5];unsigned value_count[8];
} display_calls;

void kui_retail_startup_trace_scan(void) {abort();}
void kui_retail_startup_trace_g2(void) {abort();}
void kui_retail_startup_trace_pvr(void) {abort();}
void kui_retail_startup_trace_gd(void) {abort();}
uint32_t kui_retail_startup_trace_stack_address(void) {return hooks.stack;}
void kui_retail_startup_trace_publish(uint32_t address,size_t bytes) {
    assert(hooks.published<16);
    hooks.publish_address[hooks.published]=address;
    hooks.publish_bytes[hooks.published++]=bytes;
}
uint32_t kui_retail_startup_trace_read(uint32_t address) {
    unsigned index=0;
    while(index<3 && registers[index]!=address) ++index;
    assert(index<3);
    unsigned sample=hooks.reads[index]++;
    if(index==0) {
        if(hooks.timeout==0) return 0x1ffu;
        if(hooks.timeout==1) return 0x200u;
        return scan_samples[sample<5?sample:4];
    }
    if(index==1) {
        if(hooks.timeout==2) return 0x20u;
        return g2_samples[sample<3?sample:2];
    }
    if(hooks.timeout==3) return 0xfffffff7u;
    return pvr_samples[sample<3?sample:2];
}
void retail_display_restore(const struct retail_display_state *state) {
    ++display_calls.restores;
    memcpy(video.regs,state->regs,sizeof(video.regs));memset(video.vram,0,sizeof(video.vram));
}
void retail_display_line(const char *text) {
    assert(display_calls.lines<16);display_calls.line[display_calls.lines++]=text;
    video.vram[display_calls.lines]=(uint8_t)text[0];
    if(!strcmp(text,"STORAGE WAS READ ONLY")) {
        assert(expect_terminal);longjmp(terminal,1);
    }
}
void retail_display_hex(const char *text,uint32_t value) {
    assert(display_calls.hexes<8);
    display_calls.hex[display_calls.hexes]=text;
    display_calls.hex_value[display_calls.hexes++]=value;
    video.vram[100u+display_calls.hexes]=(uint8_t)value;
}
void retail_display_values(const char *legend,const uint32_t *values,unsigned count) {
    assert((count==3 || count==5) && display_calls.values<8);
    display_calls.legend[display_calls.values]=legend;
    display_calls.value_count[display_calls.values]=count;
    memcpy(display_calls.value[display_calls.values++],values,count*sizeof(uint32_t));
    video.vram[200u+display_calls.values]=(uint8_t)values[0];
}
void retail_display_pause(uint32_t frames) {assert(frames==30);++display_calls.pauses;}

static uint32_t independent_crc(uint32_t previous,const uint8_t *bytes,size_t count) {
    uint32_t crc=~previous;
    for(size_t i=0;i<count;i++) {
        crc^=bytes[i];
        for(unsigned bit=0;bit<8;bit++) crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return ~crc;
}
static void force_crc(uint8_t *body,size_t bytes,uint32_t target) {
    assert(bytes>=4);
    uint8_t suffix[4]={0};uint32_t basis[32]={0},combination[32]={0};
    uint32_t prefix=independent_crc(0,body,bytes-4u);
    uint32_t base=independent_crc(prefix,suffix,sizeof(suffix));
    /* Solve the four-byte CRC suffix over GF(2), using independently computed
     * effects. This creates owned synthetic bytes with the exact target CRC;
     * it never substitutes a fake checksum result for the real relay check. */
    for(unsigned i=0;i<32;i++) {
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
    for(unsigned i=0;i<4;i++) body[bytes-4u+i]=(uint8_t)(solution>>(i*8u));
    assert(independent_crc(0,body,bytes)==target);
    assert(kui_retail_crc32(0,body,bytes)==target);
}
static void synthetic_owner(void) {
    owner=malloc(OWNER_BYTES);assert(owner);
    for(size_t i=0;i<OWNER_BYTES;i++) owner[i]=(uint8_t)(i*73u+(i>>8)*19u+5u);
    force_crc(owner,OWNER_BYTES,OWNER_EXEC_CRC);
}
static uint32_t *fixture(void) {
    hooks.stack=PROTECTED_STACK;hooks.timeout=-1;
    uint8_t *boot=(uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS;
    memcpy(boot,owner,OWNER_BYTES);memcpy(original_entry,owner,sizeof(original_entry));
    memset(boot,0xcc,ENTRY_PATCH_BYTES);
    exec_bytes=OWNER_BYTES;boot_crc=OWNER_EXEC_CRC;
    resident_blob=reader_blob;resident_bytes=sizeof(reader_blob);
    memcpy((void *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS,reader_blob,sizeof(reader_blob));
    manifest.ip_crc32=OWNER_IP_CRC;
    uint32_t *frame=(uint32_t *)(uintptr_t)FRAME_ADDRESS;
    for(unsigned i=0;i<21;i++) frame[i]=0x7b000000u+i*0x10101u;
    frame[3]=KUI_RETAIL_BOOT_VBR;frame[4]=0x400000f0u;
    memcpy(saved_frame,frame,sizeof(saved_frame));
    for(unsigned i=0;i<14;i++) {display.regs[i]=0x10000000u+i;video.regs[i]=0x5a000000u+i*11u;}
    for(size_t i=0;i<sizeof(video.vram);i++) video.vram[i]=(uint8_t)(i*37u+23u);
    memcpy(&owner_video,&video,sizeof(video));
    return frame;
}
static void no_video_or_frame_write(const uint32_t *frame) {
    assert(!display_calls.restores && !display_calls.lines && !display_calls.hexes && !display_calls.values);
    assert(!memcmp(&video,&owner_video,sizeof(video)));
    assert(!memcmp(frame,saved_frame,sizeof(saved_frame)));
}
static void published(unsigned event,uint32_t address) {
    assert(event<hooks.published && hooks.publish_address[event]==address && hooks.publish_bytes[event]==12);
}
static void owner_patches(unsigned restored) {
    const uint8_t *boot=(const uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS;
    /* Every byte outside the four twelve-byte patches remains untouched. */
    uint32_t at=0;
    const unsigned sorted[4]={3,0,1,2};
    for(unsigned i=0;i<4;i++) {
        unsigned point=sorted[i];uint32_t offset=points[point]-KUI_RETAIL_EXEC_ADDRESS;
        assert(!memcmp(boot+at,owner+at,offset-at));
        if(restored&(1u<<point)) assert(!memcmp(boot+offset,owner+offset,12));
        else {
            const uint16_t stub[4]={0x2f06u,0xd001u,0x402bu,0x0009u};
            void (*const target[4])(void)={kui_retail_startup_trace_scan,
                kui_retail_startup_trace_g2,kui_retail_startup_trace_pvr,kui_retail_startup_trace_gd};
            uint32_t literal=0;memcpy(&literal,boot+offset+8u,sizeof(literal));
            assert(!memcmp(boot+offset,stub,sizeof(stub)));
            assert(literal==((uint32_t)(uintptr_t)target[point]|0x20000000u));
        }
        at=offset+12;
    }
    assert(!memcmp(boot+at,owner+at,OWNER_BYTES-at));
    assert(!memcmp((const void *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS,reader_blob,sizeof(reader_blob)));
}
static void arm(uint32_t *frame) {
    kui_retail_stage_relay(frame,TEST_CCR);
    assert(hooks.published==4 && display_calls.pauses==1);
    for(unsigned i=0;i<4;i++) published(i,points[i]);
    no_video_or_frame_write(frame);owner_patches(0);
}
static void checkpoint(uint32_t *frame,unsigned point) {
    kui_retail_startup_trace_checkpoint(frame,point,TEST_CCR);
    assert(kui_retail_startup_trace_resume==points[point]);
    published(4u+point,points[point]);
    no_video_or_frame_write(frame);owner_patches((1u<<(point+1u))-1u);
}
static bool has_line(const char *text) {
    for(unsigned i=0;i<display_calls.lines;i++) if(!strcmp(display_calls.line[i],text)) return true;
    return false;
}
static bool has_hex(const char *text,uint32_t value) {
    for(unsigned i=0;i<display_calls.hexes;i++)
        if(!strcmp(display_calls.hex[i],text) && display_calls.hex_value[i]==value) return true;
    return false;
}
static void stopped_at(const uint32_t *frame,const char *message,unsigned point,unsigned passed) {
    assert(display_calls.restores==1 && has_line(message));
    assert(has_line("SONIC STARTUP TRACE - INTENTIONAL STOP"));
    assert(has_line("LAUNCH STOPPED - PHOTOGRAPH THIS SCREEN") && has_line("STORAGE WAS READ ONLY"));
    assert(has_hex("DETAIL",point));
    assert(display_calls.values==5);
    assert(!strcmp(display_calls.legend[0],"POINT PASSED SR VBR CCR") && display_calls.value_count[0]==5);
    assert(display_calls.value[0][0]==point && display_calls.value[0][1]==passed);
    assert(display_calls.value[0][2]==frame[4] && display_calls.value[0][3]==frame[3] && display_calls.value[0][4]==TEST_CCR);
    assert(!memcmp(frame,saved_frame,sizeof(saved_frame)));
    assert(memcmp(&video,&owner_video,sizeof(video)));
}
enum scenario {SUCCESS,GATE_SIZE,GATE_IP,GATE_EXEC,RELAY_BAD_EXEC,RELAY_BAD_RESIDENT,
    STACK_LOW,STACK_HIGH,STACK_ALIGN,SCAN_ZERO_TIMEOUT,SCAN_SET_TIMEOUT,G2_TIMEOUT,
    PVR_TIMEOUT,DUPLICATE,ORDER_CHANGE,GD_ORDER_CHANGE,FOURTH,POINT_INVALID,FRAME_INVALID};
static void terminal_relay(const uint32_t *frame) {
    expect_terminal=true;
    if(!setjmp(terminal)) {kui_retail_stage_relay(frame,TEST_CCR);abort();}
}
static void terminal_checkpoint(const uint32_t *frame,unsigned point) {
    expect_terminal=true;
    if(!setjmp(terminal)) {kui_retail_startup_trace_checkpoint(frame,point,TEST_CCR);abort();}
}
static void run_case(enum scenario scenario) {
    uint32_t *frame=fixture();
    if(scenario>=GATE_SIZE && scenario<=GATE_EXEC) {
        if(scenario==GATE_SIZE) {
            uint8_t *boot=(uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS;
            --exec_bytes;memcpy(boot,original_entry,ENTRY_PATCH_BYTES);
            force_crc(boot,exec_bytes,OWNER_EXEC_CRC);boot_crc=OWNER_EXEC_CRC;
            memset(boot,0xcc,ENTRY_PATCH_BYTES);
        }
        if(scenario==GATE_IP) manifest.ip_crc32^=1u;
        if(scenario==GATE_EXEC) {
            uint8_t *boot=(uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS;
            boot[OWNER_BYTES-1u]^=0x40u;
            memcpy(boot,original_entry,ENTRY_PATCH_BYTES);
            boot_crc=independent_crc(0,boot,exec_bytes);assert(boot_crc!=OWNER_EXEC_CRC);
            memset(boot,0xcc,ENTRY_PATCH_BYTES);
        }
        hooks.stack=0; /* A foreign executable must not touch diagnostic state. */
        kui_retail_stage_relay(frame,TEST_CCR);
        assert(!hooks.published && !hooks.reads[0] && !hooks.reads[1] && !hooks.reads[2]);
        no_video_or_frame_write(frame);
        for(unsigned i=0;i<4;i++) assert(!memcmp((const void *)(uintptr_t)points[i],owner+points[i]-KUI_RETAIL_EXEC_ADDRESS,12));
        return;
    }
    if(scenario>=RELAY_BAD_EXEC && scenario<=STACK_ALIGN) {
        if(scenario==RELAY_BAD_EXEC) ((uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS)[256]^=1u;
        if(scenario==RELAY_BAD_RESIDENT) ((uint8_t *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS)[100]^=1u;
        if(scenario==STACK_LOW) hooks.stack=KUI_RETAIL_STAGE_ADDRESS-32u;
        if(scenario==STACK_HIGH) hooks.stack=0x8ceff020u;
        if(scenario==STACK_ALIGN) hooks.stack=PROTECTED_STACK+1u;
        terminal_relay(frame);
        assert(display_calls.restores==1 && !hooks.published && !display_calls.pauses);
        assert(has_line(scenario==RELAY_BAD_EXEC?"BOOTSTRAP ALTERED EXECUTABLE":
            scenario==RELAY_BAD_RESIDENT?"BOOTSTRAP ALTERED RESIDENT":"STARTUP TRACE STACK INVALID"));
        assert(!memcmp(frame,saved_frame,sizeof(saved_frame)));
        for(unsigned i=0;i<4;i++) assert(!memcmp((const void *)(uintptr_t)points[i],owner+points[i]-KUI_RETAIL_EXEC_ADDRESS,12));
        return;
    }
    arm(frame);
    if(scenario==SUCCESS) {
        checkpoint(frame,0);checkpoint(frame,1);checkpoint(frame,2);
        assert(hooks.published==7 && hooks.reads[0]==5 && hooks.reads[1]==3 && hooks.reads[2]==3);
        return;
    }
    unsigned point=0,passed=0,restored=1;
    const char *message="OWNER HARDWARE WAIT TIMED OUT";
    if(scenario==SCAN_ZERO_TIMEOUT) hooks.timeout=0;
    if(scenario==SCAN_SET_TIMEOUT) hooks.timeout=1;
    if(scenario==G2_TIMEOUT || scenario==PVR_TIMEOUT || scenario==DUPLICATE || scenario==FOURTH) {
        checkpoint(frame,0);passed=1;
    }
    if(scenario==G2_TIMEOUT) {hooks.timeout=2;point=1;restored=3;}
    if(scenario==PVR_TIMEOUT || scenario==FOURTH) {checkpoint(frame,1);passed=3;}
    if(scenario==PVR_TIMEOUT) {hooks.timeout=3;point=2;restored=7;}
    if(scenario==DUPLICATE) {message="STARTUP TRACE ORDER CHANGED";restored=1;}
    if(scenario==ORDER_CHANGE) {point=1;restored=2;message="STARTUP TRACE ORDER CHANGED";}
    if(scenario==GD_ORDER_CHANGE) {point=3;restored=8;message="STARTUP TRACE ORDER CHANGED";}
    if(scenario==FOURTH) {checkpoint(frame,2);point=3;passed=7;restored=15;message="FIRST SDK GD INIT REACHED";}
    const uint32_t *entered_frame=frame;
    if(scenario==POINT_INVALID) {point=4;restored=0;message="STARTUP TRACE STATE INVALID";}
    if(scenario==FRAME_INVALID) {entered_frame=(const uint32_t *)(uintptr_t)(FRAME_ADDRESS+1u);restored=0;message="STARTUP TRACE STATE INVALID";}
    terminal_checkpoint(entered_frame,point);
    if(scenario==POINT_INVALID || scenario==FRAME_INVALID) {
        assert(display_calls.restores==1 && has_line(message) && hooks.published==4);
        assert(display_calls.value[0][0]==point && !memcmp(frame,saved_frame,sizeof(saved_frame)));
    } else stopped_at(frame,message,point,passed);
    owner_patches(restored);
    assert(display_calls.pauses==1);
    if(scenario==SCAN_ZERO_TIMEOUT) assert(hooks.reads[0]==READ_LIMIT+1u && display_calls.value[1][2]==READ_LIMIT);
    if(scenario==SCAN_SET_TIMEOUT) assert(hooks.reads[0]==READ_LIMIT+2u && display_calls.value[1][2]==READ_LIMIT+1u);
    if(scenario==G2_TIMEOUT) assert(hooks.reads[1]==READ_LIMIT+1u && display_calls.value[2][2]==READ_LIMIT);
    if(scenario==PVR_TIMEOUT) assert(hooks.reads[2]==READ_LIMIT+1u && display_calls.value[3][2]==READ_LIMIT);
    if(scenario==DUPLICATE) assert(hooks.reads[0]==6u && display_calls.value[1][2]==5u);
    if(scenario==GD_ORDER_CHANGE) {
        assert(!has_line("FIRST SDK GD INIT REACHED") && hooks.published==5);
        assert(hooks.reads[0]==1 && hooks.reads[1]==1 && hooks.reads[2]==1);
    }
    if(scenario==FOURTH) {
        assert(hooks.published==8 && hooks.reads[0]==6 && hooks.reads[1]==4 && hooks.reads[2]==4);
        assert(display_calls.value[1][2]==5 && display_calls.value[2][2]==3 && display_calls.value[3][2]==3);
        assert(display_calls.value[1][0]==scan_samples[0] && display_calls.value[1][1]==scan_samples[4]);
        assert(display_calls.value[2][0]==g2_samples[0] && display_calls.value[2][1]==g2_samples[2]);
        assert(display_calls.value[3][0]==pvr_samples[0] && display_calls.value[3][1]==pvr_samples[2]);
        assert(kui_retail_startup_trace_resume==points[3]);
    }
}
int main(void) {
    FILE *backing=tmpfile();assert(backing && !ftruncate(fileno(backing),RAM_BYTES));
    const uint32_t aliases[2]={0x8c000000u,0xac000000u};
    for(unsigned i=0;i<2;i++) {
        void *mapped=mmap((void *)(uintptr_t)aliases[i],RAM_BYTES,PROT_READ|PROT_WRITE,
            MAP_SHARED|MAP_FIXED_NOREPLACE,fileno(backing),0);
        if(mapped==MAP_FAILED) {perror("map shared Dreamcast RAM for startup trace");return 1;}
        assert(mapped==(void *)(uintptr_t)aliases[i]);
    }
    assert(!fclose(backing));synthetic_owner();
    for(size_t i=0;i<sizeof(reader_blob);i++) reader_blob[i]=(uint8_t)(i*29u+17u);
    unsigned count=0;
    for(enum scenario scenario=SUCCESS;scenario<=FRAME_INVALID;scenario++) {
        /* A launch has fresh high-stage BSS. Separate children preserve that
         * lifecycle without test-only resets of the implementation's state. */
        pid_t child=fork();assert(child>=0);
        if(!child) {run_case(scenario);_Exit(0);}
        int status=0;assert(waitpid(child,&status,0)==child);
        if(!WIFEXITED(status) || WEXITSTATUS(status)) {
            fprintf(stderr,"startup trace scenario %u failed, status=%d\n",scenario,status);return 1;
        }
        ++count;
    }
    free(owner);
    for(unsigned i=0;i<2;i++) assert(!munmap((void *)(uintptr_t)aliases[i],RAM_BYTES));
    printf("PASS startup trace: %u executed cases; exact owner gate, four RAM-only one-shot patches, bounded probes and terminal evidence\n",count);
    return 0;
}
