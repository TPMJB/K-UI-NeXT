/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

/* Execute the production relay, including its private saved entry, checksum
 * and resident state. Unused stage loading/storage paths are discarded by the
 * linker, so this test needs no production hooks or proprietary boot bytes. */
#include "../src/loader/retail_stage.c"

#ifdef KUI_RETAIL_CE
const uint8_t __retail_ce_vbr[1]={0};
#endif

#define RAM_ADDRESS 0x8c000000u
#define RAM_BYTES 0x20000u
#define FRAME_ADDRESS 0x8c00fea0u
#define EXEC_BYTES (4096u+73u)
#define RESIDENT_BYTES 256u
#define CCR_VALUE 0x00000909u

static uint8_t executable[EXEC_BYTES],reader_blob[RESIDENT_BYTES];
static uint32_t saved_frame[21];
static jmp_buf stopped_screen;
static bool expect_stop;
static unsigned cases;
static struct {
    uint32_t regs[14];
    uint8_t vram[4096];
} video,before_video;
static struct {
    unsigned restores,lines,hexes,pauses;
    uint32_t pause_frames,detail;
    const char *line[16],*hex[8];
    uint32_t value[8];
} calls;

/* Each display write visibly changes the owner's simulated scanout. Merely
 * returning correct boot bytes therefore cannot hide a success-path redraw. */
void retail_display_restore(const struct retail_display_state *state) {
    ++calls.restores;
    memcpy(video.regs,state->regs,sizeof(video.regs));
    memset(video.vram,0,sizeof(video.vram));
}
void retail_display_line(const char *text) {
    assert(calls.lines<sizeof(calls.line)/sizeof(calls.line[0]));
    calls.line[calls.lines++]=text;
    video.vram[calls.lines]=(uint8_t)text[0];
    if(!strcmp(text,"STORAGE WAS READ ONLY")) {
        assert(expect_stop);
        longjmp(stopped_screen,1);
    }
}
void retail_display_hex(const char *text,uint32_t value) {
    assert(calls.hexes<sizeof(calls.hex)/sizeof(calls.hex[0]));
    calls.hex[calls.hexes]=text;calls.value[calls.hexes++]=value;
    video.vram[100u+calls.hexes]=(uint8_t)value;
    if(!strcmp(text,"DETAIL")) calls.detail=value;
}
void retail_display_pause(uint32_t frames) {
    ++calls.pauses;calls.pause_frames=frames;
}

static uint32_t independent_crc(const uint8_t *bytes,size_t count) {
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<count;i++) {
        crc^=bytes[i];
        for(unsigned bit=0;bit<8;bit++)
            crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return ~crc;
}
static uint32_t *fixture(void) {
    memset((void *)(uintptr_t)RAM_ADDRESS,0xa5,RAM_BYTES);
    memset(&calls,0,sizeof(calls));expect_stop=false;
    for(size_t i=0;i<sizeof(executable);i++) executable[i]=(uint8_t)(i*73u+(i>>8)*19u+5u);
#ifdef KUI_RETAIL_CE
    memcpy(executable+0x40,"ECEC",4);
#endif
    for(size_t i=0;i<sizeof(reader_blob);i++) reader_blob[i]=(uint8_t)(i*29u+17u);
    uint8_t *boot=(uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS;
    memcpy(boot,executable,sizeof(executable));
    memcpy(original_entry,executable,sizeof(original_entry));
    memset(boot,0xcc,ENTRY_PATCH_BYTES); /* Bootstrap 2 reaches this relay. */
    exec_bytes=sizeof(executable);boot_crc=independent_crc(executable,sizeof(executable));
    resident_blob=reader_blob;resident_bytes=sizeof(reader_blob);
    memcpy((void *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS,reader_blob,sizeof(reader_blob));
#ifdef KUI_RETAIL_CE
    for(unsigned i=0;i<KUI_RETAIL_CE_KERNEL_WORDS;i++) ce_kernel[i]=0x8c012000u+i*4u;
    memcpy((void *)(uintptr_t)KUI_RETAIL_CE_KERNEL,ce_kernel,sizeof(ce_kernel));
#endif
    memset(&manifest,0,sizeof(manifest));strcpy(manifest.title,"Generated relay fixture");
    uint32_t *frame=(uint32_t *)(uintptr_t)FRAME_ADDRESS;
    for(unsigned i=0;i<21;i++) frame[i]=0x7b000000u+i*0x10101u;
    frame[3]=KUI_RETAIL_BOOT_VBR;frame[4]=0x400000f0u;
    memcpy(saved_frame,frame,sizeof(saved_frame));
    for(unsigned i=0;i<14;i++) {
        display.regs[i]=0x10000000u+i;
        video.regs[i]=0x5a000000u+i*11u;
    }
    for(size_t i=0;i<sizeof(video.vram);i++) video.vram[i]=(uint8_t)(i*37u+23u);
    memcpy(&before_video,&video,sizeof(video));
    return frame;
}
static bool has_line(const char *text) {
    for(unsigned i=0;i<calls.lines;i++) if(!strcmp(calls.line[i],text)) return true;
    return false;
}
static bool has_hex(const char *text,uint32_t value) {
    for(unsigned i=0;i<calls.hexes;i++)
        if(!strcmp(calls.hex[i],text) && calls.value[i]==value) return true;
    return false;
}
static void success(bool repair_vbr) {
    uint32_t *frame=fixture();
#ifdef KUI_RETAIL_CE
    if(repair_vbr) {
        frame[3]=(uint32_t)(uintptr_t)__retail_ce_vbr;
        frame[4]&=~0x10000000u;
        saved_frame[4]|=0x10000000u;
    }
#else
    assert(!repair_vbr);
#endif
    kui_retail_stage_relay(frame,CCR_VALUE);
    assert(!memcmp(frame,saved_frame,sizeof(saved_frame)));
    assert(!memcmp((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,executable,sizeof(executable)));
    assert(calls.pauses==1 && calls.pause_frames==STEP_PAUSE_FRAMES);
#ifdef KUI_RETAIL_CE
    assert(calls.restores==1 && calls.lines==7 && calls.hexes==4);
    assert(has_line("BOOTSTRAP 2 REACHED GAME ENTRY"));
    assert(has_line("ENTERING WINDOWS CE"));
    assert(has_line("A RESET NOW MEANS WINDOWS CE ITSELF FAILED"));
    assert(has_hex("BOOT STACK",FRAME_ADDRESS+sizeof(saved_frame)));
    assert(has_hex("BOOT SR",frame[4]) && has_hex("BOOT CACHE",CCR_VALUE));
    assert(has_hex("BODY CRC32",boot_crc));
    assert(memcmp(&video,&before_video,sizeof(video)));
    assert(!memcmp((const void *)(uintptr_t)KUI_RETAIL_CE_KERNEL,ce_kernel,sizeof(ce_kernel)));
#else
    assert(!calls.restores && !calls.lines && !calls.hexes);
    assert(!memcmp(&video,&before_video,sizeof(video)));
    assert(!memcmp((const void *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS,reader_blob,sizeof(reader_blob)));
#endif
    ++cases;
}
enum fault {BAD_EXEC,BAD_RESIDENT_FIRST,BAD_RESIDENT_LAST,BAD_STACK_LOW,
    BAD_STACK_HIGH,BAD_STACK_ALIGN,BAD_VBR,BAD_SR,
#ifdef KUI_RETAIL_CE
    BAD_KERNEL,
#endif
};
static void failure(enum fault fault) {
    uint32_t *frame=fixture();
    const uint32_t *entered_frame=frame;
    const char *message=NULL;uint32_t detail=0;
    bool restored_entry=false;
    switch(fault) {
        case BAD_EXEC:
            ((uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS)[ENTRY_PATCH_BYTES+123u]^=0x80u;
            message="BOOTSTRAP ALTERED EXECUTABLE";restored_entry=true;
            /* The relay restores the first entry bytes before checking CRC. */
            memcpy((void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,original_entry,ENTRY_PATCH_BYTES);
            detail=independent_crc((const uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,exec_bytes);
            memset((void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,0xcc,ENTRY_PATCH_BYTES);
            break;
        case BAD_RESIDENT_FIRST:case BAD_RESIDENT_LAST:
            detail=fault==BAD_RESIDENT_FIRST?0u:RESIDENT_BYTES-1u;
            ((uint8_t *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS)[detail]^=0x20u;
            message="BOOTSTRAP ALTERED RESIDENT";restored_entry=true;break;
        case BAD_STACK_LOW:
            entered_frame=(const uint32_t *)(uintptr_t)(KUI_RETAIL_BOOT2_ADDRESS-4u);break;
        case BAD_STACK_HIGH:
            entered_frame=(const uint32_t *)(uintptr_t)(KUI_RETAIL_EXEC_ADDRESS-sizeof(saved_frame)+4u);break;
        case BAD_STACK_ALIGN:
            entered_frame=(const uint32_t *)(uintptr_t)(FRAME_ADDRESS+1u);break;
        case BAD_VBR:
            frame[3]=0x8c00e400u;message="UNSUPPORTED BOOT CPU STATE";detail=frame[3];break;
        case BAD_SR:
            frame[4]&=~0x40000000u;message="UNSUPPORTED BOOT CPU STATE";detail=frame[3];break;
#ifdef KUI_RETAIL_CE
        case BAD_KERNEL:
            detail=KUI_RETAIL_CE_KERNEL-KUI_RETAIL_RESIDENT_ADDRESS;
            ((uint8_t *)(uintptr_t)KUI_RETAIL_CE_KERNEL)[0]^=0x40u;
            message="BOOTSTRAP ALTERED RESIDENT";restored_entry=true;break;
#endif
    }
    if(!message) {message="UNSUPPORTED BOOT STACK";detail=(uint32_t)(uintptr_t)entered_frame;}
    memcpy(saved_frame,frame,sizeof(saved_frame));expect_stop=true;
    if(!setjmp(stopped_screen)) {
        kui_retail_stage_relay(entered_frame,CCR_VALUE);
        assert(!"invalid relay state returned successfully");
    }
    assert(calls.restores==1 && !calls.pauses);
    assert(has_line(message) && has_hex("DETAIL",detail) && calls.detail==detail);
    assert(has_line("LAUNCH STOPPED - PHOTOGRAPH THIS SCREEN"));
    assert(has_line("POWER OFF AND ON TO RETURN") && has_line("STORAGE WAS READ ONLY"));
    assert(memcmp(&video,&before_video,sizeof(video)));
    assert(!memcmp(frame,saved_frame,sizeof(saved_frame)));
    const uint8_t *boot=(const uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS;
    if(restored_entry) assert(!memcmp(boot,original_entry,ENTRY_PATCH_BYTES));
    else for(unsigned i=0;i<ENTRY_PATCH_BYTES;i++) assert(boot[i]==0xcc);
    ++cases;
}
int main(void) {
    /* Dreamcast's P1 RAM lies in AddressSanitizer's own shadow on 64-bit
     * Linux. This fixed-address execution target uses UBSan instead. */
    void *ram=mmap((void *)(uintptr_t)RAM_ADDRESS,RAM_BYTES,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    if(ram==MAP_FAILED) {perror("map Dreamcast RAM for relay test");return 1;}
    assert(ram==(void *)(uintptr_t)RAM_ADDRESS);
    success(false);
#ifdef KUI_RETAIL_CE
    success(true);
#endif
    for(enum fault fault=BAD_EXEC;fault<=BAD_SR;fault++) failure(fault);
#ifdef KUI_RETAIL_CE
    failure(BAD_KERNEL);
    printf("PASS CE relay: %u executed cases; status writes and VBR repair preserved, faults visible\n",cases);
#else
    printf("PASS native relay: %u executed cases; success preserves owner video/frame, faults visible\n",cases);
#endif
    assert(!munmap(ram,RAM_BYTES));
    return 0;
}
