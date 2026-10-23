/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_trace.h"
/* All four windows must match before the first write. Original timestamp
 * helpers are integer leaf code; their exact replay/PR is tested separately. */
static int __attribute__((noipa)) native_evolution_trace_install(const struct kui_vmu_native_passive_exports *e,
 uint32_t dest,uint32_t bytes,uint32_t diagnostics) {
 if(diagnostics!=KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE) return 0;
 if(!kui_vmu_native_trace_image(&native_driver)) return 0;
 uint32_t table=(e->trace_exports&0x1fffffffu)|0xa0000000u;
 if((table&3u) || table<dest+80u || table>dest+bytes-32u) return -1;
 const uint32_t *t=(const uint32_t *)(uintptr_t)table;
 if(t[0]!=0x4b545231u || t[1]!=1u || t[2]!=32u || t[7]) return -1;
 for(unsigned i=3;i<7;i++) if(!passive_entry(t[i],dest,bytes)) return -1;
 static const struct {uint32_t address,bytes;uint16_t ops[8];} w[3]={
  {0x8c1663f8u,12u,{0x2fe6,0x2fd6,0x2fc6,0x2fb6,0xeb00,0x2fa6}},
  {0x8c166422u,16u,{0x4d0b,0x0009,0x6503,0x4a0b,0x64c2,0x490b,0x6403,0x6e03}},
  {0x8c166454u,12u,{0xd43f,0xd23e,0xd03b,0xd33c,0x20b2,0x23b2}}
 };
 struct kui_vmu_native_patch_window ranges[3];
 for(unsigned i=0;i<3;i++) {
  ranges[i]=(struct kui_vmu_native_patch_window){w[i].address,w[i].bytes};
  if(memcmp((void *)(uintptr_t)w[i].address,w[i].ops,w[i].bytes)) return -1;
 }
 if(*(volatile uint32_t *)(uintptr_t)0x8c166544u!=0x8c1cd000u ||
    !kui_vmu_native_patch_no_interiors((void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
      exec_bytes,KUI_RETAIL_EXEC_ADDRESS,ranges,3)) return -1;
 uint16_t patches[3][8];
 for(unsigned i=0;i<3;i++) {
  if(!kui_vmu_native_patch_encode(w[i].address,t[i==2u?6u:i+3u],patches[i])) return -1;
  patches[i][6]=patches[i][7]=0x0009u;
 }
 for(unsigned i=0;i<3;i++) {
  memcpy((void *)(uintptr_t)w[i].address,patches[i],w[i].bytes);
  kui_native_passive_publish(w[i].address,w[i].address+w[i].bytes);
  if(memcmp((void *)(uintptr_t)(w[i].address|0x20000000u),patches[i],w[i].bytes)) return -2;
 }
 *(volatile uint32_t *)(uintptr_t)0x8c166544u=t[5];
 kui_native_passive_publish(0x8c166544u,0x8c166548u);
 return *(volatile uint32_t *)(uintptr_t)0xac166544u==t[5]?1:-2;
}
