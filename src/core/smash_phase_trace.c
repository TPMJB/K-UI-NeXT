/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_phase_trace.h"
#include <string.h>
static bool zero(const void *p,size_t n) {
 const uint8_t *b=p;uint8_t any=0u;while(n--)any|=*b++;return !any;
}
static bool phase_valid(uint32_t phase) {return (phase>=1u && phase<=5u) || (phase>=10u && phase<=28u);}
void kui_smash_phase_trace_init(struct kui_smash_phase_trace *t) {
 if(!t)return;
 memset(t,0,sizeof(*t));t->magic0=KUI_SMASH_PHASE_TRACE_MAGIC0;
 t->magic1=KUI_SMASH_PHASE_TRACE_MAGIC1;t->version=1u;t->bytes=sizeof(*t);
}
bool kui_smash_phase_trace_valid(const struct kui_smash_phase_trace *t) {
 if(!t || t->magic0!=KUI_SMASH_PHASE_TRACE_MAGIC0 || t->magic1!=KUI_SMASH_PHASE_TRACE_MAGIC1 ||
    t->version!=1u || t->bytes!=sizeof(*t) || t->count>KUI_SMASH_PHASE_TRACE_CAPACITY ||
    t->overflow>1u || t->reserved[0] || t->reserved[1])return false;
 uint32_t last=0u;
 for(uint32_t i=0u;i<t->count;i++) {
  const struct kui_smash_phase_entry *e=&t->entries[i];
  if(!phase_valid(e->phase) || e->phase<=last || e->reserved || e->stamp.reserved)return false;
  last=e->phase;
 }
 return zero(t->entries+t->count,(KUI_SMASH_PHASE_TRACE_CAPACITY-t->count)*sizeof(t->entries[0]));
}
bool kui_smash_phase_trace_blank(const struct kui_smash_phase_trace *t) {
 return kui_smash_phase_trace_valid(t) && !t->count && !t->overflow;
}
bool kui_smash_phase_trace_mark(struct kui_smash_phase_trace *t,uint32_t phase) {
 if(!t)return false;
 if(zero(t,sizeof(*t)))kui_smash_phase_trace_init(t);
 if(!kui_smash_phase_trace_valid(t) || !phase_valid(phase) ||
    (t->count && phase<=t->entries[t->count-1u].phase))return false;
 if(t->count==KUI_SMASH_PHASE_TRACE_CAPACITY) {t->overflow=1u;return false;}
 struct kui_smash_phase_entry *e=&t->entries[t->count];e->phase=phase;
 kui_smash_phase_clock_read(&e->stamp);++t->count;return true;
}
