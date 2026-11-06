/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_native_vmu_probe.h"
bool kui_smash_native_vmu_probe_tick(struct kui_smash_native_vmu_probe *p,
    kui_smash_native_vmu_probe_read read,void *context) {
    if(!p || !read || p->ready)return false;
    if(p->completions==UINT32_MAX)return false;
    if((++p->completions&63u)!=0u)return false;
    uint32_t bytes,status;
    if(!read(context,KUI_SMASH_SRAM_BYTES,4u,&bytes) || !bytes || bytes>65536u)return false;
    if(!p->registered_at)p->registered_at=p->completions;
    if(read(context,KUI_SMASH_BUP_APP,2u,&status) && status) {p->ready=1u;return false;}
    return p->completions-p->registered_at>=512u;
}
