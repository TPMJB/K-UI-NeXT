/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual capture observer, exact frame collector and terminal snapshot over
 * the existing guarded worker fixture. No transport operation is mocked. */
#define KUI_NATIVE_E1_WAIT_CAPTURE 1
#define KUI_NATIVE_RA_TICK_OFF_ISOLATION 1
#define KUI_READER_ONLY_TEST_MAIN reader_only_regression_main
#include "test_vmu_native_reader_only.c"

static unsigned observation_reads,rdr_reads;
static uint32_t observation_clock=UINT32_C(0x70000000);
bool kui_native_wait_capture_test_read(uint32_t address,unsigned width,uint32_t *value) {
    ++observation_reads;
    if(address==UINT32_C(0xffe00014)) {++rdr_reads;CHECK(false);}
    if(ram(address,width,width))return kui_vmu_native_passive_test_read(address,width,value);
    switch(address) {
    case 0xa05f810cu:CHECK(width==4u);*value=0x200u;return true;
    case 0xffd8000cu:CHECK(width==4u);*value=--observation_clock;return true;
    case 0xffa0001cu:case 0xffa00040u:case 0xff00001cu:case 0xff000028u:
    case 0xa05f80d8u:case 0xa05f80ccu:case 0xa05f80d0u:
        CHECK(width==4u);*value=address;return true;
    case 0xffe00000u:case 0xffe00004u:case 0xffe00008u:case 0xffe00010u:
        CHECK(width==1u);*value=(address>>2)&255u;return true;
    case 0xffd00008u:CHECK(width==2u);*value=0x5a0fu;return true;
    case 0xa05f6c04u:case 0xa05f6c18u:case 0xa05f6c14u:case 0xa05f6c10u:
        return kui_vmu_native_passive_test_read(address,width,value);
    default:CHECK(false);return false;
    }
}
uint32_t kui_native_wait_capture_test_vbr(void) {return UINT32_C(0x8c00f400);}
static void held(bool pressed) {
    controller_pair.count=1u;controller_pair.frames[0].rx=rx_at(0u);
    controller_pair.frames[0].header=0x01002009u;
    put(rx_at(0u)+8u,4u,pressed?UINT32_C(0xfffffffd):UINT32_MAX);
}
static void frame_words(uint32_t f,bool bad) {
    const uint32_t witnesses[4][2]={{88u,0x8c1b1500u},{120u,0x8c1b09f6u},
        {140u,0x8c1b0ad2u},{148u,0x8c1da1bcu}};
    for(unsigned i=0u;i<4u;i++)put(f+witnesses[i][0],4u,witnesses[i][1]);
    put(f+388u,4u,0x8c1f362cu);put(f+392u,4u,0x8c1f362cu);put(f+396u,4u,0x60000000u);
    put(f+308u,4u,0x600000f0u);put(f+312u,4u,0x3c020u);
    put(f+328u,4u,bad?0xffe00014u:0x10cu);
    put(f+360u,4u,bad?UINT32_MAX:0x8c1f1f36u);
    put(f+364u,4u,bad?UINT32_MAX:0x3ffu);put(f+368u,4u,0x10cu);
    put(f+380u,4u,0x8c00f400u);put(0x8c358d18u,4u,123u);
}
static void observe(uint32_t f) {
    native_wait_capture_observe((const struct kui_vmu_native_passive_frame *)(uintptr_t)f);
}
int main(void) {
    map_exact(UINT32_C(0x8c720000),4096u);map_exact(UINT32_C(0xac721000),4096u);
    reader_only_setup(KUI_VMU_NATIVE_DIAGNOSTICS_NORMAL);
    const uint32_t f=UINT32_C(0x8c2ff800);
    held(false);for(unsigned i=0u;i<10u;i++)observe(f);
    CHECK(!diagnostic.active && !diagnostic.gesture_count && !observation_reads);
    held(true);for(unsigned i=0u;i<29u;i++)observe(f);
    CHECK(!diagnostic.active && diagnostic.gesture_count==29u && !observation_reads);
    held(false);observe(f);CHECK(!diagnostic.gesture_count && !observation_reads);
    held(true);for(unsigned i=0u;i<30u;i++)observe(f);
    CHECK(diagnostic.active && !diagnostic_phase.wait.attempts && !observation_reads);
    held(false);
    for(unsigned i=0u;i<7u;i++) {
        frame_words(f,i==2u);observe(f);
        CHECK(diagnostic_phase.wait.valid==i+1u && !diagnostic_phase.wait.ready);
    }
    frame_words(f,false);
    if(!setjmp(terminal)) {observe(f);CHECK(false);}
    const struct native_wait_capture *c=&diagnostic_phase.wait;
    CHECK(terminal_phase==18u && terminal_error==1u && c->valid==8u && c->attempts==8u);
    CHECK(c->first_bad==2u && c->first_wait==0u && c->operand_bad==1u && c->operand_match==7u);
    CHECK(c->record[2].r4==0xffe00014u && c->record[2].r12==UINT32_MAX);
    CHECK(!rdr_reads && observation_reads && !clock_reads && !diagnostic.clock_samples);
    CHECK(c->snapshot[10]==0xffa0001cu && c->snapshot[11]==0xffa00040u);
    CHECK(c->snapshot[13]==0x8c00f400u && c->snapshot[14]==0x5a0fu);
    CHECK(c->snapshot[15]==0xff00001cu && c->snapshot[16]==0xff000028u);
    no_virtual_or_storage();
    reader_only_setup(KUI_VMU_NATIVE_DIAGNOSTICS_NORMAL);
    held(true);for(unsigned i=0u;i<30u;i++)observe(f);held(false);
    for(unsigned i=0u;i<119u;i++)observe(f);
    CHECK(!diagnostic_phase.wait.valid && diagnostic_phase.wait.attempts==119u &&
        !diagnostic_phase.wait.ready);
    if(!setjmp(terminal)) {observe(f);CHECK(false);}
    CHECK(terminal_phase==18u && terminal_error==2u && diagnostic_phase.wait.ready==2u);
    CHECK(diagnostic_phase.wait.attempts==120u && diagnostic_phase.wait.unwind_fail==120u &&
        !diagnostic_phase.wait.valid && !rdr_reads);
    no_virtual_or_storage();
    /* The final variant rejects profiling and foreign identity at arm. */
    const struct kui_vmu_native_passive_config good=input;
    for(unsigned changed=0u;changed<4u;changed++) {
        input=good;state.armed=0u;
        if(!changed)input.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE;
        else if(changed==1u)input.driver.image_sha256[31]^=1u;
        else if(changed==2u)++input.driver.image_bytes;
        else input.driver.load_address+=4u;
        CHECK(kui_vmu_native_passive_arm(&input,NULL)<0 && !state.armed);
    }
    CHECK(sizeof(diagnostic_phase)==908u);
    printf("Wait observer: %u checks; gesture-only beforearm, next8 checked frames, fixed snapshot/no RDR, no PROFILE or VMU/storage IO, exact identity/mode refusal\n",checks);
    return 0;
}
