/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_STREAM_INITIAL_WORD_H
#define KUI_ARMADA_CE_STREAM_INITIAL_WORD_H
#include "kui/armada_ce_live_sound.h"

struct kui_armada_ce_stream_initial_owner {
    uint32_t process,slot,handle,index;
};
static inline enum kui_armada_ce_live_status kui_armada_ce_stream_discovery_changed(
    struct kui_armada_ce_live_workspace *w,uint32_t address,uint32_t observed,
    uint32_t expected,uint32_t detail) {
    kui_armada_ce_live_witness(w,KUI_ARMADA_CE_LIVE_FAULT_MAPPING,address,
        observed,expected,detail);return KUI_ARMADA_CE_LIVE_CHANGED;
}
/* Select only a destination for the immutable resident owner callback.
 * This phase reads kernel heap/module/process metadata through canonical P1
 * only. It does not inspect a DLL section, executable guard or object, and
 * never publishes callable methods. The callback must run the complete
 * native sound binder in this destination before using sound/thread APIs.
 * Caller masks IRQs and repeats actual current process, TTB, CCR and SR. */
static inline enum kui_armada_ce_live_status kui_armada_ce_stream_discover_owner(
    uint32_t ttb,uint32_t ccr,kui_armada_ce_ram_read32 read_table,void *context,
    struct kui_armada_ce_live_workspace *w,struct kui_armada_ce_stream_initial_owner *out) {
    uint32_t p1,current,low,high,head,node,value,again;
    uint32_t process_word=0,refs[3]={0};
    struct kui_armada_ce_stream_initial_owner candidate={0};
    enum kui_armada_ce_live_status status=KUI_ARMADA_CE_LIVE_ARGUMENT;
    if(!out) return status;
    memset(out,0,sizeof(*out));
    if(!w || !read_table || !kui_armada_ce_ram_table_p1(ttb,&p1)) return status;
    uintptr_t output_address=(uintptr_t)(void *)out,workspace_address=(uintptr_t)(void *)w;
    if((output_address>=workspace_address && output_address-workspace_address<sizeof(*w)) ||
       (output_address<workspace_address && workspace_address-output_address<sizeof(*out))) return status;
    memset(w,0,sizeof(*w));w->read32=read_table;w->context=context;w->ttb=ttb;w->ccr=ccr;
    w->failure=KUI_ARMADA_CE_LIVE_READ;
    if((ccr&0xa1u)!=1u) return KUI_ARMADA_CE_LIVE_CACHE;
    if(!kui_armada_ce_live_physical(w,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,&current) ||
       !kui_armada_ce_live_physical(w,KUI_ARMADA_CE_LIVE_HEAP_LOW,&low) ||
       !kui_armada_ce_live_physical(w,KUI_ARMADA_CE_LIVE_HEAP_HIGH,&high) ||
       !kui_armada_ce_live_physical(w,KUI_ARMADA_CE_LIVE_MODULE_HEAD,&head)) return w->failure;
    if(!kui_armada_ce_ram_table_p1(low,&p1) || high<=low || high>0x8d000000u)
        return kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_HEAP,head,low,high,0u);
    if(current<KUI_ARMADA_CE_LIVE_PROCESS_BASE ||
       current>=KUI_ARMADA_CE_LIVE_PROCESS_BASE+32u*KUI_ARMADA_CE_LIVE_PROCESS_STRIDE ||
       (current-KUI_ARMADA_CE_LIVE_PROCESS_BASE)%KUI_ARMADA_CE_LIVE_PROCESS_STRIDE)
        return KUI_ARMADA_CE_LIVE_OWNER;
    for(node=head;node;) {
        if(w->links==KUI_ARMADA_CE_LIVE_MODULE_LIMIT) {
            kui_armada_ce_live_witness(w,KUI_ARMADA_CE_LIVE_FAULT_BUDGET,node,w->links,
                KUI_ARMADA_CE_LIVE_MODULE_LIMIT,0u);return KUI_ARMADA_CE_LIVE_BUDGET;
        }
        if(!kui_armada_ce_ram_table_p1(node,&p1) || node<low || node>=high || high-node<180u)
            return kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_NODE,node,low,high,w->links);
        for(unsigned i=0;i<w->links;i++) if(w->link[i].node==node)
            return kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_CYCLE,node,node,0u,w->links);
        struct kui_armada_ce_live_link *link=&w->link[w->links++];link->node=node;
        if(!kui_armada_ce_live_physical(w,node,&value) ||
           !kui_armada_ce_live_physical(w,node+4u,&link->next) ||
           !kui_armada_ce_live_physical(w,node+96u,&link->toc) ||
           !kui_armada_ce_live_physical(w,node+100u,&link->kind)) return w->failure;
        if(value!=node)
            return kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_SELF,node,value,node,w->links);
        unsigned profile=3u;uint32_t kind=link->kind&255u;
        if(kind==3u) {
            if(link->toc==KUI_ARMADA_CE_LIVE_DSOUND_TOC) profile=0u;
            if(link->toc==KUI_ARMADA_CE_LIVE_SNDCORE_TOC) profile=1u;
            if(link->toc==KUI_ARMADA_CE_LIVE_COREDLL_TOC) profile=2u;
        } else if(kind==2u) {
            uint32_t span,count;
            if(!kui_armada_ce_live_physical(w,node+124u,&span) ||
               !kui_armada_ce_live_physical(w,node+112u,&count)) return w->failure;
            /* No virtual pages are sampled in discovery. Reuse each unused
             * page-witness pair for this link's complete span/count words,
             * including external nodes that do not match either profile. */
            _Static_assert(KUI_ARMADA_CE_LIVE_PAGE_LIMIT>=KUI_ARMADA_CE_LIVE_MODULE_LIMIT,
                           "Discovery needs one metadata pair per module link");
            w->page[w->links-1u]=(struct kui_armada_ce_live_page){span,count};
            if((count&0xffffu)==4u && (span==0xb000u || span==0x9000u))
                profile=span==0xb000u?0u:1u;
        }
        if(profile<3u) {
            struct kui_armada_ce_live_module observed;
            status=kui_armada_ce_live_module_fields(w,node,&observed,profile,kind);
            if(status==KUI_ARMADA_CE_LIVE_MODULE && kind==2u) {
                memset(w->fault,0,sizeof(w->fault));node=link->next;continue;
            }
            if(status!=KUI_ARMADA_CE_LIVE_OK) return status;
            static const uint32_t table[3]={0x846cu,0x6198u,0x2e170u};
            if(observed.sections!=observed.base+table[profile]) {
                if(kind==2u) {node=link->next;continue;}
                return kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_TABLE,
                    node,observed.sections,observed.base+table[profile],profile);
            }
            /* Preserve the complete section-count word as a metadata witness;
             * section[] is otherwise unused by this metadata-only phase. */
            if(!kui_armada_ce_live_physical(w,node+112u,&observed.section[0])) return w->failure;
            if((observed.section[0]&0xffffu)!=observed.count)
                return kui_armada_ce_stream_discovery_changed(w,node+112u,
                    observed.section[0]&0xffffu,observed.count,profile);
            struct kui_armada_ce_live_module *m=profile==0u?&w->dsound:
                profile==1u?&w->sndcore:&w->coredll;
            if(m->node) {
                kui_armada_ce_live_witness(w,KUI_ARMADA_CE_LIVE_FAULT_CYCLE,node,m->node,0u,profile);
                return KUI_ARMADA_CE_LIVE_OWNER;
            }
            *m=observed;
        }
        node=link->next;
    }
    if(!w->dsound.node || !w->sndcore.node || !w->coredll.node)
        return kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_MISSING,
            w->dsound.node,w->sndcore.node,w->coredll.node,w->links);
    const struct kui_armada_ce_live_module *mods[3]={&w->dsound,&w->sndcore,&w->coredll};
    uint32_t attached=w->dsound.attach&w->sndcore.attach&w->coredll.attach;
    for(unsigned i=0;i<32u;i++) {
        if(!(attached&(UINT32_C(1)<<i))) continue;
        uint32_t process=KUI_ARMADA_CE_LIVE_PROCESS_BASE+KUI_ARMADA_CE_LIVE_PROCESS_STRIDE*i;
        uint32_t index,slot,handle,observed_refs[3];
        if(!kui_armada_ce_live_physical(w,process,&index) ||
           !kui_armada_ce_live_physical(w,process+12u,&slot) ||
           !kui_armada_ce_live_physical(w,process+8u,&handle)) return w->failure;
        for(unsigned j=0;j<3u;j++)
            if(!kui_armada_ce_live_physical(w,mods[j]->node+16u+4u*(i/2u),&observed_refs[j])) return w->failure;
        if((index&255u)!=i || slot!=((i+1u)<<25) || !handle || handle==UINT32_MAX ||
           !((observed_refs[0]>>(16u*(i&1u)))&0xffffu) ||
           !((observed_refs[1]>>(16u*(i&1u)))&0xffffu) ||
           !((observed_refs[2]>>(16u*(i&1u)))&0xffffu)) {
            kui_armada_ce_live_witness(w,KUI_ARMADA_CE_LIVE_FAULT_ATTACH,
                process,slot,((i+1u)<<25),attached);return KUI_ARMADA_CE_LIVE_OWNER;
        }
        if(candidate.process) {
            kui_armada_ce_live_witness(w,KUI_ARMADA_CE_LIVE_FAULT_ATTACH,
                process,candidate.process,0u,attached);return KUI_ARMADA_CE_LIVE_OWNER;
        }
        candidate=(struct kui_armada_ce_stream_initial_owner){process,slot,handle,i};
        process_word=index;
        for(unsigned j=0;j<3u;j++) refs[j]=observed_refs[j];
    }
    if(!candidate.process) return KUI_ARMADA_CE_LIVE_OWNER;
    /* Repeat every observed list link and all candidate module metadata.
     * No virtual value or section-table word is needed to prove this route. */
    for(unsigned i=0;i<w->links;i++) {
        const struct kui_armada_ce_live_link *link=&w->link[i];
        static const unsigned offset[4]={0u,4u,96u,100u};
        const uint32_t expected[4]={link->node,link->next,link->toc,link->kind};
        for(unsigned j=0;j<4u;j++) {
            if(!kui_armada_ce_live_physical(w,link->node+offset[j],&again)) return w->failure;
            if(again!=expected[j]) return kui_armada_ce_stream_discovery_changed(w,
                link->node+offset[j],again,expected[j],offset[j]);
        }
        if((link->kind&255u)==2u) {
            if(!kui_armada_ce_live_physical(w,link->node+124u,&again)) return w->failure;
            if(again!=w->page[i].va) return kui_armada_ce_stream_discovery_changed(w,
                link->node+124u,again,w->page[i].va,124u);
            if(!kui_armada_ce_live_physical(w,link->node+112u,&again)) return w->failure;
            if(again!=w->page[i].word) return kui_armada_ce_stream_discovery_changed(w,
                link->node+112u,again,w->page[i].word,112u);
        }
    }
    for(unsigned i=0;i<3u;i++) {
        const struct kui_armada_ce_live_module *m=mods[i];
        static const unsigned offset[6]={12u,80u,124u,176u,112u,84u};
        const uint32_t expected[6]={m->attach,m->base,m->span,m->sections,m->section[0],m->flags};
        for(unsigned j=0;j<6u;j++) {
            if(!kui_armada_ce_live_physical(w,m->node+offset[j],&again)) return w->failure;
            if(again!=expected[j]) return kui_armada_ce_stream_discovery_changed(w,
                m->node+offset[j],again,expected[j],offset[j]);
        }
        if(!kui_armada_ce_live_physical(w,m->node+16u+4u*(candidate.index/2u),&again)) return w->failure;
        if(again!=refs[i]) return kui_armada_ce_stream_discovery_changed(w,
            m->node+16u+4u*(candidate.index/2u),again,refs[i],candidate.index);
    }
    static const unsigned process_offset[3]={0u,12u,8u};
    const uint32_t process_expected[3]={process_word,candidate.slot,candidate.handle};
    for(unsigned i=0;i<3u;i++) {
        if(!kui_armada_ce_live_physical(w,candidate.process+process_offset[i],&again)) return w->failure;
        if(again!=process_expected[i]) return kui_armada_ce_stream_discovery_changed(w,
            candidate.process+process_offset[i],again,process_expected[i],process_offset[i]);
    }
    static const uint32_t root_address[4]={KUI_ARMADA_CE_LIVE_MODULE_HEAD,KUI_ARMADA_CE_LIVE_HEAP_LOW,
        KUI_ARMADA_CE_LIVE_HEAP_HIGH,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS};
    const uint32_t root_expected[4]={head,low,high,current};
    for(unsigned i=0;i<4u;i++) {
        if(!kui_armada_ce_live_physical(w,root_address[i],&again)) return w->failure;
        if(again!=root_expected[i]) return kui_armada_ce_stream_discovery_changed(w,
            root_address[i],again,root_expected[i],i);
    }
    memset(w->fault,0,sizeof(w->fault));*out=candidate;
    return KUI_ARMADA_CE_LIVE_OK;
}
/* The restored caller already has its native process/access-key context.
 * Capture only the exact kernel process-table record, with IRQs masked.
 * Eight bounded metadata reads precede installation of any native reader;
 * the caller also repeats actual TTB/CCR and privileged SR around this call. */
static inline bool kui_armada_ce_stream_initial_owner(
    uint32_t ttb,uint32_t ccr,uint32_t sr,kui_armada_ce_ram_read32 read_table,
    void *context,struct kui_armada_ce_stream_initial_owner *out) {
    uint32_t current,index,slot,handle,again,p1;
    if(!out) return false;
    memset(out,0,sizeof(*out));
    if(!read_table || !kui_armada_ce_ram_table_p1(ttb,&p1) ||
       (ccr&0xa1u)!=1u || (sr&0x700000f0u)!=0x400000f0u ||
       !read_table(context,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,&current) ||
       current<KUI_ARMADA_CE_LIVE_PROCESS_BASE ||
       current>=KUI_ARMADA_CE_LIVE_PROCESS_BASE+32u*KUI_ARMADA_CE_LIVE_PROCESS_STRIDE ||
       (current-KUI_ARMADA_CE_LIVE_PROCESS_BASE)%KUI_ARMADA_CE_LIVE_PROCESS_STRIDE)
        return false;
    if(!read_table(context,current,&index) || !read_table(context,current+12u,&slot) ||
       !read_table(context,current+8u,&handle)) return false;
    index&=255u;
    if(index!=(current-KUI_ARMADA_CE_LIVE_PROCESS_BASE)/KUI_ARMADA_CE_LIVE_PROCESS_STRIDE ||
       slot!=((index+1u)<<25) || !handle || handle==UINT32_MAX) return false;
    if(!read_table(context,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,&again) || again!=current ||
       !read_table(context,current,&again) || (again&255u)!=index ||
       !read_table(context,current+12u,&again) || again!=slot ||
       !read_table(context,current+8u,&again) || again!=handle) return false;
    *out=(struct kui_armada_ce_stream_initial_owner){current,slot,handle,index};
    return true;
}
/* A module record reaches this policy only after the full binder admits its
 * heap node, exact profile fields and section-table location. This permits
 * reading the bounded candidate to check its layout/CRC; it grants no API
 * target, COM reference or successful module identity. */
static inline bool kui_armada_ce_stream_initial_module(
    const struct kui_armada_ce_live_module *m,unsigned profile,uint32_t index,uint32_t va) {
    static const uint32_t span[3]={0xb000u,0x9000u,0x34000u};
    static const uint32_t table[3]={0x846cu,0x6198u,0x2e170u};
    if(!m || profile>2u || index>=32u || !m->node || !(m->attach&(UINT32_C(1)<<index)) ||
       (m->kind!=3u && (m->kind!=2u || profile==2u)) ||
       m->count!=(profile==2u || m->kind==2u?4u:3u) || m->span!=span[profile] ||
       (m->base&0xfe00ffffu)!=0x02000000u || m->sections!=m->base+table[profile] ||
       (m->base&0x01ffffffu)>0x02000000u-m->span || va<m->base ||
       va-m->base>=m->span || m->span-(va-m->base)<4u) return false;
    return true;
}
static inline bool kui_armada_ce_stream_initial_address(
    const struct kui_armada_ce_stream_initial_owner *owner,
    const struct kui_armada_ce_live_workspace *w,uint32_t va) {
    if(!owner || !w || (va&3u) || va>=0x80000000u ||
       (va&0x01ffffffu)<0x10000u) return false;
    const uint32_t slot=va&0xfe000000u;
    if(slot==owner->slot) return true;
    if(slot!=0x02000000u) return false;
    if(kui_armada_ce_stream_initial_module(&w->dsound,0u,owner->index,va) ||
       kui_armada_ce_stream_initial_module(&w->sndcore,1u,owner->index,va) ||
       kui_armada_ce_stream_initial_module(&w->coredll,2u,owner->index,va)) return true;
    const struct kui_armada_ce_live_module *candidate=w->native_candidate;
    return candidate && candidate->kind==2u &&
        (kui_armada_ce_stream_initial_module(candidate,0u,owner->index,va) ||
         kui_armada_ce_stream_initial_module(candidate,1u,owner->index,va));
}
#endif
