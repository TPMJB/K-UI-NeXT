/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_LIVE_SOUND_H
#define KUI_ARMADA_CE_LIVE_SOUND_H
#include "kui/armada_ce_ram_bind.h"
#include <stddef.h>
#include <string.h>

/* Exact private Armada US CE profile. The caller verifies the kernel identity
 * and keeps its captured TTB/context unchanged throughout this read-only call.
 * No name pointer, CE API, port, firmware queue or sound RAM is dereferenced. */
#define KUI_ARMADA_CE_LIVE_READ_LIMIT 4096u
#define KUI_ARMADA_CE_LIVE_MODULE_LIMIT 64u
#define KUI_ARMADA_CE_LIVE_PAGE_LIMIT 64u
#if defined(KUI_WINCE_CDDA_WORMS) && KUI_WINCE_CDDA_WORMS == 1
/* Exact supplied Worms Armageddon US CE2 kernel, game and sound DLLs. */
#define KUI_ARMADA_CE_LIVE_MODULE_HEAD UINT32_C(0x8c121b24)
#define KUI_ARMADA_CE_LIVE_PROCESS_BASE UINT32_C(0x8c1276c0)
#define KUI_ARMADA_CE_LIVE_CURRENT_PROCESS UINT32_C(0x8c121890)
#define KUI_ARMADA_CE_LIVE_HEAP_LOW UINT32_C(0x8c126410)
#define KUI_ARMADA_CE_LIVE_HEAP_HIGH UINT32_C(0x8c126414)
#define KUI_ARMADA_CE_LIVE_APISETS UINT32_C(0x8c128a60)
#define KUI_ARMADA_CE_LIVE_DSOUND_TOC UINT32_C(0x8c11e104)
#define KUI_ARMADA_CE_LIVE_SNDCORE_TOC UINT32_C(0x8c11e0e4)
#define KUI_ARMADA_CE_LIVE_COREDLL_TOC UINT32_C(0x8c11dee4)
#define KUI_ARMADA_CE_LIVE_DEVICE_VTABLE_RVA 0x1154u
#define KUI_ARMADA_CE_LIVE_DSOUND_TEXT_BYTES 0x73a6u
/* This global holds the game sound object; its first word holds DSOUND. */
#define KUI_ARMADA_CE_LIVE_GAME_DEVICE 0x127ba8u
#define KUI_ARMADA_CE_LIVE_GAME_IAT 0xd215cu
#define KUI_ARMADA_CE_LIVE_BUFFER_PLAY_RVA 0x2bacu
#define KUI_ARMADA_CE_LIVE_BUFFER_POSITION_RVA 0x2f00u
#elif defined(KUI_WINCE_CDDA_BAM4) && KUI_WINCE_CDDA_BAM4 == 1
/* Exact supplied Bust-A-Move4 US kernel/game/DSOUND profile. SNDCORE and
 * the admitted COREDLL wrapper prefixes match the Armada contracts. */
#define KUI_ARMADA_CE_LIVE_MODULE_HEAD UINT32_C(0x8c1d1b24)
#define KUI_ARMADA_CE_LIVE_PROCESS_BASE UINT32_C(0x8c1da6e0)
#define KUI_ARMADA_CE_LIVE_CURRENT_PROCESS UINT32_C(0x8c1d1890)
#define KUI_ARMADA_CE_LIVE_HEAP_LOW UINT32_C(0x8c1d9430)
#define KUI_ARMADA_CE_LIVE_HEAP_HIGH UINT32_C(0x8c1d9434)
#define KUI_ARMADA_CE_LIVE_APISETS UINT32_C(0x8c1dba80)
#define KUI_ARMADA_CE_LIVE_DSOUND_TOC UINT32_C(0x8c1ce430)
#define KUI_ARMADA_CE_LIVE_SNDCORE_TOC UINT32_C(0x8c1ce410)
#define KUI_ARMADA_CE_LIVE_COREDLL_TOC UINT32_C(0x8c1ce210)
#define KUI_ARMADA_CE_LIVE_DEVICE_VTABLE_RVA 0x111cu
#define KUI_ARMADA_CE_LIVE_DSOUND_TEXT_BYTES 0x61b6u
#define KUI_ARMADA_CE_LIVE_GAME_DEVICE 0x13f14cu
#define KUI_ARMADA_CE_LIVE_GAME_IAT 0x5f0c4u
#define KUI_ARMADA_CE_LIVE_BUFFER_PLAY_RVA 0x2e5cu
#define KUI_ARMADA_CE_LIVE_BUFFER_POSITION_RVA 0x315cu
#else
#define KUI_ARMADA_CE_LIVE_MODULE_HEAD UINT32_C(0x8c145b24)
#define KUI_ARMADA_CE_LIVE_PROCESS_BASE UINT32_C(0x8c14e6e0)
#define KUI_ARMADA_CE_LIVE_CURRENT_PROCESS UINT32_C(0x8c145890)
#define KUI_ARMADA_CE_LIVE_HEAP_LOW UINT32_C(0x8c14d430)
#define KUI_ARMADA_CE_LIVE_HEAP_HIGH UINT32_C(0x8c14d434)
#define KUI_ARMADA_CE_LIVE_APISETS UINT32_C(0x8c14fa80)
#define KUI_ARMADA_CE_LIVE_DSOUND_TOC UINT32_C(0x8c1416b4)
#define KUI_ARMADA_CE_LIVE_SNDCORE_TOC UINT32_C(0x8c141694)
#define KUI_ARMADA_CE_LIVE_COREDLL_TOC UINT32_C(0x8c141494)
#define KUI_ARMADA_CE_LIVE_DEVICE_VTABLE_RVA 0x116cu
#define KUI_ARMADA_CE_LIVE_DSOUND_TEXT_BYTES 0x61a6u
#define KUI_ARMADA_CE_LIVE_GAME_DEVICE 0xf7180u
#define KUI_ARMADA_CE_LIVE_GAME_IAT 0xf21a8u
#define KUI_ARMADA_CE_LIVE_BUFFER_PLAY_RVA 0x2e54u
#define KUI_ARMADA_CE_LIVE_BUFFER_POSITION_RVA 0x3154u
#endif
#define KUI_ARMADA_CE_LIVE_PROCESS_STRIDE UINT32_C(0x9c)
#if defined(KUI_WINCE_CDDA_WORMS) && KUI_WINCE_CDDA_WORMS == 1
#define KUI_ARMADA_CE_LIVE_WIN32_RECORD UINT32_C(0x8c011b7c)
#define KUI_ARMADA_CE_LIVE_WIN32_METHODS UINT32_C(0x8c01193c)
#define KUI_ARMADA_CE_LIVE_CALLBACK_SENTINEL UINT32_C(0x8c011b00)
#define KUI_ARMADA_CE_LIVE_DSOUND_SPAN 0xc000u
#define KUI_ARMADA_CE_LIVE_COREDLL_SPAN 0x21000u
#define KUI_ARMADA_CE_LIVE_TABLE_RVAS {0x940cu,0x6190u,0x1b0f8u}
#define KUI_ARMADA_CE_LIVE_SINGLETON_RVA 0x90a8u
#define KUI_ARMADA_CE_LIVE_CREATE_RVA 0x1598u
#define KUI_ARMADA_CE_LIVE_ADDREF_RVA 0x18dcu
#define KUI_ARMADA_CE_LIVE_RELEASE_RVA 0x18e4u
#define KUI_ARMADA_CE_LIVE_CREATE_BUFFER_RVA 0x1924u
#define KUI_ARMADA_CE_LIVE_COOPERATIVE_RVA 0x1b08u
#define KUI_ARMADA_CE_LIVE_BUFFER_VTABLE_RVA 0x1268u
#define KUI_ARMADA_CE_LIVE_ENTER_IAT_RVA 0x9008u
#define KUI_ARMADA_CE_LIVE_LEAVE_IAT_RVA 0x900cu
#define KUI_ARMADA_CE_LIVE_ENTER_RVA 0x8a78u
#define KUI_ARMADA_CE_LIVE_LEAVE_RVA 0x8adcu
#define KUI_ARMADA_CE_LIVE_ENTER_CRC 0x12d156c9u
#define KUI_ARMADA_CE_LIVE_LEAVE_CRC 0x661d8023u
#define KUI_ARMADA_CE_LIVE_POSITION_CRC 0x3f18ba2du
#else
#define KUI_ARMADA_CE_LIVE_WIN32_RECORD UINT32_C(0x8c011b20)
#define KUI_ARMADA_CE_LIVE_WIN32_METHODS UINT32_C(0x8c0118e0)
#define KUI_ARMADA_CE_LIVE_CALLBACK_SENTINEL UINT32_C(0x8c011aa4)
#define KUI_ARMADA_CE_LIVE_DSOUND_SPAN 0xb000u
#define KUI_ARMADA_CE_LIVE_COREDLL_SPAN 0x34000u
#define KUI_ARMADA_CE_LIVE_TABLE_RVAS {0x846cu,0x6198u,0x2e170u}
#define KUI_ARMADA_CE_LIVE_SINGLETON_RVA 0x8110u
#define KUI_ARMADA_CE_LIVE_CREATE_RVA 0x15ecu
#define KUI_ARMADA_CE_LIVE_ADDREF_RVA 0x1994u
#define KUI_ARMADA_CE_LIVE_RELEASE_RVA 0x199cu
#define KUI_ARMADA_CE_LIVE_CREATE_BUFFER_RVA 0x19dcu
#define KUI_ARMADA_CE_LIVE_COOPERATIVE_RVA 0x1bccu
#define KUI_ARMADA_CE_LIVE_BUFFER_VTABLE_RVA 0x11ccu
#define KUI_ARMADA_CE_LIVE_ENTER_IAT_RVA 0x801cu
#define KUI_ARMADA_CE_LIVE_LEAVE_IAT_RVA 0x8020u
#define KUI_ARMADA_CE_LIVE_ENTER_RVA 0x8ea0u
#define KUI_ARMADA_CE_LIVE_LEAVE_RVA 0x8efcu
#define KUI_ARMADA_CE_LIVE_ENTER_CRC 0xeea467ebu
#define KUI_ARMADA_CE_LIVE_LEAVE_CRC 0x01916477u
#define KUI_ARMADA_CE_LIVE_POSITION_CRC 0x0d46861au
#endif

enum kui_armada_ce_live_status {
    KUI_ARMADA_CE_LIVE_OK, KUI_ARMADA_CE_LIVE_ARGUMENT,
    KUI_ARMADA_CE_LIVE_READ, KUI_ARMADA_CE_LIVE_BUDGET,
    KUI_ARMADA_CE_LIVE_MODULE, KUI_ARMADA_CE_LIVE_OWNER,
    KUI_ARMADA_CE_LIVE_IDENTITY, KUI_ARMADA_CE_LIVE_OBJECT,
    KUI_ARMADA_CE_LIVE_CHANGED, KUI_ARMADA_CE_LIVE_CACHE,
    KUI_ARMADA_CE_LIVE_MAPPING
};
/* Failure-only metadata, captured from words already read. No extra guest
 * dereference is made to explain a refusal, and no callable lease is implied. */
enum kui_armada_ce_live_module_fault {
    KUI_ARMADA_CE_LIVE_FAULT_NONE,
    KUI_ARMADA_CE_LIVE_FAULT_HEAP, KUI_ARMADA_CE_LIVE_FAULT_NODE,
    KUI_ARMADA_CE_LIVE_FAULT_CYCLE, KUI_ARMADA_CE_LIVE_FAULT_SELF,
    KUI_ARMADA_CE_LIVE_FAULT_COUNT, KUI_ARMADA_CE_LIVE_FAULT_BASE,
    KUI_ARMADA_CE_LIVE_FAULT_SPAN, KUI_ARMADA_CE_LIVE_FAULT_ATTACH,
    KUI_ARMADA_CE_LIVE_FAULT_TABLE, KUI_ARMADA_CE_LIVE_FAULT_SECTION_SIZE,
    KUI_ARMADA_CE_LIVE_FAULT_SECTION_RVA, KUI_ARMADA_CE_LIVE_FAULT_SECTION_FLAGS,
    KUI_ARMADA_CE_LIVE_FAULT_SECTION_ADDRESS, KUI_ARMADA_CE_LIVE_FAULT_MISSING,
    KUI_ARMADA_CE_LIVE_FAULT_READ, KUI_ARMADA_CE_LIVE_FAULT_MAPPING,
    KUI_ARMADA_CE_LIVE_FAULT_BUDGET
};
struct kui_armada_ce_live_sound {
    uint32_t current_process,owner_process,owner_index,owner_slot;
    uint32_t dsound_module,sndcore_module,coredll_module,dsound_base,sndcore_base,coredll_base;
    uint32_t device,vtable,direct_sound_create,create_sound_buffer,add_ref,release,set_cooperative;
    uint32_t enter_cs,leave_cs,buffer_vtable,buffer_release,buffer_get_caps,buffer_lock,buffer_unlock;
    uint32_t buffer_play,buffer_stop,buffer_get_status,buffer_get_position;
    uint32_t refcount,reads;
};
/* Separate streaming admission; the established19-word excerpt handoff is
 * unchanged. This is still a read-only snapshot, not a thread/COM lease. */
struct kui_armada_ce_live_stream_api {
    uint32_t owner_process,owner_slot,owner_handle,coredll_module;
    uint32_t create_thread,resume_thread,sleep,set_thread_priority;
    uint32_t wait_single,close_handle,virtual_alloc,virtual_free;
    uint32_t set_kmode,flush_instruction_cache,get_tick_count,buffer_set_position;
    uint32_t enter_cs,leave_cs;
};
_Static_assert(sizeof(struct kui_armada_ce_live_stream_api)==18u*4u,"stream API ABI");
struct kui_armada_ce_live_module {
    uint32_t node,attach,base,span,sections,count,flags,kind;
    uint32_t section[16];
};
struct kui_armada_ce_live_link {uint32_t node,next,toc,kind;};
/* The immutable full page word already contains the physical frame number.
 * Keep it once: the RAM binder derives the physical page from this word. */
struct kui_armada_ce_live_page {uint32_t va,word;};
/* Allocate this workspace statically in the controller, not on the 2048-byte
 * GD hook stack. It retains only metadata, never a COM reference or lease. */
struct kui_armada_ce_live_workspace {
    kui_armada_ce_ram_read32 read32;
    kui_armada_ce_ram_read_mapped mapped32;
    void *context;
    uint32_t ttb,ccr,reads; /* Budgeted RAM/page-table reads. Each legacy mapped
                           * callback has at most ten array/P2 reads and one
                           * CCR sample. Native rebind callbacks have bounded
                           *18 metadata reads and one native VA sample. The
                           * mandatory budgeted walk bounds callback count. */
    enum kui_armada_ce_live_status failure;
    uint32_t fault[5]; /* Reason, node, observed, expected, profile/section. */
    unsigned links,pages;
    struct kui_armada_ce_live_link link[KUI_ARMADA_CE_LIVE_MODULE_LIMIT];
    struct kui_armada_ce_live_page page[KUI_ARMADA_CE_LIVE_PAGE_LIMIT];
    struct kui_armada_ce_live_module dsound,sndcore,coredll;
    /* Set only by a verified restored caller or native worker binder. */
    bool native_values;
    /* Scoped to a field-admitted external candidate's section/CRC reads.
     * Never retained after those checks or published as a module identity. */
    const struct kui_armada_ce_live_module *native_candidate;
};
static inline void kui_armada_ce_live_witness(
    struct kui_armada_ce_live_workspace *w,uint32_t reason,uint32_t node,
    uint32_t observed,uint32_t expected,uint32_t detail) {
    w->fault[0]=reason;w->fault[1]=node;w->fault[2]=observed;
    w->fault[3]=expected;w->fault[4]=detail;
}
static inline enum kui_armada_ce_live_status kui_armada_ce_live_module_fail(
    struct kui_armada_ce_live_workspace *w,uint32_t reason,uint32_t node,
    uint32_t observed,uint32_t expected,uint32_t detail) {
    kui_armada_ce_live_witness(w,reason,node,observed,expected,detail);
    return KUI_ARMADA_CE_LIVE_MODULE;
}
static inline bool kui_armada_ce_live_physical(void *context,uint32_t address,uint32_t *value) {
    struct kui_armada_ce_live_workspace *w=context;
    uint32_t p1;
    if(!kui_armada_ce_ram_table_p1(address,&p1)) {
        w->failure=KUI_ARMADA_CE_LIVE_MAPPING;
        kui_armada_ce_live_witness(w,KUI_ARMADA_CE_LIVE_FAULT_MAPPING,address,0,0x8c010000u,0);return false;
    }
    if(w->reads>=KUI_ARMADA_CE_LIVE_READ_LIMIT) {
        w->failure=KUI_ARMADA_CE_LIVE_BUDGET;
        kui_armada_ce_live_witness(w,KUI_ARMADA_CE_LIVE_FAULT_BUDGET,address,w->reads,KUI_ARMADA_CE_LIVE_READ_LIMIT,0);return false;
    }
    ++w->reads;
    if(!w->read32(w->context,p1,value)) {
        w->failure=KUI_ARMADA_CE_LIVE_READ;
        kui_armada_ce_live_witness(w,KUI_ARMADA_CE_LIVE_FAULT_READ,p1,0,1u,w->reads);return false;
    }
    return true;
}
static inline enum kui_armada_ce_ram_bind_status kui_armada_ce_live_mapped(
    void *context,uint32_t va,uint32_t p1,uint32_t page,uint32_t ccr,uint32_t *out) {
    struct kui_armada_ce_live_workspace *w=context;
    enum kui_armada_ce_ram_bind_status status=w->mapped32(w->context,va,p1,page,ccr,out);
    if(status==KUI_ARMADA_CE_RAM_BIND_READ) w->failure=KUI_ARMADA_CE_LIVE_READ;
    return status;
}
static inline enum kui_armada_ce_live_status kui_armada_ce_live_word(
    struct kui_armada_ce_live_workspace *w,uint32_t address,uint32_t *value) {
    if(address&3u) return KUI_ARMADA_CE_LIVE_MAPPING;
    if(address>=UINT32_C(0x80000000)) {
        return kui_armada_ce_live_physical(w,address,value)?KUI_ARMADA_CE_LIVE_OK:w->failure;
    }
    if(address<UINT32_C(0x02000000)) return KUI_ARMADA_CE_LIVE_MAPPING;
    struct kui_armada_ce_ram_binding b;
    enum kui_armada_ce_ram_bind_status result;
    if(w->native_values) {
        /* CCR0 requests mapping only even for matching VA/P1 colors. The
         * worker callback owns every mapped value, with its own masked
         * owner/TTB checks and repeated mapping witnesses. */
        result=kui_armada_ce_ram_bind(w->ttb,address,0u,
            kui_armada_ce_live_physical,w,&b);
        if(result==KUI_ARMADA_CE_RAM_BIND_CACHE)
            result=kui_armada_ce_live_mapped(w,address,b.physical_address,
                b.page_word,w->ccr,&b.value);
    } else result=kui_armada_ce_ram_bind_with_reader(
        w->ttb,address,w->ccr,kui_armada_ce_live_physical,
        w->mapped32?kui_armada_ce_live_mapped:NULL,w,&b);
    if(result!=KUI_ARMADA_CE_RAM_BIND_OK) {
        if(result==KUI_ARMADA_CE_RAM_BIND_READ) return w->failure;
        kui_armada_ce_live_witness(w,KUI_ARMADA_CE_LIVE_FAULT_MAPPING,address,(uint32_t)result,
                                   KUI_ARMADA_CE_RAM_BIND_OK,w->ccr);
        if(result==KUI_ARMADA_CE_RAM_BIND_CACHE) return KUI_ARMADA_CE_LIVE_CACHE;
        if(result==KUI_ARMADA_CE_RAM_BIND_CHANGED) return KUI_ARMADA_CE_LIVE_CHANGED;
        return KUI_ARMADA_CE_LIVE_MAPPING;
    }
    uint32_t va=address&UINT32_C(0xfffff000);
    unsigned i=0;
    while(i<w->pages && w->page[i].va!=va) ++i;
    if(i<w->pages) {
        if(w->page[i].word!=b.page_word)
            return KUI_ARMADA_CE_LIVE_CHANGED;
    } else {
        if(w->pages==KUI_ARMADA_CE_LIVE_PAGE_LIMIT) return KUI_ARMADA_CE_LIVE_BUDGET;
        w->page[w->pages++]=(struct kui_armada_ce_live_page){va,b.page_word};
    }
    *value=b.value;return KUI_ARMADA_CE_LIVE_OK;
}
/* MapPtrToProcess's reviewed low-pointer rule. Explicit pointers retain their
 * address domain; no P2, MMIO, unaligned or reserved address is admitted here. */
static inline bool kui_armada_ce_live_owner_pointer(uint32_t pointer,uint32_t slot,uint32_t *out) {
    uint32_t p1;
    if(!out || slot<0x02000000u || slot>0x40000000u || (slot&0x01ffffffu)) return false;
    if(pointer<UINT32_C(0x10000) || (pointer&3u)) return false;
    if(pointer<UINT32_C(0x02000000)) pointer|=slot;
    if(pointer>=UINT32_C(0x80000000) && !kui_armada_ce_ram_table_p1(pointer,&p1)) return false;
    *out=pointer;return true;
}
static inline uint32_t kui_armada_ce_live_crc_word(uint32_t crc,uint32_t value) {
    for(unsigned byte=0;byte<4u;byte++) {
        crc^=(value>>(byte*8u))&255u;
        for(unsigned bit=0;bit<8u;bit++) crc=(crc>>1)^((0u-(crc&1u))&UINT32_C(0xedb88320));
    }
    return crc;
}
static inline enum kui_armada_ce_live_status kui_armada_ce_live_crc_range(
    struct kui_armada_ce_live_workspace *w,uint32_t address,uint32_t bytes,uint32_t expected) {
    uint32_t crc=UINT32_MAX;
    for(unsigned at=0;at<bytes;at+=4u) {
        uint32_t value;
        enum kui_armada_ce_live_status status=kui_armada_ce_live_word(w,address+at,&value);
        if(status!=KUI_ARMADA_CE_LIVE_OK) return status;
        crc=kui_armada_ce_live_crc_word(crc,value);
    }
    return (crc^UINT32_MAX)==expected?KUI_ARMADA_CE_LIVE_OK:KUI_ARMADA_CE_LIVE_IDENTITY;
}
static inline enum kui_armada_ce_live_status kui_armada_ce_live_game_guard(
    struct kui_armada_ce_live_workspace *w,uint32_t slot) {
#if defined(KUI_WINCE_CDDA_WORMS) && KUI_WINCE_CDDA_WORMS == 1
    static const uint32_t offset[3]={0xa4c90u,0xad240u,0xa4d78u};
    static const uint32_t expected[3]={0x5dd60ad4u,0x7d5b8b13u,0x8ba77820u};
    static const uint32_t bytes[3]={32u,12u,12u};
#elif defined(KUI_WINCE_CDDA_BAM4) && KUI_WINCE_CDDA_BAM4 == 1
    static const uint32_t offset[3]={0x1b9b0u,0x5bf98u,0x1bb00u};
    static const uint32_t expected[3]={0x1d70fb98u,0x413d9757u,0x8a2de20eu};
    static const uint32_t bytes[3]={32u,12u,40u};
#else
    static const uint32_t offset[3]={0x995d4u,0xce900u,0x996a0u};
    static const uint32_t expected[3]={0x9baf8d11u,0x46696861u,0xb374b05au};
    static const uint32_t bytes[3]={32u,12u,28u};
#endif
    for(unsigned range=0;range<3u;range++) {
        enum kui_armada_ce_live_status status=kui_armada_ce_live_crc_range(w,slot|offset[range],bytes[range],expected[range]);
        if(status!=KUI_ARMADA_CE_LIVE_OK) return status;
    }
    return KUI_ARMADA_CE_LIVE_OK;
}
static inline enum kui_armada_ce_live_status kui_armada_ce_live_module_fields(
    struct kui_armada_ce_live_workspace *w,uint32_t node,struct kui_armada_ce_live_module *m,unsigned profile,uint32_t kind) {
    static const unsigned offset[6]={12u,80u,124u,176u,112u,84u};
    uint32_t field[6];
    for(unsigned i=0;i<6u;i++) if(!kui_armada_ce_live_physical(w,node+offset[i],&field[i])) return w->failure;
    m->node=node;m->attach=field[0];m->base=field[1];m->span=field[2];
    m->sections=field[3];m->count=field[4]&0xffffu;m->flags=field[5];m->kind=kind;
    static const uint32_t span[3]={KUI_ARMADA_CE_LIVE_DSOUND_SPAN,0x9000u,KUI_ARMADA_CE_LIVE_COREDLL_SPAN};
    uint32_t expected_count=profile==2u || kind==2u?4u:3u;
    if(profile>2u || (kind!=2u && kind!=3u) || m->count!=expected_count)
        return kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_COUNT,node,m->count,expected_count,profile);
    if(m->base<UINT32_C(0x02000000) || m->base>=UINT32_C(0x80000000) || (m->base&0xffffu))
        return kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_BASE,node,m->base,0x02000000u,profile);
    if(m->span!=span[profile] || (m->base&0x01ffffffu)>0x02000000u-m->span)
        return kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_SPAN,node,m->span,span[profile],profile);
    if(!m->attach)
        return kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_ATTACH,node,m->attach,1u,profile);
    return KUI_ARMADA_CE_LIVE_OK;
}
static inline enum kui_armada_ce_live_status kui_armada_ce_live_sections(
    struct kui_armada_ce_live_workspace *w,struct kui_armada_ce_live_module *m,unsigned profile,bool verify) {
#if defined(KUI_WINCE_CDDA_WORMS) && KUI_WINCE_CDDA_WORMS == 1
    static const uint32_t ds_rva[3]={0x1000u,0x9000u,0xa000u},sn_rva[3]={0x1000u,0x5000u,0x7000u};
    static const uint32_t ds_size[3]={0x73a6u,0x460u,0x610u},sn_size[3]={0x3885u,0x11e4u,0x378u};
    static const uint32_t flags[3]={0x60000020u,0xc0002040u,0x40002040u};
    static const uint32_t co_rva[4]={0x1000u,0x1b000u,0x1c000u,0x1e000u};
    static const uint32_t co_size[4]={0x1a0efu,0x168u,0xfe0u,0x218u};
    static const uint32_t co_flags[4]={0x60000020u,0xc0300040u,0x40002040u,0x40000040u};
    static const uint32_t disk_ds_rva[4]={0x1000u,0x9000u,0xa000u,0xb000u};
    static const uint32_t disk_sn_rva[4]={0x1000u,0x5000u,0x7000u,0x8000u};
    static const uint32_t disk_ds_size[4]={0x73a6u,0x47cu,0x610u,0x770u};
    static const uint32_t disk_sn_size[4]={0x3885u,0x1200u,0x378u,0x494u};
    static const uint32_t disk_flags[4]={0x60000020u,0xc0000040u,0x40000040u,0x42000040u};
#else
    static const uint32_t ds_rva[3]={0x1000u,0x8000u,0x9000u},sn_rva[3]={0x1000u,0x5000u,0x7000u};
    /* After normal load NK embeds the ASCII basename and the 28-byte
     * section records in writable .data page slack. The stable live size
     * includes those records; raw PE/ROM sizes describe an earlier state.
     * Name-only and raw layouts are not callable-module admission. */
    static const uint32_t ds_size[3]={KUI_ARMADA_CE_LIVE_DSOUND_TEXT_BYTES,0x4c0u,0x550u},sn_size[3]={0x3c25u,0x11ecu,0x3d0u};
    static const uint32_t flags[3]={0x60000020u,0xc0002040u,0x40002040u};
    static const uint32_t co_rva[4]={0x1000u,0x2e000u,0x2f000u,0x31000u};
    static const uint32_t co_size[4]={0x2ccc4u,0x1e0u,0x1848u,0x218u};
    static const uint32_t co_flags[4]={0x60000020u,0xc0300040u,0x40002040u,0x40000040u};
    /* The exact external copies retain a fourth relocation section. Their
     * embedded section table is 112 bytes instead of the ROM sound DLLs'
     * 84 bytes. Original flags are retained; NK may subsequently free only
     * the discardable relocation section, clearing its adjusted address. */
    static const uint32_t disk_ds_rva[4]={0x1000u,0x8000u,0x9000u,0xa000u};
    static const uint32_t disk_sn_rva[4]={0x1000u,0x5000u,0x7000u,0x8000u};
    static const uint32_t disk_ds_size[4]={KUI_ARMADA_CE_LIVE_DSOUND_TEXT_BYTES,0x4dcu,0x550u,0x764u};
    static const uint32_t disk_sn_size[4]={0x3c25u,0x1208u,0x3d0u,0x4b4u};
    static const uint32_t disk_flags[4]={0x60000020u,0xc0000040u,0x40000040u,0x42000040u};
#endif
    if(profile>2u) return KUI_ARMADA_CE_LIVE_MODULE;
    bool disk=m->kind==2u && profile<2u;
    const uint32_t *rva=profile==2u?co_rva:disk?(profile==0u?disk_ds_rva:disk_sn_rva):profile==0u?ds_rva:sn_rva;
    const uint32_t *size=profile==2u?co_size:disk?(profile==0u?disk_ds_size:disk_sn_size):profile==0u?ds_size:sn_size;
    const uint32_t *expected_flags=profile==2u?co_flags:disk?disk_flags:flags;
    static const uint32_t table_rva[3]=KUI_ARMADA_CE_LIVE_TABLE_RVAS;
    /* NK moves the stable table into this exact writable-section tail and
     * publishes its canonical PROCESS0 address. An arbitrary heap table or
     * an earlier separately allocated table is not the loaded layout. */
    if(m->sections!=m->base+table_rva[profile])
        return kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_TABLE,m->node,m->sections,m->base+table_rva[profile],profile);
    for(unsigned i=0;i<m->count;i++) {
        uint32_t s[4];
        static const unsigned offset[4]={0u,4u,8u,16u};
        for(unsigned j=0;j<4u;j++) {
            enum kui_armada_ce_live_status status=kui_armada_ce_live_word(w,m->sections+28u*i+offset[j],&s[j]);
            if(status!=KUI_ARMADA_CE_LIVE_OK) return status;
            if(verify && s[j]!=m->section[4u*i+j]) return KUI_ARMADA_CE_LIVE_CHANGED;
            if(!verify) m->section[4u*i+j]=s[j];
        }
        uint32_t detail=(profile<<16)|i;
        if(s[0]!=size[i]) return kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_SECTION_SIZE,m->node,s[0],size[i],detail);
        if(s[1]!=rva[i]) return kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_SECTION_RVA,m->node,s[1],rva[i],detail);
        if(s[3]!=expected_flags[i]) return kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_SECTION_FLAGS,m->node,s[3],expected_flags[i],detail);
        if(!(disk && i==3u && !s[2]) && (s[2]<0x02000000u || s[2]>=0x80000000u ||
           (s[2]&0x01ffffffu)!=((m->base&0x01ffffffu)+rva[i]))
           )
            return kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_SECTION_ADDRESS,m->node,s[2],m->base+rva[i],detail);
    }
    return KUI_ARMADA_CE_LIVE_OK;
}
static inline enum kui_armada_ce_live_status kui_armada_ce_live_disk_identity(
    struct kui_armada_ce_live_workspace *w,const struct kui_armada_ce_live_module *m,unsigned profile) {
#if defined(KUI_WINCE_CDDA_WORMS) && KUI_WINCE_CDDA_WORMS == 1
    static const uint32_t offsets[2][3]={{0x1598u,0x1924u,0x2bacu},{0x1110u,0x1200u,0x12f4u}};
    static const uint32_t crc[2][3]={{0x3486e9e0u,0x06c05de7u,0x17ca5406u},{0x5e7cdea8u,0xa437a283u,0x05db23f8u}};
#else
    static const uint32_t offsets[2][3]={{0x15ecu,0x19dcu,KUI_ARMADA_CE_LIVE_BUFFER_PLAY_RVA},{0x10d8u,0x11c8u,0x12b8u}};
    static const uint32_t crc[2][3]={{0xeb34aa87u,0x34718dd9u,0x3453b46fu},{0x5ba8b52bu,0x2718008fu,0x834f5156u}};
#endif
    if(profile>1u || m->kind!=2u) return KUI_ARMADA_CE_LIVE_ARGUMENT;
    /* Each 32-byte range is proved free of HIGHLOW relocations in the exact
     * input DLL. Unlike relocated vtable words, its identity stays stable. */
    for(unsigned i=0;i<3u;i++) {
        enum kui_armada_ce_live_status status=kui_armada_ce_live_crc_range(w,m->base+offsets[profile][i],32u,crc[profile][i]);
        if(status!=KUI_ARMADA_CE_LIVE_OK) return status;
    }
    return KUI_ARMADA_CE_LIVE_OK;
}
static inline bool kui_armada_ce_live_method(uint32_t pointer,uint32_t slot,uint32_t base,uint32_t rva,uint32_t *out) {
    if(!kui_armada_ce_live_owner_pointer(pointer,slot,out)) return false;
    return *out==(slot|((base&0x01ffffffu)+rva)) || *out==base+rva;
}
static inline enum kui_armada_ce_live_status kui_armada_ce_live_sound_bind_with_value_reader(
    uint32_t ttb,uint32_t ccr,kui_armada_ce_ram_read32 read32,void *context,
    kui_armada_ce_ram_read_mapped mapped32,bool native_values,
    struct kui_armada_ce_live_workspace *w,struct kui_armada_ce_live_sound *out) {
    uint32_t head,low,high,node,current,candidate=0,slot=0,id=0,value,again,p1,owner_ref[3]={0};
    enum kui_armada_ce_live_status status=KUI_ARMADA_CE_LIVE_ARGUMENT;
    if(!out) return status;
    memset(out,0,sizeof(*out));
    if(!w || !read32 || (native_values && !mapped32) ||
       !kui_armada_ce_ram_table_p1(ttb,&p1)) return status;
    /* Workspace metadata is mutable scratch, while the result is published
     * only after every witness repeats. An aliased output cannot satisfy that
     * contract. Check with subtraction to avoid an address-end overflow. */
    uintptr_t output_address=(uintptr_t)(void *)out,workspace_address=(uintptr_t)(void *)w;
    if((output_address>=workspace_address && output_address-workspace_address<sizeof(*w)) ||
       (output_address<workspace_address && workspace_address-output_address<sizeof(*out))) return status;
    memset(w,0,sizeof(*w));w->read32=read32;w->mapped32=mapped32;w->context=context;w->ttb=ttb;w->ccr=ccr;
    w->native_values=native_values;
    w->failure=KUI_ARMADA_CE_LIVE_READ;
    if((ccr&0xa1u)!=1u) {status=KUI_ARMADA_CE_LIVE_CACHE;goto done;}
    if(!kui_armada_ce_live_physical(w,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,&current) ||
       !kui_armada_ce_live_physical(w,KUI_ARMADA_CE_LIVE_HEAP_LOW,&low) ||
       !kui_armada_ce_live_physical(w,KUI_ARMADA_CE_LIVE_HEAP_HIGH,&high) ||
       !kui_armada_ce_live_physical(w,KUI_ARMADA_CE_LIVE_MODULE_HEAD,&head)) {status=w->failure;goto done;}
    if(!kui_armada_ce_ram_table_p1(low,&p1) || high<=low || high>0x8d000000u) {
        status=kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_HEAP,head,low,high,0);goto done;
    }
    if(current<KUI_ARMADA_CE_LIVE_PROCESS_BASE ||
       current>KUI_ARMADA_CE_LIVE_PROCESS_BASE+31u*KUI_ARMADA_CE_LIVE_PROCESS_STRIDE ||
       (current-KUI_ARMADA_CE_LIVE_PROCESS_BASE)%KUI_ARMADA_CE_LIVE_PROCESS_STRIDE) {
        status=KUI_ARMADA_CE_LIVE_OWNER;goto done;
    }
    for(node=head;node;) {
        if(w->links==KUI_ARMADA_CE_LIVE_MODULE_LIMIT) {status=KUI_ARMADA_CE_LIVE_BUDGET;goto done;}
        if(!kui_armada_ce_ram_table_p1(node,&p1) || node<low || node>=high ||
           high-node<180u) {status=kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_NODE,node,low,high,w->links);goto done;}
        for(unsigned i=0;i<w->links;i++) if(w->link[i].node==node) {status=kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_CYCLE,node,node,0,w->links);goto done;}
        struct kui_armada_ce_live_link *link=&w->link[w->links++];link->node=node;
        if(!kui_armada_ce_live_physical(w,node,&value) ||
           !kui_armada_ce_live_physical(w,node+4u,&link->next) ||
           !kui_armada_ce_live_physical(w,node+96u,&link->toc) ||
           !kui_armada_ce_live_physical(w,node+100u,&link->kind)) {status=w->failure;goto done;}
        if(value!=node) {status=kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_SELF,node,value,node,w->links);goto done;}
        struct kui_armada_ce_live_module *m=NULL;unsigned profile=0;
        if((link->kind&255u)==3u && link->toc==KUI_ARMADA_CE_LIVE_DSOUND_TOC) {m=&w->dsound;profile=0;}
        if((link->kind&255u)==3u && link->toc==KUI_ARMADA_CE_LIVE_SNDCORE_TOC) {m=&w->sndcore;profile=1;}
        if((link->kind&255u)==3u && link->toc==KUI_ARMADA_CE_LIVE_COREDLL_TOC) {m=&w->coredll;profile=2;}
        if(m) {
            if(m->node) {status=kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_CYCLE,node,m->node,0,profile);goto done;}
            status=kui_armada_ce_live_module_fields(w,node,m,profile,3u);
            if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
        } else if((link->kind&255u)==2u) {
            /* NK opens a filesystem DLL before trying ROM. The file-reader
             * handle in +0x60 is not a ROM TOC or a name pointer. Identify
             * only the exact external sound images by layout and immutable
             * code; unrelated loaded DLLs remain unrelated. */
            uint32_t span,count;
            if(!kui_armada_ce_live_physical(w,node+124u,&span) ||
               !kui_armada_ce_live_physical(w,node+112u,&count)) {status=w->failure;goto done;}
            if((span==KUI_ARMADA_CE_LIVE_DSOUND_SPAN || span==0x9000u) && (count&0xffffu)==4u) {
                struct kui_armada_ce_live_module candidate_module;
                profile=span==KUI_ARMADA_CE_LIVE_DSOUND_SPAN?0u:1u;
                status=kui_armada_ce_live_module_fields(w,node,&candidate_module,profile,2u);
                if(status==KUI_ARMADA_CE_LIVE_MODULE) {memset(w->fault,0,sizeof(w->fault));node=link->next;continue;}
                if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
                w->native_candidate=&candidate_module;
                status=kui_armada_ce_live_sections(w,&candidate_module,profile,false);
                w->native_candidate=NULL;
                if(status==KUI_ARMADA_CE_LIVE_MODULE) {memset(w->fault,0,sizeof(w->fault));node=link->next;continue;}
                if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
                w->native_candidate=&candidate_module;
                status=kui_armada_ce_live_disk_identity(w,&candidate_module,profile);
                w->native_candidate=NULL;
                if(status==KUI_ARMADA_CE_LIVE_IDENTITY) {node=link->next;continue;}
                if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
                m=profile==0u?&w->dsound:&w->sndcore;
                if(m->node) {status=kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_CYCLE,node,m->node,0,profile);goto done;}
                *m=candidate_module;
            }
        }
        node=link->next;
    }
    if(!w->dsound.node || !w->sndcore.node || !w->coredll.node) {
        status=kui_armada_ce_live_module_fail(w,KUI_ARMADA_CE_LIVE_FAULT_MISSING,w->dsound.node,w->sndcore.node,w->coredll.node,w->links);goto done;
    }
    status=kui_armada_ce_live_sections(w,&w->dsound,0,false);
    if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
    status=kui_armada_ce_live_sections(w,&w->sndcore,1,false);
    if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
    status=kui_armada_ce_live_sections(w,&w->coredll,2,false);
    if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
    for(unsigned i=0;i<32u;i++) {
        uint32_t process=KUI_ARMADA_CE_LIVE_PROCESS_BASE+KUI_ARMADA_CE_LIVE_PROCESS_STRIDE*i;
        if(!(w->dsound.attach&w->sndcore.attach&w->coredll.attach&(UINT32_C(1)<<i))) continue;
        uint32_t index,base,dsref,snref,coref;
        if(!kui_armada_ce_live_physical(w,process,&index) ||
           !kui_armada_ce_live_physical(w,process+12u,&base) ||
           !kui_armada_ce_live_physical(w,w->dsound.node+16u+4u*(i/2u),&dsref) ||
           !kui_armada_ce_live_physical(w,w->sndcore.node+16u+4u*(i/2u),&snref) ||
           !kui_armada_ce_live_physical(w,w->coredll.node+16u+4u*(i/2u),&coref)) {status=w->failure;goto done;}
        if(!base) continue;
        if((index&255u)!=i || base!=((i+1u)<<25) ||
           !((dsref>>(16u*(i&1u)))&0xffffu) || !((snref>>(16u*(i&1u)))&0xffffu) ||
           !((coref>>(16u*(i&1u)))&0xffffu)) {
            status=KUI_ARMADA_CE_LIVE_OWNER;goto done;
        }
        status=kui_armada_ce_live_game_guard(w,base);
        if(status==KUI_ARMADA_CE_LIVE_MAPPING || status==KUI_ARMADA_CE_LIVE_IDENTITY) continue;
        if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
        if(candidate) {status=KUI_ARMADA_CE_LIVE_OWNER;goto done;}
        candidate=process;slot=base;id=i;
        owner_ref[0]=(dsref>>(16u*(i&1u)))&0xffffu;
        owner_ref[1]=(snref>>(16u*(i&1u)))&0xffffu;
        owner_ref[2]=(coref>>(16u*(i&1u)))&0xffffu;
    }
    if(!candidate) {status=KUI_ARMADA_CE_LIVE_OWNER;goto done;}
    struct kui_armada_ce_live_sound result={0};
    result.current_process=current;result.owner_process=candidate;result.owner_index=id;result.owner_slot=slot;
    result.dsound_module=w->dsound.node;result.sndcore_module=w->sndcore.node;
    result.coredll_module=w->coredll.node;
    result.dsound_base=w->dsound.base;result.sndcore_base=w->sndcore.base;result.coredll_base=w->coredll.base;
    uint32_t object_raw,singleton_raw,vtable_raw,refs,iat,methods[4],cs[2],buffer_methods[8];
    uint32_t singleton=slot|((w->dsound.base&0x01ffffffu)+KUI_ARMADA_CE_LIVE_SINGLETON_RVA);
    uint32_t game_holder=slot|KUI_ARMADA_CE_LIVE_GAME_DEVICE;
#if defined(KUI_WINCE_CDDA_WORMS) && KUI_WINCE_CDDA_WORMS == 1
    uint32_t game_class_raw;
    if((status=kui_armada_ce_live_word(w,game_holder,&game_class_raw))!=KUI_ARMADA_CE_LIVE_OK) goto done;
    if(!kui_armada_ce_live_owner_pointer(game_class_raw,slot,&game_holder) ||
       (game_holder&0xfe000000u)!=slot) {status=KUI_ARMADA_CE_LIVE_OBJECT;goto done;}
#endif
    if((status=kui_armada_ce_live_word(w,game_holder,&object_raw))!=KUI_ARMADA_CE_LIVE_OK ||
       (status=kui_armada_ce_live_word(w,singleton,&singleton_raw))!=KUI_ARMADA_CE_LIVE_OK ||
       (status=kui_armada_ce_live_word(w,slot|KUI_ARMADA_CE_LIVE_GAME_IAT,&iat))!=KUI_ARMADA_CE_LIVE_OK) goto done;
    uint32_t singleton_pointer;
    if(!kui_armada_ce_live_owner_pointer(object_raw,slot,&result.device) ||
       !kui_armada_ce_live_owner_pointer(singleton_raw,slot,&singleton_pointer) || singleton_pointer!=result.device ||
       result.device>UINT32_MAX-12u ||
       !kui_armada_ce_live_method(iat,slot,w->dsound.base,KUI_ARMADA_CE_LIVE_CREATE_RVA,&result.direct_sound_create)) {
        status=KUI_ARMADA_CE_LIVE_OBJECT;goto done;
    }
    if((status=kui_armada_ce_live_word(w,result.device,&vtable_raw))!=KUI_ARMADA_CE_LIVE_OK ||
       (status=kui_armada_ce_live_word(w,result.device+8u,&refs))!=KUI_ARMADA_CE_LIVE_OK) goto done;
    if(!refs || refs>0x7fffffffu || !kui_armada_ce_live_method(vtable_raw,slot,w->dsound.base,KUI_ARMADA_CE_LIVE_DEVICE_VTABLE_RVA,&result.vtable)) {
        status=KUI_ARMADA_CE_LIVE_OBJECT;goto done;
    }
    result.refcount=refs;
    if((status=kui_armada_ce_live_word(w,result.vtable+4u,&methods[0]))!=KUI_ARMADA_CE_LIVE_OK ||
       (status=kui_armada_ce_live_word(w,result.vtable+8u,&methods[1]))!=KUI_ARMADA_CE_LIVE_OK ||
       (status=kui_armada_ce_live_word(w,result.vtable+12u,&methods[2]))!=KUI_ARMADA_CE_LIVE_OK ||
       (status=kui_armada_ce_live_word(w,result.vtable+24u,&methods[3]))!=KUI_ARMADA_CE_LIVE_OK) goto done;
    if(!kui_armada_ce_live_method(methods[0],slot,w->dsound.base,KUI_ARMADA_CE_LIVE_ADDREF_RVA,&result.add_ref) ||
       !kui_armada_ce_live_method(methods[1],slot,w->dsound.base,KUI_ARMADA_CE_LIVE_RELEASE_RVA,&result.release) ||
       !kui_armada_ce_live_method(methods[2],slot,w->dsound.base,KUI_ARMADA_CE_LIVE_CREATE_BUFFER_RVA,&result.create_sound_buffer) ||
       !kui_armada_ce_live_method(methods[3],slot,w->dsound.base,KUI_ARMADA_CE_LIVE_COOPERATIVE_RVA,&result.set_cooperative)) {
        status=KUI_ARMADA_CE_LIVE_OBJECT;goto done;
    }
    uint32_t ds_owner=slot|(w->dsound.base&0x01ffffffu);
    if((status=kui_armada_ce_live_word(w,ds_owner+KUI_ARMADA_CE_LIVE_ENTER_IAT_RVA,&cs[0]))!=KUI_ARMADA_CE_LIVE_OK ||
       (status=kui_armada_ce_live_word(w,ds_owner+KUI_ARMADA_CE_LIVE_LEAVE_IAT_RVA,&cs[1]))!=KUI_ARMADA_CE_LIVE_OK) goto done;
    if(!kui_armada_ce_live_method(cs[0],slot,w->coredll.base,KUI_ARMADA_CE_LIVE_ENTER_RVA,&result.enter_cs) ||
       !kui_armada_ce_live_method(cs[1],slot,w->coredll.base,KUI_ARMADA_CE_LIVE_LEAVE_RVA,&result.leave_cs)) {
        status=KUI_ARMADA_CE_LIVE_OBJECT;goto done;
    }
    status=kui_armada_ce_live_crc_range(w,result.enter_cs,32u,KUI_ARMADA_CE_LIVE_ENTER_CRC);
    if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
    status=kui_armada_ce_live_crc_range(w,result.leave_cs,32u,KUI_ARMADA_CE_LIVE_LEAVE_CRC);
    if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
    result.buffer_vtable=result.vtable-KUI_ARMADA_CE_LIVE_DEVICE_VTABLE_RVA+KUI_ARMADA_CE_LIVE_BUFFER_VTABLE_RVA;
    static const unsigned buffer_offset[8]={8u,12u,44u,76u,48u,72u,36u,16u};
#if defined(KUI_WINCE_CDDA_WORMS) && KUI_WINCE_CDDA_WORMS == 1
    static const uint32_t buffer_rva[8]={0x2744u,0x283cu,0x2a10u,0x3744u,0x2bacu,0x364cu,0x2980u,0x2884u};
#elif defined(KUI_WINCE_CDDA_BAM4) && KUI_WINCE_CDDA_BAM4 == 1
    static const uint32_t buffer_rva[8]={0x2918u,0x2a04u,0x2cc4u,0x39b4u,0x2e5cu,0x38a0u,0x2c34u,0x2a4cu};
#else
    static const uint32_t buffer_rva[8]={0x2910u,0x29fcu,0x2cbcu,0x39acu,0x2e54u,0x3898u,0x2c2cu,0x2a44u};
#endif
    uint32_t *buffer_target[8]={&result.buffer_release,&result.buffer_get_caps,&result.buffer_lock,&result.buffer_unlock,
        &result.buffer_play,&result.buffer_stop,&result.buffer_get_status,&result.buffer_get_position};
    for(unsigned i=0;i<8u;i++) {
        status=kui_armada_ce_live_word(w,result.buffer_vtable+buffer_offset[i],&buffer_methods[i]);
        if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
        if(!kui_armada_ce_live_method(buffer_methods[i],slot,w->dsound.base,buffer_rva[i],buffer_target[i])) {
            status=KUI_ARMADA_CE_LIVE_OBJECT;goto done;
        }
    }
    /* A success is a stable observation, never an acquired COM reference.
     * Repeat guards and every published mapping/metadata before publishing. */
    status=kui_armada_ce_live_game_guard(w,slot);
    if(status==KUI_ARMADA_CE_LIVE_IDENTITY) status=KUI_ARMADA_CE_LIVE_CHANGED;
    if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
    status=kui_armada_ce_live_sections(w,&w->dsound,0,true);
    if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
    status=kui_armada_ce_live_sections(w,&w->sndcore,1,true);
    if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
    status=kui_armada_ce_live_sections(w,&w->coredll,2,true);
    if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
    if(w->dsound.kind==2u) {
        status=kui_armada_ce_live_disk_identity(w,&w->dsound,0u);
        if(status==KUI_ARMADA_CE_LIVE_IDENTITY) status=KUI_ARMADA_CE_LIVE_CHANGED;
        if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
    }
    if(w->sndcore.kind==2u) {
        status=kui_armada_ce_live_disk_identity(w,&w->sndcore,1u);
        if(status==KUI_ARMADA_CE_LIVE_IDENTITY) status=KUI_ARMADA_CE_LIVE_CHANGED;
        if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
    }
    status=kui_armada_ce_live_crc_range(w,result.enter_cs,32u,KUI_ARMADA_CE_LIVE_ENTER_CRC);
    if(status==KUI_ARMADA_CE_LIVE_IDENTITY) status=KUI_ARMADA_CE_LIVE_CHANGED;
    if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
    status=kui_armada_ce_live_crc_range(w,result.leave_cs,32u,KUI_ARMADA_CE_LIVE_LEAVE_CRC);
    if(status==KUI_ARMADA_CE_LIVE_IDENTITY) status=KUI_ARMADA_CE_LIVE_CHANGED;
    if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
    for(unsigned i=0;i<w->pages;i++) {
        status=kui_armada_ce_live_word(w,w->page[i].va,&again);
        if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
    }
    for(unsigned i=0;i<w->links;i++) {
        struct kui_armada_ce_live_link *link=&w->link[i];
        uint32_t fresh[4];static const unsigned offset[4]={0u,4u,96u,100u};
        for(unsigned j=0;j<4u;j++) if(!kui_armada_ce_live_physical(w,link->node+offset[j],&fresh[j])) {status=w->failure;goto done;}
        if(fresh[0]!=link->node || fresh[1]!=link->next || fresh[2]!=link->toc || fresh[3]!=link->kind) {
            status=KUI_ARMADA_CE_LIVE_CHANGED;goto done;
        }
    }
    const struct kui_armada_ce_live_module *mods[3]={&w->dsound,&w->sndcore,&w->coredll};
    for(unsigned i=0;i<3u;i++) {
        const struct kui_armada_ce_live_module *m=mods[i];
        static const unsigned offset[6]={12u,80u,124u,176u,112u,84u};
        uint32_t expected[6]={m->attach,m->base,m->span,m->sections,m->count,m->flags};
        for(unsigned j=0;j<6u;j++) {
            if(!kui_armada_ce_live_physical(w,m->node+offset[j],&again)) {status=w->failure;goto done;}
            if((j==4u?(again&0xffffu):again)!=expected[j]) {status=KUI_ARMADA_CE_LIVE_CHANGED;goto done;}
        }
        if(!kui_armada_ce_live_physical(w,m->node+16u+4u*(id/2u),&again)) {status=w->failure;goto done;}
        if(((again>>(16u*(id&1u)))&0xffffu)!=owner_ref[i]) {status=KUI_ARMADA_CE_LIVE_CHANGED;goto done;}
    }
    if(!kui_armada_ce_live_physical(w,candidate,&again)) {status=w->failure;goto done;}
    if((again&255u)!=id) {status=KUI_ARMADA_CE_LIVE_CHANGED;goto done;}
    if(!kui_armada_ce_live_physical(w,candidate+12u,&again)) {status=w->failure;goto done;}
    if(again!=slot) {status=KUI_ARMADA_CE_LIVE_CHANGED;goto done;}
    if(!kui_armada_ce_live_physical(w,KUI_ARMADA_CE_LIVE_MODULE_HEAD,&again)) {status=w->failure;goto done;}
    if(again!=head) {status=KUI_ARMADA_CE_LIVE_CHANGED;goto done;}
    if(!kui_armada_ce_live_physical(w,KUI_ARMADA_CE_LIVE_HEAP_LOW,&again)) {status=w->failure;goto done;}
    if(again!=low) {status=KUI_ARMADA_CE_LIVE_CHANGED;goto done;}
    if(!kui_armada_ce_live_physical(w,KUI_ARMADA_CE_LIVE_HEAP_HIGH,&again)) {status=w->failure;goto done;}
    if(again!=high) {status=KUI_ARMADA_CE_LIVE_CHANGED;goto done;}
    if(!kui_armada_ce_live_physical(w,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,&again)) {status=w->failure;goto done;}
    if(again!=current) {status=KUI_ARMADA_CE_LIVE_CHANGED;goto done;}
#if defined(KUI_WINCE_CDDA_WORMS) && KUI_WINCE_CDDA_WORMS == 1
    status=kui_armada_ce_live_word(w,slot|KUI_ARMADA_CE_LIVE_GAME_DEVICE,&again);
    if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
    if(again!=game_class_raw) {status=KUI_ARMADA_CE_LIVE_CHANGED;goto done;}
#endif
    static const unsigned verify_offset[9]={KUI_ARMADA_CE_LIVE_GAME_DEVICE,KUI_ARMADA_CE_LIVE_GAME_IAT,0u,8u,4u,8u,12u,24u,0u};
    for(unsigned i=0;i<9u;i++) {
        uint32_t address=i==0u?game_holder:i==1u?(slot|verify_offset[i]):i<4u?result.device+verify_offset[i]:
            i<8u?result.vtable+verify_offset[i]:singleton;
        uint32_t expected=i==0u?object_raw:i==1u?iat:i==2u?vtable_raw:i==3u?refs:i<8u?methods[i-4u]:singleton_raw;
        status=kui_armada_ce_live_word(w,address,&again);
        if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
        if(again!=expected) {status=KUI_ARMADA_CE_LIVE_CHANGED;goto done;}
    }
    for(unsigned i=0;i<2u;i++) {
        status=kui_armada_ce_live_word(w,ds_owner+KUI_ARMADA_CE_LIVE_ENTER_IAT_RVA+4u*i,&again);
        if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
        if(again!=cs[i]) {status=KUI_ARMADA_CE_LIVE_CHANGED;goto done;}
    }
    for(unsigned i=0;i<8u;i++) {
        status=kui_armada_ce_live_word(w,result.buffer_vtable+buffer_offset[i],&again);
        if(status!=KUI_ARMADA_CE_LIVE_OK) goto done;
        if(again!=buffer_methods[i]) {status=KUI_ARMADA_CE_LIVE_CHANGED;goto done;}
    }
    result.reads=w->reads;*out=result;status=KUI_ARMADA_CE_LIVE_OK;
done:
    if(status==KUI_ARMADA_CE_LIVE_OK) memset(w->fault,0,sizeof(w->fault));
    out->reads=w->reads;return status;
}
static inline enum kui_armada_ce_live_status kui_armada_ce_live_sound_bind_with_reader(
    uint32_t ttb,uint32_t ccr,kui_armada_ce_ram_read32 read32,void *context,
    kui_armada_ce_ram_read_mapped mapped32,struct kui_armada_ce_live_workspace *w,
    struct kui_armada_ce_live_sound *out) {
    return kui_armada_ce_live_sound_bind_with_value_reader(ttb,ccr,read32,context,
        mapped32,false,w,out);
}
static inline enum kui_armada_ce_live_status kui_armada_ce_live_sound_bind(
    uint32_t ttb,uint32_t ccr,kui_armada_ce_ram_read32 read32,void *context,
    struct kui_armada_ce_live_workspace *w,struct kui_armada_ce_live_sound *out) {
    return kui_armada_ce_live_sound_bind_with_reader(ttb,ccr,read32,context,NULL,w,out);
}
/* Exact COREDLL wrappers in the user-supplied Armada CE kernel. Prefix lengths
 * deliberately exclude relocated literal pools (notably Sleep). Resolve in
 * the same live address domain as DSOUND's verified EnterCriticalSection IAT,
 * then verify bytes and metadata again before publishing callable targets. */
static inline enum kui_armada_ce_live_status kui_armada_ce_live_stream_api_bind(
    struct kui_armada_ce_live_workspace *w,const struct kui_armada_ce_live_sound *sound,
    struct kui_armada_ce_live_stream_api *out) {
    if(!out) return KUI_ARMADA_CE_LIVE_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!w || !sound || !w->read32 || !sound->owner_process ||
       !w->links || w->links>KUI_ARMADA_CE_LIVE_MODULE_LIMIT ||
       !w->pages || w->pages>KUI_ARMADA_CE_LIVE_PAGE_LIMIT ||
       sound->owner_index>=32u || sound->owner_process!=KUI_ARMADA_CE_LIVE_PROCESS_BASE+
           KUI_ARMADA_CE_LIVE_PROCESS_STRIDE*sound->owner_index ||
       sound->owner_slot!=((sound->owner_index+1u)<<25) ||
       sound->coredll_module!=w->coredll.node || sound->coredll_base!=w->coredll.base ||
       sound->dsound_module!=w->dsound.node || sound->dsound_base!=w->dsound.base ||
       sound->enter_cs<KUI_ARMADA_CE_LIVE_ENTER_RVA) return KUI_ARMADA_CE_LIVE_ARGUMENT;
    struct kui_armada_ce_live_stream_api result={0};
    result.owner_process=sound->owner_process;result.owner_slot=sound->owner_slot;
    result.coredll_module=sound->coredll_module;
    result.enter_cs=sound->enter_cs;result.leave_cs=sound->leave_cs;
    uint32_t alias=sound->enter_cs-KUI_ARMADA_CE_LIVE_ENTER_RVA,handle,current,position;
    uint32_t admitted;
    if(!kui_armada_ce_live_method(alias+KUI_ARMADA_CE_LIVE_LEAVE_RVA,sound->owner_slot,w->coredll.base,KUI_ARMADA_CE_LIVE_LEAVE_RVA,&admitted) ||
       admitted!=sound->leave_cs) return KUI_ARMADA_CE_LIVE_OBJECT;
    if(!kui_armada_ce_live_physical(w,sound->owner_process+8u,&handle) ||
       !kui_armada_ce_live_physical(w,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,&current)) return w->failure;
    if(!handle || handle==UINT32_MAX || current!=sound->current_process)
        return KUI_ARMADA_CE_LIVE_CHANGED;
    result.owner_handle=handle;
#if defined(KUI_WINCE_CDDA_WORMS) && KUI_WINCE_CDDA_WORMS == 1
    static const uint32_t rva[11]={0x8900u,0x83f4u,0x82e4u,0x8414u,0x8358u,0x8110u,
        0x848cu,0x8494u,0x8824u,0x88b8u,0x84ecu};
#else
    static const uint32_t rva[11]={0x8d20u,0x8814u,0x8708u,0x8834u,0x877cu,0x852cu,
        0x88acu,0x88b4u,0x8c44u,0x8cd8u,0x890cu};
#endif
    static const uint32_t bytes[11]={8u,8u,32u,8u,32u,8u,8u,8u,20u,8u,8u};
    static const uint32_t crc[11]={0xc1eabb03u,0xd3f7581fu,0xa6a3bf77u,0xe1c13a9du,
        0xddf88da0u,0x4dcea5a7u,0x3a94fd2au,0x08a29fa8u,0x7ae32575u,0x914c763fu,0xdce3a077u};
    uint32_t *target[11]={&result.create_thread,&result.resume_thread,&result.sleep,&result.set_thread_priority,
        &result.wait_single,&result.close_handle,&result.virtual_alloc,&result.virtual_free,
        &result.set_kmode,&result.flush_instruction_cache,&result.get_tick_count};
    for(unsigned i=0;i<11u;i++) {
        if(!kui_armada_ce_live_method(alias+rva[i],sound->owner_slot,w->coredll.base,rva[i],target[i]))
            return KUI_ARMADA_CE_LIVE_OBJECT;
        enum kui_armada_ce_live_status status=kui_armada_ce_live_crc_range(w,*target[i],bytes[i],crc[i]);
        if(status!=KUI_ARMADA_CE_LIVE_OK) return status;
    }
    enum kui_armada_ce_live_status status=kui_armada_ce_live_word(w,sound->buffer_vtable+52u,&position);
    if(status!=KUI_ARMADA_CE_LIVE_OK) return status;
    if(!kui_armada_ce_live_method(position,sound->owner_slot,w->dsound.base,KUI_ARMADA_CE_LIVE_BUFFER_POSITION_RVA,&result.buffer_set_position))
        return KUI_ARMADA_CE_LIVE_OBJECT;
    status=kui_armada_ce_live_crc_range(w,result.buffer_set_position,32u,KUI_ARMADA_CE_LIVE_POSITION_CRC);
    if(status!=KUI_ARMADA_CE_LIVE_OK) return status;
    status=kui_armada_ce_live_game_guard(w,sound->owner_slot);
    if(status!=KUI_ARMADA_CE_LIVE_OK) return status==KUI_ARMADA_CE_LIVE_IDENTITY?KUI_ARMADA_CE_LIVE_CHANGED:status;
    const struct kui_armada_ce_live_module *mods[3]={&w->dsound,&w->sndcore,&w->coredll};
    for(unsigned i=0;i<3u;i++) {
        const struct kui_armada_ce_live_module *m=mods[i];
        static const unsigned offset[6]={12u,80u,124u,176u,112u,84u};
        uint32_t expected[6]={m->attach,m->base,m->span,m->sections,m->count,m->flags};
        for(unsigned j=0;j<6u;j++) {
            uint32_t value;
            if(!kui_armada_ce_live_physical(w,m->node+offset[j],&value)) return w->failure;
            if((j==4u?(value&0xffffu):value)!=expected[j]) return KUI_ARMADA_CE_LIVE_CHANGED;
        }
        uint32_t refs;
        if(!kui_armada_ce_live_physical(w,m->node+16u+4u*(sound->owner_index/2u),&refs)) return w->failure;
        if(!(m->attach&(UINT32_C(1)<<sound->owner_index)) ||
           !((refs>>(16u*(sound->owner_index&1u)))&0xffffu)) return KUI_ARMADA_CE_LIVE_CHANGED;
        status=kui_armada_ce_live_sections(w,(struct kui_armada_ce_live_module *)m,i,true);
        if(status!=KUI_ARMADA_CE_LIVE_OK) return status;
    }
    for(unsigned i=0;i<11u;i++) {
        status=kui_armada_ce_live_crc_range(w,*target[i],bytes[i],crc[i]);
        if(status!=KUI_ARMADA_CE_LIVE_OK) return status==KUI_ARMADA_CE_LIVE_IDENTITY?KUI_ARMADA_CE_LIVE_CHANGED:status;
    }
    status=kui_armada_ce_live_crc_range(w,result.buffer_set_position,32u,KUI_ARMADA_CE_LIVE_POSITION_CRC);
    if(status!=KUI_ARMADA_CE_LIVE_OK) return status==KUI_ARMADA_CE_LIVE_IDENTITY?KUI_ARMADA_CE_LIVE_CHANGED:status;
    uint32_t again;
    status=kui_armada_ce_live_word(w,sound->buffer_vtable+52u,&again);
    if(status!=KUI_ARMADA_CE_LIVE_OK) return status;
    if(again!=position) return KUI_ARMADA_CE_LIVE_CHANGED;
    for(unsigned i=0;i<w->pages;i++) {
        status=kui_armada_ce_live_word(w,w->page[i].va,&again);
        if(status!=KUI_ARMADA_CE_LIVE_OK) return status;
    }
    for(unsigned i=0;i<w->links;i++) {
        const struct kui_armada_ce_live_link *link=&w->link[i];
        static const unsigned offset[4]={0u,4u,96u,100u};
        uint32_t expected[4]={link->node,link->next,link->toc,link->kind};
        for(unsigned j=0;j<4u;j++) {
            if(!kui_armada_ce_live_physical(w,link->node+offset[j],&again)) return w->failure;
            if(again!=expected[j]) return KUI_ARMADA_CE_LIVE_CHANGED;
        }
    }
    status=kui_armada_ce_live_crc_range(w,result.enter_cs,32u,KUI_ARMADA_CE_LIVE_ENTER_CRC);
    if(status!=KUI_ARMADA_CE_LIVE_OK) return status==KUI_ARMADA_CE_LIVE_IDENTITY?KUI_ARMADA_CE_LIVE_CHANGED:status;
    status=kui_armada_ce_live_crc_range(w,result.leave_cs,32u,KUI_ARMADA_CE_LIVE_LEAVE_CRC);
    if(status!=KUI_ARMADA_CE_LIVE_OK) return status==KUI_ARMADA_CE_LIVE_IDENTITY?KUI_ARMADA_CE_LIVE_CHANGED:status;
    if(!kui_armada_ce_live_physical(w,sound->owner_process,&again)) return w->failure;
    if((again&255u)!=sound->owner_index) return KUI_ARMADA_CE_LIVE_CHANGED;
    if(!kui_armada_ce_live_physical(w,sound->owner_process+8u,&again)) return w->failure;
    if(again!=handle) return KUI_ARMADA_CE_LIVE_CHANGED;
    if(!kui_armada_ce_live_physical(w,sound->owner_process+12u,&again)) return w->failure;
    if(again!=sound->owner_slot) return KUI_ARMADA_CE_LIVE_CHANGED;
    if(!kui_armada_ce_live_physical(w,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,&again)) return w->failure;
    if(again!=current) return KUI_ARMADA_CE_LIVE_CHANGED;
    *out=result;return KUI_ARMADA_CE_LIVE_OK;
}
#endif
