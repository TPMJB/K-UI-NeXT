/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#define KUI_VMU_NATIVE_TRACE_HOST_TEST 1
#define KUI_VMU_NATIVE_PERSISTENT_PROBE 1
#define KUI_VMU_NATIVE_READONLY_PROBE 1
#include "../src/loader/vmu_native_trace.c"
static unsigned reads,pixels;static uint32_t control=5,size,sof1,sof2,tick=1234;
static jmp_buf terminal;
uint32_t kui_vmu_trace_read(uint32_t a,unsigned w) {
 ++reads;
 if(a==0xa05f8044u) return control;
 if(a==0xa05f805cu) return size;
 if(a==0xa05f8050u) return sof1;
 if(a==0xa05f8054u) return sof2;
 if(a==0xffd8000cu) return tick;
 if(a==0xffd80004u) {assert(w==1);return 1;}
 if(a==0xffd80010u) {assert(w==2);return 0;}
 assert(w==4);
 assert(a==0xa05f6c04u || a==0xa05f6c18u || a==0xa05f6c14u || a==0xa05f6c10u ||
  a==0x8c3049a8u || a==0xff00001cu || a==0xffa00000u || a==0xffa00004u ||
  a==0xffa00008u || a==0xffa0000cu || a==0xa05f810cu);
 return a==0x8c3049a8u?1:0;
}
void kui_vmu_trace_pixel(uint32_t a,unsigned w,uint32_t v) {
 (void)v;assert(w==2 || w==4);assert(a>=0xa5000000u && a+w<=0xa5800000u);++pixels;
}
void kui_vmu_trace_stop(uint32_t r,uint32_t v,uint32_t a,uint32_t b) {
 assert(r==0xe002u && v==KUI_VMU_TRACE_WAIT_LIMIT && a==tick && b==tick);longjmp(terminal,1);
}
int main(void) {
 struct kui_vmu_native_passive_config c={0};struct kui_vmu_native_passive_frame f={.pr=0x8c0f6c72,.sr=0x60000161,.r14=17};
 kui_vmu_native_trace_init(&c);kui_vmu_native_trace_point(0x10,&f,1,0);assert(!reads && !pixels);
 c.driver.image_bytes=2143652;c.driver.load_address=0x8c010000;memcpy(c.driver.image_sha256,evolution_digest,32);
 for(unsigned bit=0;bit<256;bit++) {c.driver.image_sha256[bit/8]^=1u<<(bit%8);assert(!kui_vmu_native_trace_image(&c.driver));c.driver.image_sha256[bit/8]^=1u<<(bit%8);}
 kui_vmu_native_trace_init(&c);assert(kui_vmu_native_trace_enabled());
 size=319u|(479u<<10)|(1u<<20);sof1=0;sof2=0x100000;
 kui_vmu_native_trace_point(0x10,&f,3,2);assert(trace.hook==0x10 && trace.submitted==3 && trace.completed==2 && pixels==768);
 unsigned before=pixels;control=9;kui_vmu_native_trace_point(0x20,&f,3,2);assert(pixels==before); /*24-bit skipped*/
 control=13;sof1=0x7ffffc;sof2=0x7ffffc;kui_vmu_native_trace_point(0x30,&f,3,2);assert(pixels==before && trace.frees==1);
 control=13;sof1=sof2=0;kui_vmu_native_trace_point(0x31,&f,3,2);assert(pixels==before+384);
 for(unsigned i=0;i<40;i++) kui_vmu_native_trace_point(0x1f,&f,i,i);
 assert(trace.ring[(trace.sequence-1)&15].pr==f.pr);
 control=0;kui_vmu_native_trace_game(0x41,&f);assert(!trace.loops);
 unsigned sequence=trace.sequence;
 for(unsigned i=0;i<31;i++) kui_vmu_native_trace_game(0x42,&f);
 assert(trace.loops==31 && trace.sequence==sequence+5);
 kui_vmu_native_trace_game(0x41,&f);assert(!trace.loops);
 if(!setjmp(terminal)) for(unsigned i=0;i<KUI_VMU_TRACE_WAIT_LIMIT;i++) kui_vmu_native_trace_game(0x42,&f);
 assert(trace.loops==KUI_VMU_TRACE_WAIT_LIMIT);
 puts("Native trace: exact image, bounded VRAM, no-card sampling, ring and wait watchdog passed");
 return 0;
}
