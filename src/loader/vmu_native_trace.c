/* SPDX-License-Identifier: GPL-3.0-only */
/* Exact-Evolution diagnostics. No SD access, timer programming or cache-mode
 * changes. The live strip is an observation aid, not an arbitrary-PC trace. */
#include "kui/vmu_native_trace.h"
#include "retail_display.h"
#include <string.h>
static const uint8_t evolution_digest[32]={
 0xff,0xb5,0xa8,0xe5,0xff,0x55,0x47,0x02,0x6d,0x93,0x64,0x9e,0x1f,0x90,0x58,0xb7,
 0xee,0x5e,0x29,0xd8,0xd0,0xff,0x8a,0x89,0x33,0x57,0x01,0xf7,0xe9,0x64,0x7f,0x35};
static struct {
 uint32_t enabled,sequence,hook,game,loops,first_tick,last_tick,pr,sr,sp;
 uint32_t submitted,completed,frees,mdstar,mdst,mden,mdtsel,tstr,tcr,tick,latch,elapsed,ccr;
 uint32_t dma_source,dma_target,dma_count,dma_control,scan;
 struct retail_display_state display;
 struct {uint32_t code,pr,sr,sp,mdstar,mdst,tick,latch;} ring[16];
} trace;
bool kui_vmu_native_trace_image(const struct kui_vmu_native_discovery *d) {
 return d && d->image_bytes==2143652u && d->load_address==UINT32_C(0x8c010000) &&
  !memcmp(d->image_sha256,evolution_digest,32u);
}
#ifdef KUI_VMU_NATIVE_TRACE_HOST_TEST
extern uint32_t kui_vmu_trace_read(uint32_t,unsigned);
extern void kui_vmu_trace_pixel(uint32_t,unsigned,uint32_t);
extern void kui_vmu_trace_stop(uint32_t,uint32_t,uint32_t,uint32_t) __attribute__((noreturn));
#define READ(a,w) kui_vmu_trace_read((a),(w))
#define PIXEL(a,w,v) kui_vmu_trace_pixel((a),(w),(v))
#else
static uint32_t trace_read(uint32_t a,unsigned w) {
 return w==1?*(volatile uint8_t *)(uintptr_t)a:w==2?*(volatile uint16_t *)(uintptr_t)a:
  *(volatile uint32_t *)(uintptr_t)a;
}
#define READ(a,w) trace_read((a),(w))
#define PIXEL(a,w,v) do {if((w)==2u) *(volatile uint16_t *)(uintptr_t)(a)=(uint16_t)(v); \
 else *(volatile uint32_t *)(uintptr_t)(a)=(v);} while(0)
#endif
static const uint8_t hex_font[16][7]={
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
 {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
 {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
 {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16}};
static void trace_strip(void) {
 uint32_t ctrl=READ(0xa05f8044u,4),size=READ(0xa05f805cu,4),depth=(ctrl>>2)&3u;
 unsigned bpp=depth==3u?4u:2u;
 uint32_t pitch=((size&1023u)+((size>>20)&1023u))*4u;
 /* Never guess a 24-bit surface, an unrecognised pitch, or a VRAM span. */
 if(!(ctrl&1u) || depth==2u || pitch<256u || pitch>4096u || ((size>>10)&1023u)<15u) return;
 uint32_t starts[2]={READ(0xa05f8050u,4)&0x007ffffcu,READ(0xa05f8054u,4)&0x007ffffcu};
 uint32_t value=(trace.hook&255u)<<24|(trace.game&255u)<<16|(trace.sequence&65535u);
 for(unsigned field=0;field<2u;field++) {
  uint32_t start=starts[field];if(field && start==starts[0]) continue;
  if(start>0x800000u-12u*pitch-52u*bpp) continue;
  for(unsigned y=0;y<8u;y++) for(unsigned x=0;x<48u;x++) {
   unsigned digit=(value>>(28u-4u*(x/6u)))&15u;
   bool ink=y<7u && x%6u<5u && (hex_font[digit][y]&(16u>>(x%6u)));
   PIXEL(0xa5000000u+start+(4u+y)*pitch+(4u+x)*bpp,bpp,ink?UINT32_MAX:0x0864u);
  }
 }
}
void kui_vmu_native_trace_init(const struct kui_vmu_native_passive_config *c) {
 memset(&trace,0,sizeof(trace));
 if(!c || !kui_vmu_native_trace_image(&c->driver)) return;
 memcpy(trace.display.regs,c->display,sizeof(c->display));trace.enabled=1u;
}
int kui_vmu_native_trace_enabled(void) {return trace.enabled!=0u;}
void kui_vmu_native_trace_point(uint32_t code,const struct kui_vmu_native_passive_frame *f,
 uint32_t submitted,uint32_t completed) {
 if(!trace.enabled) return;
 trace.sequence++;if(code<0x40u) trace.hook=code;else trace.game=code;
 if(code==0x30u) ++trace.frees;
 if(f) {trace.pr=f->pr;trace.sr=f->sr;trace.sp=(uint32_t)(uintptr_t)f+
  ((code>=0x40u || (code>=0x20u && code<0x30u))?80u:76u);}
 if(code<0x40u) {trace.submitted=submitted;trace.completed=completed;}
 trace.mdstar=READ(0xa05f6c04u,4);trace.mdst=READ(0xa05f6c18u,4);
 trace.mden=READ(0xa05f6c14u,4);trace.mdtsel=READ(0xa05f6c10u,4);
 trace.tstr=READ(0xffd80004u,1);trace.tcr=READ(0xffd80010u,2);trace.tick=READ(0xffd8000cu,4);
 trace.latch=READ(0x8c3049a8u,4);trace.ccr=READ(0xff00001cu,4);
 trace.dma_source=READ(0xffa00000u,4);trace.dma_target=READ(0xffa00004u,4);
 trace.dma_count=READ(0xffa00008u,4);trace.dma_control=READ(0xffa0000cu,4);
 trace.scan=READ(0xa05f810cu,4);
 unsigned n=(trace.sequence-1u)&15u;
 trace.ring[n].code=code;trace.ring[n].pr=trace.pr;trace.ring[n].sr=trace.sr;trace.ring[n].sp=trace.sp;
 trace.ring[n].mdstar=trace.mdstar;trace.ring[n].mdst=trace.mdst;
 trace.ring[n].tick=trace.tick;trace.ring[n].latch=trace.latch;
 trace_strip();
}
void kui_vmu_native_trace_game(uint32_t code,const struct kui_vmu_native_passive_frame *f) {
 if(!trace.enabled) return;
 if(code==0x41u) {trace.loops=0u;trace.first_tick=READ(0xffd8000cu,4);}
 if(code==0x42u) {
  ++trace.loops;trace.last_tick=READ(0xffd8000cu,4);trace.elapsed=f->r14;
  if(trace.loops>=KUI_VMU_TRACE_WAIT_LIMIT)
   kui_vmu_native_trace_report(0xe002u,trace.loops,trace.first_tick,trace.last_tick);
  if(trace.loops&(trace.loops-1u)) return; /* logarithmic display/ring sampling */
 }
 kui_vmu_native_trace_point(code,f,trace.submitted,trace.completed);
}
#ifndef KUI_VMU_NATIVE_TRACE_HOST_TEST
static void trace_history(unsigned first) {
 retail_display_line("CODE PR MDST TICK WAIT FLAG / OLDEST FIRST");
 unsigned count=trace.sequence<16u?trace.sequence:16u;
 for(unsigned i=first;i<first+8u && i<count;i++) {
  unsigned n=(trace.sequence-count+i)&15u;
  uint32_t values[5]={trace.ring[n].code,trace.ring[n].pr,trace.ring[n].mdst,
   trace.ring[n].tick,trace.ring[n].latch};
  char line[46];
  for(unsigned v=0;v<5u;v++) {
   for(unsigned j=0;j<8u;j++) line[v*9u+j]="0123456789ABCDEF"[(values[v]>>(28u-j*4u))&15u];
   line[v*9u+8u]=' ';
  }
  line[45]=0;retail_display_line(line);
 }
}
#endif
void kui_vmu_native_trace_report(uint32_t reason,uint32_t value,uint32_t a,uint32_t b) {
#ifdef KUI_VMU_NATIVE_TRACE_HOST_TEST
 kui_vmu_trace_stop(reason,value,a,b);
#else
 for(;;) {
 retail_display_restore(&trace.display);
 retail_display_line("EVOLUTION TRACE PAGE 1 OF 3");
 retail_display_values("REASON VALUE CONTEXT1 CONTEXT2",(uint32_t[]){reason,value,a,b},4);
 retail_display_values("HOOK GAME SEQUENCE WAIT LOOPS",(uint32_t[]){trace.hook,trace.game,trace.sequence,trace.loops},4);
 retail_display_values("LAST CHECKPOINT PR SR SP",(uint32_t[]){trace.pr,trace.sr,trace.sp},3);
 retail_display_values("MAPLE SOURCE START ENABLE SELECT",(uint32_t[]){trace.mdstar,trace.mdst,trace.mden,trace.mdtsel},4);
 retail_display_values("TMU0 START CONTROL COUNT WAIT FLAG",(uint32_t[]){trace.tstr,trace.tcr,trace.tick,trace.latch},4);
 retail_display_values("SUBMIT COMPLETE FREE ELAPSED CCR",(uint32_t[]){trace.submitted,trace.completed,trace.frees,trace.elapsed,trace.ccr},5);
 retail_display_line("CHECKPOINT STATE / NOT FAULT PC");
 retail_display_line("PHOTO ALL THREE PAGES THEN POWER OFF");
 retail_display_pause(300u);
 retail_display_restore(&trace.display);
 retail_display_line("EVOLUTION TRACE PAGE 2 OF 3");
 retail_display_values("DMAC0 SOURCE TARGET COUNT CONTROL",(uint32_t[]){trace.dma_source,trace.dma_target,trace.dma_count,trace.dma_control},4);
 retail_display_values("WAIT FIRST TICK LAST TICK PVR SCAN",(uint32_t[]){trace.first_tick,trace.last_tick,trace.scan},3);
 trace_history(0u);
 retail_display_line("PHOTO ALL THREE PAGES THEN POWER OFF");
 retail_display_pause(300u);
 retail_display_restore(&trace.display);
 retail_display_line("EVOLUTION TRACE PAGE 3 OF 3");
 retail_display_values("REASON VALUE SEQUENCE",(uint32_t[]){reason,value,trace.sequence},3);
 trace_history(8u);
 retail_display_line("LAST CHECKPOINT / NOT FAULT PC");
 retail_display_line("PHOTO ALL THREE PAGES THEN POWER OFF");
 retail_display_pause(300u);
 }
#endif
}
