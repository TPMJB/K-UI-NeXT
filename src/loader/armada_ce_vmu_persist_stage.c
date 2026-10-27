/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_vmu_persist_stage.h"
#include "kui/armada_ce_vmu_sd_loan.h"
#include "kui/retail_loader_layout.h"
#include <string.h>

_Static_assert(sizeof(struct kui_armada_ce_vmu_persist_stage_workspace)<=98304u,
    "CE persistent high-stage scratch bound");
#if defined(__sh__)
/* This separate freestanding stage needs the scanner's dot-name comparison;
 * readonly images do not link this unit or gain a new minic implementation. */
int strcmp(const char *left,const char *right) {
    const unsigned char *a=(const unsigned char *)left,*b=(const unsigned char *)right;
    while(*a && *a==*b) {++a;++b;}
    return *a<*b?-1:*a>*b?1:0;
}
#endif
static int overlaps(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an && bn && (x<=y?y-x<an:x-y<bn);
}
int kui_armada_ce_vmu_persist_stage_prepare(const struct kui_vmu_persist_profile *p,
    const struct kui_retail_manifest *m,const struct kui_vmu_persist_session_ops *ops,
    struct kui_armada_ce_vmu_persist_stage_workspace *w,struct kui_vmu_durable_handoff *out) {
    if(!out || (w && overlaps(w,sizeof(*w),out,sizeof(*out))) ||
       (p && overlaps(p,sizeof(*p),out,sizeof(*out))) ||
       (m && overlaps(m,sizeof(*m),out,sizeof(*out))) ||
       (ops && overlaps(ops,sizeof(*ops),out,sizeof(*out)))) return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!p || !m || !ops || !w || w->started ||
       overlaps(w,sizeof(*w),p,sizeof(*p)) || overlaps(w,sizeof(*w),m,sizeof(*m)) ||
       overlaps(w,sizeof(*w),ops,sizeof(*ops))) return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    w->started=1u;
    if(kui_retail_manifest_validate(m)!=KUI_GAME_OK ||
       m->storage_transport!=KUI_STORAGE_SCI || m->reader!=KUI_RETAIL_READER_ASYNC ||
       (m->flags&~KUI_RETAIL_IMAGE_BOOT_CRC)) return KUI_VMU_PERSIST_STAGE_MAPPING;
    w->serialized=*m;
    w->serialized.reader=KUI_RETAIL_READER_STANDARD;
    return kui_vmu_persist_stage_prepare(p,&w->serialized,ops,&w->core,out);
}
int kui_armada_ce_vmu_persist_services_prepare(const uint8_t *resident,size_t bytes,
    bool stream,struct kui_armada_ce_vmu_runtime_services *out) {
    if(!out || (resident && overlaps(resident,bytes,out,sizeof(*out)))) return -1;
    memset(out,0,sizeof(*out));
    const size_t offset=52u+(stream?4u:0u);
    if(!resident || bytes> KUI_RETAIL_CE_ASYNC_LIMIT-KUI_RETAIL_RESIDENT_ADDRESS ||
       bytes<offset+KUI_ARMADA_CE_VMU_LOAN_DESCRIPTOR_BYTES) return -1;
    uint32_t descriptor[5];memcpy(descriptor,resident+offset,sizeof(descriptor));
    if(descriptor[0]!=KUI_ARMADA_CE_VMU_LOAN_DESCRIPTOR_MAGIC ||
       descriptor[1]!=KUI_ARMADA_CE_VMU_LOAN_DESCRIPTOR_VERSION ||
       descriptor[2]!=KUI_ARMADA_CE_VMU_LOAN_DESCRIPTOR_BYTES) return -1;
    for(unsigned i=3u;i<5u;i++)
        if(descriptor[i]<KUI_RETAIL_RESIDENT_ADDRESS+offset+sizeof(descriptor) ||
           descriptor[i]-KUI_RETAIL_RESIDENT_ADDRESS>bytes-2u) return -1;
    out->loan_begin=descriptor[3];out->loan_end=descriptor[4];
    if(kui_armada_ce_vmu_runtime_services_seal(out)) {memset(out,0,sizeof(*out));return -1;}
    return 0;
}
