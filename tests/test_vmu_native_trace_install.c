/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#define KUI_VMU_NATIVE_PERSISTENT_PROBE 1
#define KUI_VMU_NATIVE_READONLY_PROBE 1
#define KUI_VMU_NATIVE_CACHED_CODE 1
#include "kui/vmu_native_passive.h"
#include "kui/vmu_native_patch.h"
#include "kui/vmu_native_trace.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#define KUI_RETAIL_EXEC_ADDRESS 0x8c010000u
static uint32_t exec_bytes=2143652u;
static struct kui_vmu_native_discovery native_driver;
static unsigned publishes;
static bool passive_entry(uint32_t p,uint32_t begin,uint32_t bytes) {
 begin-=0x20000000u;return !(p&1u) && p>=begin+80u && p<begin+bytes;
}
static void kui_native_passive_publish(uint32_t begin,uint32_t end) {
 assert(begin>=0x8c010000u && begin<end && end<=0x8c21b5a4u);
 memcpy((void *)(uintptr_t)(begin|0x20000000u),(void *)(uintptr_t)begin,end-begin);++publishes;
}
#include "../src/loader/vmu_native_evolution_trace_stage.inc"
static unsigned char *original,*live,*mirror;
static void reset(void) {memcpy(live,original,exec_bytes);memcpy(mirror,original,exec_bytes);publishes=0;}
static void map_at(uint32_t at,size_t n) {
 void *p=mmap((void *)(uintptr_t)at,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 assert(p==(void *)(uintptr_t)at);
}
int main(int argc,char **argv) {
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
 original=malloc(exec_bytes);assert(original && fread(original,1,exec_bytes,f)==exec_bytes);fclose(f);
 map_at(0x8c010000u,exec_bytes);map_at(0xac010000u,exec_bytes);map_at(0xac700000u,0x2000);
 live=(void *)(uintptr_t)0x8c010000u;mirror=(void *)(uintptr_t)0xac010000u;
 native_driver.image_bytes=exec_bytes;native_driver.load_address=0x8c010000;
 const unsigned char digest[]={0xff,0xb5,0xa8,0xe5,0xff,0x55,0x47,0x02,0x6d,0x93,0x64,0x9e,0x1f,0x90,0x58,0xb7,0xee,0x5e,0x29,0xd8,0xd0,0xff,0x8a,0x89,0x33,0x57,0x01,0xf7,0xe9,0x64,0x7f,0x35};
 memcpy(native_driver.image_sha256,digest,32);
 uint32_t *t=(void *)(uintptr_t)0xac700080u;
 uint32_t words[]={0x4b545231,2,40,0x8c700100,0x8c700200,0x8c700300,0x8c700400,
  0xac700500,0xac700600,0xac701000};
 memcpy(t,words,sizeof(words));
 struct kui_vmu_native_passive_exports e={.trace_exports=0x8c700080,
  .bss_begin=0xac701000,.bss_end=0xac701100};
 /* Ordinary modes must bypass every trace-table/image access and mutation,
  * even when given an unreadable export pointer. */
 const uint32_t untraced[]={KUI_VMU_NATIVE_DIAGNOSTICS_NORMAL,KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE,
  KUI_VMU_NATIVE_DIAGNOSTICS_HOOKS_ONLY,KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN,5u,UINT32_MAX};
 for(unsigned i=0;i<sizeof(untraced)/sizeof(untraced[0]);i++) {
  reset();assert(native_evolution_trace_install(NULL,0,0,untraced[i])==0 && !publishes);
  assert(!memcmp(live,original,exec_bytes) && !memcmp(mirror,original,exec_bytes));
 }
 reset();assert(native_evolution_trace_install(&e,0xac700000,0x2000,KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE)==1 && publishes==4);
 uint32_t sites[]={0x8c1663f8,0x8c166422,0x8c166454};
 unsigned lengths[]={12,16,12};
 for(unsigned i=0;i<3;i++) {
  uint16_t patch[8]={0};assert(kui_vmu_native_patch_encode(sites[i],words[i==2?6:i+3],patch));
  patch[6]=patch[7]=9;assert(!memcmp((void *)(uintptr_t)sites[i],patch,lengths[i]));
 }
 assert(*(volatile uint32_t *)(uintptr_t)0x8c166544==words[5]);
 unsigned refusals=0;
 for(unsigned i=0;i<3;i++) for(unsigned j=0;j<lengths[i];j++) {
  reset();*(unsigned char *)(uintptr_t)(sites[i]+j)^=1;
  assert(native_evolution_trace_install(&e,0xac700000,0x2000,KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE)<0 && !publishes);
  *(unsigned char *)(uintptr_t)(sites[i]+j)^=1;assert(!memcmp(live,original,exec_bytes));++refusals;
 }
 for(unsigned i=0;i<10;i++) {
  reset();t[i]=words[i]^1u;
  assert(native_evolution_trace_install(&e,0xac700000,0x2000,KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE)<0 && !publishes);
  assert(!memcmp(live,original,exec_bytes));t[i]=words[i];++refusals;
 }
 reset();*(uint16_t *)(uintptr_t)0x8c166000u=0xa210u;
 assert(native_evolution_trace_install(&e,0xac700000,0x2000,KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE)<0 && !publishes);++refusals;
 reset();native_driver.image_sha256[0]^=1;
 assert(native_evolution_trace_install(&e,0xac700000,0x2000,KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE)==0 && !publishes);
 assert(!memcmp(live,original,exec_bytes));
 printf("Exact Evolution diagnostic installer: accepted original; %u refusals before mutation\n",refusals);
 free(original);return 0;
}
