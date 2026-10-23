/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#define KUI_VMU_NATIVE_TRACE_HOST_TEST 1
#define KUI_VMU_NATIVE_PERSISTENT_PROBE 1
#define KUI_VMU_NATIVE_READONLY_PROBE 1
#include "../src/loader/vmu_native_trace.c"
static unsigned reads,pixels,irq_reads;static uint32_t control=5,size,sof1,sof2,tick=1234;
static uint32_t clock_start=1u,clock_control=2u,reload=UINT32_MAX;
static uint32_t irq_state[3]={0x8c0f69e4u,0x60000061u,0x8c000000u};
static uint32_t interrupt_event=0x320u,vblank_counter=7u,cached_control=5u,blank_flag=1u;
static jmp_buf terminal;
static uint32_t expected_reason,expected_value,expected_a,expected_b;
uint32_t kui_vmu_trace_read(uint32_t a,unsigned w) {
 ++reads;
 if(a==0xa05f8044u) return control;
 if(a==0xa05f805cu) return size;
 if(a==0xa05f8050u) return sof1;
 if(a==0xa05f8054u) return sof2;
 if(a==0xffd8000cu) return tick;
 if(a==0xffd80008u) return reload;
 if(a==0xff000028u) return interrupt_event;
 if(a==0x8c358d18u) return vblank_counter;
 if(a==0x8c358c70u) return cached_control;
 if(a==0x8c358c50u) return blank_flag;
 if(a==0xffd80004u) {assert(w==1);return clock_start;}
 if(a==0xffd80010u) {assert(w==2);return clock_control;}
 assert(w==4);
 assert(a==0xa05f6c04u || a==0xa05f6c18u || a==0xa05f6c14u || a==0xa05f6c10u ||
  a==0x8c3049a8u || a==0xff00001cu || a==0xffa00000u || a==0xffa00004u ||
  a==0xffa00008u || a==0xffa0000cu || a==0xa05f810cu);
 return a==0x8c3049a8u?1:0;
}
void kui_vmu_native_trace_irq(uint32_t out[3]) {
 ++irq_reads;memcpy(out,irq_state,sizeof(irq_state));
}
void kui_vmu_trace_pixel(uint32_t a,unsigned w,uint32_t v) {
 (void)v;assert(w==2 || w==4);assert(a>=0xa5000000u && a+w<=0xa5800000u);++pixels;
}
void kui_vmu_trace_stop(uint32_t r,uint32_t v,uint32_t a,uint32_t b) {
 assert(r==expected_reason && v==expected_value && a==expected_a && b==expected_b);
 longjmp(terminal,1);
}
static void valid_surface(void) {
 control=5u;size=319u|(479u<<10)|(1u<<20);sof1=0u;sof2=0x100000u;
}
static void fresh(const struct kui_vmu_native_passive_config *c) {
 kui_vmu_native_trace_init(c);valid_surface();
 clock_start=1u;clock_control=2u;reload=UINT32_MAX;tick=1234u;vblank_counter=7u;
}
static void surface_skip(const struct kui_vmu_native_passive_config *c,
 const struct kui_vmu_native_passive_frame *f,unsigned reason) {
 fresh(c);kui_vmu_native_trace_point(0x10u,f,3u,2u);unsigned before=pixels,sequence=trace.sequence;
 if(reason==1u) control=4u;
 else if(reason==2u) control=9u;
 else if(reason==3u) size=479u<<10;
 else if(reason==4u) size=319u|(1u<<20);
 else sof1=sof2=0x7ffffcu;
 kui_vmu_native_trace_point(0x1fu,f,4u,3u);
 assert(pixels==before && trace.surface_reason==reason && trace.surface_seen && trace.draw_off);
 assert(trace.last_video_ctrl==5u && trace.last_video_size==(319u|(479u<<10)|(1u<<20)));
 assert(trace.hook==0x1fu && trace.submitted==4u && trace.completed==3u && trace.sequence==sequence+1u);
 valid_surface();tick-=100u;kui_vmu_native_trace_point(0x20u,f,4u,3u);
 assert(pixels==before && trace.draw_off && trace.sequence==sequence+2u);
 if(reason==1u) assert(!trace.blank_active && trace.blank_ended==1u);
 else assert(!trace.blank_active && !trace.blank_ended);
}
static void blank_capture(const struct kui_vmu_native_passive_config *c,
 const struct kui_vmu_native_passive_frame *f,bool progress,bool wrap) {
 fresh(c);kui_vmu_native_trace_point(0x10u,f,1u,0u);unsigned before=pixels;
 tick=wrap?100u:UINT32_MAX;uint32_t start=tick;control=4u;
 kui_vmu_native_trace_point(0x1fu,f,1u,0u);
 assert(trace.blank_active && trace.blank_clock_valid && trace.blank_first_counter==7u);
 assert(trace.blank_first_spc==irq_state[0] && trace.blank_first_ssr==irq_state[1]);
 assert(trace.blank_first_event==interrupt_event);
 tick=start-KUI_VMU_TRACE_BLANK_TICKS+1u;if(progress) ++vblank_counter;
 clock_control=0x102u; /* UNF is not a prescaler change. */
 kui_vmu_native_trace_point(0x20u,f,1u,0u);
 assert(trace.blank_ticks==KUI_VMU_TRACE_BLANK_TICKS-1u && pixels==before);
 tick=start-KUI_VMU_TRACE_BLANK_TICKS;if(progress) ++vblank_counter;
 kui_vmu_native_trace_point(0x21u,f,1u,0u);
 assert(trace.blank_samples==3u && trace.blank_counter_changes==(progress?2u:0u));
 assert(trace.blank_last_counter==vblank_counter && trace.blank_ticks==KUI_VMU_TRACE_BLANK_TICKS);
 assert(trace.draw_off && pixels==before && trace.exception[0]==irq_state[0] && trace.event==interrupt_event);
 assert(trace.reload==UINT32_MAX && trace.vblank==vblank_counter && trace.cached_ctrl==cached_control && trace.blank_flag==blank_flag);
 /* Ordinary video blanking must not become an artificial two-second stop.
  * Continue well beyond the previous threshold with valid small deltas. */
 for(unsigned i=0u;i<20u;i++) {
  tick-=KUI_VMU_TRACE_BLANK_TICKS/2u;
  if(progress) ++vblank_counter;
  kui_vmu_native_trace_point(0x20u,f,1u,0u);
 }
 assert(trace.blank_ticks==11u*KUI_VMU_TRACE_BLANK_TICKS && pixels==before);
}
static void invalid_blank_clock(const struct kui_vmu_native_passive_config *c,
 const struct kui_vmu_native_passive_frame *f,unsigned invalid) {
 fresh(c);kui_vmu_native_trace_point(0x10u,f,1u,0u);control=4u;
 kui_vmu_native_trace_point(0x1fu,f,1u,0u);assert(trace.blank_clock_valid);
 if(invalid==0u) clock_start=0u;
 else if(invalid==1u) clock_control=3u;
 else if(invalid==2u) reload=123u;
 if(invalid==3u) tick+=100u; /* TCNT was reset upward; unsigned delta is huge. */
 else tick-=10u; /* Prove the bad configuration, independently of jump guard. */
 kui_vmu_native_trace_point(0x20u,f,1u,0u);
 assert(trace.blank_active && !trace.blank_clock_valid);
 clock_start=1u;clock_control=2u;reload=UINT32_MAX;tick-=KUI_VMU_TRACE_BLANK_TICKS;
 kui_vmu_native_trace_point(0x21u,f,1u,0u);assert(!trace.blank_clock_valid);
 control=5u;kui_vmu_native_trace_point(0x22u,f,1u,0u);
 assert(!trace.blank_active && trace.blank_ended==1u && trace.draw_off);
 /* A new window may validate a fresh stable clock; the old window is never
  * retrospectively assigned a fabricated elapsed duration. */
 control=4u;kui_vmu_native_trace_point(0x23u,f,1u,0u);
 assert(trace.blank_active && trace.blank_clock_valid && !trace.blank_ticks);
}
static void invalid_initial_clock(const struct kui_vmu_native_passive_config *c,
 const struct kui_vmu_native_passive_frame *f,unsigned invalid) {
 fresh(c);kui_vmu_native_trace_point(0x10u,f,1u,0u);
 if(invalid==0u) clock_start=0u;
 else if(invalid==1u) clock_control=3u;
 else reload=123u;
 control=4u;--tick;kui_vmu_native_trace_point(0x1fu,f,1u,0u);
 assert(trace.blank_active && !trace.blank_clock_valid);
 clock_start=1u;clock_control=2u;reload=UINT32_MAX;tick-=KUI_VMU_TRACE_BLANK_TICKS;
 kui_vmu_native_trace_point(0x20u,f,1u,0u);assert(!trace.blank_clock_valid);
}
int main(void) {
 struct kui_vmu_native_passive_config c={0};struct kui_vmu_native_passive_frame f={.pr=0x8c0f6c72,.sr=0x60000161,.r14=17};
 kui_vmu_native_trace_init(&c);kui_vmu_native_trace_point(0x10,&f,1,0);assert(!reads && !pixels && !irq_reads);
 c.driver.image_bytes=2143652;c.driver.load_address=0x8c010000;memcpy(c.driver.image_sha256,evolution_digest,32);
 for(unsigned mode=0u;mode<KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE;mode++) {
  c.diagnostics=mode;kui_vmu_native_trace_init(&c);
  kui_vmu_native_trace_point(0x10u,&f,1u,0u);
  kui_vmu_native_trace_game(0x41u,&f);
  assert(!kui_vmu_native_trace_enabled() && !reads && !pixels && !irq_reads && !trace.sequence);
 }
 c.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE;
 for(unsigned bit=0;bit<256;bit++) {c.driver.image_sha256[bit/8]^=1u<<(bit%8);assert(!kui_vmu_native_trace_image(&c.driver));c.driver.image_sha256[bit/8]^=1u<<(bit%8);}
 fresh(&c);assert(kui_vmu_native_trace_enabled());
 control=0;kui_vmu_native_trace_point(0x10,&f,0,0);assert(!trace.surface_seen && !pixels && !trace.blank_active && !trace.draw_off);
 tick-=KUI_VMU_TRACE_BLANK_TICKS+1u;kui_vmu_native_trace_point(0x11u,&f,0u,0u);
 assert(!trace.blank_active && !trace.draw_off);
 control=9;kui_vmu_native_trace_point(0x10,&f,0,0);assert(!trace.surface_seen && !pixels && !trace.draw_off);
 control=5;
 kui_vmu_native_trace_point(0x10,&f,3,2);assert(trace.hook==0x10 && trace.submitted==3 && trace.completed==2 && pixels==768);
 for(unsigned reason=1;reason<=5;reason++) surface_skip(&c,&f,reason);
 blank_capture(&c,&f,false,false);blank_capture(&c,&f,true,false);blank_capture(&c,&f,true,true);
 for(unsigned invalid=0u;invalid<4u;invalid++) invalid_blank_clock(&c,&f,invalid);
 for(unsigned invalid=0u;invalid<3u;invalid++) invalid_initial_clock(&c,&f,invalid);
 fresh(&c);unsigned before=pixels;control=13;sof1=0x7ffffc;sof2=0;
 kui_vmu_native_trace_point(0x30,&f,3,2);assert(pixels==before+384 && trace.frees==1);
 control=13;sof1=sof2=0;kui_vmu_native_trace_point(0x31,&f,3,2);assert(pixels==before+768);
 for(unsigned i=0;i<40;i++) kui_vmu_native_trace_point(0x1f,&f,i,i);
 assert(trace.ring[(trace.sequence-1)&15].pr==f.pr);
 kui_vmu_native_trace_game(0x41,&f);assert(!trace.loops);
 unsigned sequence=trace.sequence;
 for(unsigned i=0;i<31;i++) kui_vmu_native_trace_game(0x42,&f);
 assert(trace.loops==31 && trace.sequence==sequence+5);
 kui_vmu_native_trace_game(0x41,&f);assert(!trace.loops);
 expected_reason=0xe002u;expected_value=KUI_VMU_TRACE_WAIT_LIMIT;expected_a=expected_b=tick;
 if(!setjmp(terminal)) for(unsigned i=0;i<KUI_VMU_TRACE_WAIT_LIMIT;i++) kui_vmu_native_trace_game(0x42,&f);
 assert(trace.loops==KUI_VMU_TRACE_WAIT_LIMIT);
 /* A terminal capture owns its packet snapshot: later caller mutation and
  * oversized counts cannot change or extend the bounded report. */
 struct kui_vmu_volatile_chain_frame packets[KUI_VMU_VOLATILE_CHAIN_FRAMES];
 for(unsigned i=0;i<KUI_VMU_VOLATILE_CHAIN_FRAMES;i++)
  packets[i]=(struct kui_vmu_volatile_chain_frame){i,0x0c300000u+1024u*i,0x201u,i+1u};
 struct kui_vmu_native_trace_failure failure={0x30002u,0x0c248420u,
  (uint32_t)-5,0u,0x0c248420u,48u,0x0cf10000u,512u,UINT32_MAX,0u,packets};
 kui_vmu_native_trace_failure(&failure);
 assert(trace.failure_present && trace.failure_count==KUI_VMU_VOLATILE_CHAIN_FRAMES);
 assert(trace.failure.check==0x30002u && trace.failure.value==(uint32_t)-5);
 assert(trace.failure.expected==0u && !trace.failure.frames);
 packets[31].header=0u;assert(trace.failure_frames[31].header==0x201u);
 failure.frames=NULL;kui_vmu_native_trace_failure(&failure);
 assert(trace.failure_count==0u);
 kui_vmu_native_trace_init(&c);assert(!trace.failure_present && !trace.failure_count);
 c.driver.image_bytes=1u;kui_vmu_native_trace_init(&c);
 kui_vmu_native_trace_failure(&failure);assert(!trace.failure_present);
 puts("Native trace: opt-in only, no default MMIO/VRAM reads or writes, nonterminal long blanking, exact image, exception state, ring, wait watchdog and owned guard snapshot passed");
 return 0;
}
