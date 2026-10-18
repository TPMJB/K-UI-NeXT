/* SPDX-License-Identifier: GPL-3.0-only */
/* Exact-owner schedule observation and explicitly selected virtual VMU proofs. */
#include "kui/vmu_maple_observe.h"
#if KUI_VMU_READONLY_PROOF
#include "kui/vmu_readonly.h"
#include "kui/vmu_readonly_overlay.h"
#endif
#if KUI_VMU_READONLY_PROOF == 3
#include "kui/vmu_volatile.h"
#include "kui/vmu_volatile_chain.h"
#endif
#if KUI_VMU_READONLY_PROOF >= 2
#include "kui/vmu_sd_profile.h"
#include "kui/vmu_backing.h"
#include "kui/retail_image.h"
#include "sd_reader.h"
#include <string.h>
#endif
#include "retail_display.h"

#define PHYSICAL_BASE UINT32_C(0x0c000000)
#define WORKER_BEGIN UINT32_C(0x0cfd0000)
#define PROTECTED_BEGIN (WORKER_BEGIN - UINT32_C(32))
#define RAM_END UINT32_C(0x0d000000)
#define STACK_PATTERN UINT32_C(0x564d5531)

struct kui_vmu_maple_observe_snapshot kui_vmu_maple_observer_snapshot;
static struct retail_display_state saved_display;
struct pending_descriptor {uint32_t rx,header,ordinal;};
static struct pending_descriptor pending[32];
static uint32_t pending_source;
void kui_vmu_maple_observer_report(void);
#if KUI_VMU_READONLY_PROOF
#if KUI_VMU_READONLY_PROOF == 3
static struct kui_vmu_volatile ram_card;
static _Alignas(32) uint8_t issued_chain[512];
static struct kui_vmu_volatile_chain_frame captured_chain[32];
static uint32_t write_payloads[32][32];
static struct kui_vmu_volatile_chain_plan issued_plan;
static uint32_t original_hash,chain_state;
enum {CHAIN_IDLE,CHAIN_PREPARED,CHAIN_ISSUED,CHAIN_RESTORED,CHAIN_FAULT};
_Static_assert(sizeof(issued_chain)==512u && sizeof(captured_chain)==512u &&
    sizeof(write_payloads)==4096u,"Volatile DMA private buffer ABI");
#else
static struct kui_vmu_readonly blank_card;
#endif
static struct kui_vmu_readonly_overlay overlay;
static struct kui_vmu_readonly_overlay_frame proof_frames[32];
#endif
#if KUI_VMU_READONLY_PROOF >= 2
struct observe_sd_state {
    struct kui_vmu_sd_profile profile;
    _Alignas(32) uint8_t bounce[512];
    uint16_t block;
    uint8_t inflight,invoked,verified,failed;
};
static struct observe_sd_state sd_state;
static int (*sd_lease)(kui_vmu_sd_transaction,void *);
_Static_assert(sizeof(struct kui_vmu_sd_profile)==1320u,"SD profile decoded ABI");
#endif

#ifdef KUI_VMU_MAPLE_OBSERVE_HOST_TEST
static uint32_t host_stack[KUI_VMU_MAPLE_OBSERVE_STACK_BYTES / 4u];
#define STACK_BOTTOM host_stack
#define STACK_TOP (host_stack + sizeof(host_stack) / sizeof(host_stack[0]))
static uint32_t read_word(uint32_t address) {
    return kui_vmu_maple_observer_test_read(address);
}
static uint32_t read_byte(uint32_t address) {
    return kui_vmu_maple_observer_test_read(address)&255u;
}
#else
extern uint32_t stack_bottom[] __asm__("__vmu_observe_stack_bottom");
extern uint32_t stack_top[] __asm__("__vmu_observe_stack_top");
#define STACK_BOTTOM stack_bottom
#define STACK_TOP stack_top
static uint32_t read_word(uint32_t address) {
    return *(volatile const uint32_t *)(uintptr_t)address;
}
static uint32_t read_byte(uint32_t address) {
    return *(volatile const uint8_t *)(uintptr_t)address;
}
#endif

static void increment(uint32_t *value) {
    if(*value != UINT32_MAX)
        ++*value;
}

#if KUI_VMU_READONLY_PROOF == 3
static void write_word(uint32_t address,uint32_t value) {
#ifdef KUI_VMU_MAPLE_OBSERVE_HOST_TEST
    kui_vmu_maple_observer_test_write(address,value);
#else
    *(volatile uint32_t *)(uintptr_t)address=value;
#endif
}
static uint32_t original_fingerprint(uint32_t source,uint32_t bytes) {
    uint32_t hash=UINT32_C(2166136261);
    for(uint32_t at=0;at<bytes;at+=4u)
        hash=(hash^read_word((source+at)|UINT32_C(0xa0000000)))*UINT32_C(16777619);
    return hash;
}
static void chain_status(unsigned status) {
    chain_state=status;kui_vmu_maple_observer_snapshot.chain_state=status;
}
static uint32_t chain_physical(void) {
#ifdef KUI_VMU_MAPLE_OBSERVE_HOST_TEST
    return kui_vmu_maple_observer_test_private_chain(issued_chain,sizeof(issued_chain));
#else
    uintptr_t pointer=(uintptr_t)issued_chain;
    if((pointer&31u) || pointer<UINT32_C(0xacfd0000) ||
       pointer>UINT32_C(0xacfe0000)-sizeof(issued_chain)) return 0;
    return (uint32_t)pointer&UINT32_C(0x1fffffff);
#endif
}
static int physical_response_valid(const struct pending_descriptor *frame) {
    unsigned address=(frame->header>>8)&255u,port=address&192u,cmd=frame->header&255u;
    if(address==1u) return 1; /* This request never reached hardware. */
    uint32_t header=read_word(frame->rx|UINT32_C(0xa0000000));
    if(header==UINT32_MAX) return address!=0x20u; /* Empty physical ports B-D. */
    unsigned expected=cmd==1u?5u:cmd==2u?6u:8u,words=cmd==1u?28u:cmd==2u?48u:3u;
    if((header&255u)!=expected || ((header>>8)&255u)!=port ||
       ((header>>16)&224u)!=(port|32u) || (header>>24)!=words) return 0;
    uint32_t function=read_word((frame->rx+4u)|UINT32_C(0xa0000000));
    return cmd==9u?function==UINT32_C(0x01000000):!!(function&UINT32_C(0x01000000));
}
#endif

#if KUI_VMU_READONLY_PROOF >= 2
/* The resident gateway invokes this exact exported entry with a temporary
 * protocol descriptor, while it owns the real GD lock and SCI lease. Only
 * our private aligned bounce can receive the DMA-facing card bytes. */
int kui_vmu_maple_observer_sd_transaction(void *context,struct kui_loader_sd *loan) {
    struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    if(context!=&sd_state || !loan || s->initialized!=1u ||
       !sd_state.inflight || sd_state.invoked || sd_state.failed) {
        s->sd_last_result=KUI_VMU_OBSERVE_SD_CONTEXT;
        if(loan) loan->ready=false;
        return -1;
    }
    sd_state.invoked=1u;
    if(!loan->ready || loan->slow || loan->blocks!=sd_state.profile.card_sectors ||
       loan->high_capacity!=!!sd_state.profile.high_capacity) {
        s->sd_last_result=KUI_VMU_OBSERVE_SD_IDENTITY;goto fail;
    }
    struct kui_vmu_sd_identity identity;
    increment(&s->sd_cid_checks);
    if(kui_vmu_sd_identity_read(loan,&identity)!=KUI_VMU_BACKING_OK ||
       identity.sectors!=sd_state.profile.card_sectors ||
       identity.high_capacity!=!!sd_state.profile.high_capacity ||
       memcmp(identity.cid,sd_state.profile.cid,16u)) {
        s->sd_last_result=KUI_VMU_OBSERVE_SD_IDENTITY;goto fail;
    }
    struct kui_loader_sd_stream stream={0};
    enum kui_loader_sd_result result=kui_loader_sd_stream_start(loan,&stream,s->sd_last_lba,1u);
    if(result==KUI_LOADER_SD_OK) result=kui_loader_sd_stream_next(loan,&stream,sd_state.bounce);
    enum kui_loader_sd_result stopped=kui_loader_sd_stream_stop(loan,&stream);
    if(result!=KUI_LOADER_SD_OK || stopped!=KUI_LOADER_SD_OK || !loan->ready) {
        s->sd_last_result=KUI_VMU_OBSERVE_SD_READ;goto fail;
    }
    s->sd_crc_actual=kui_retail_crc32(0,sd_state.bounce,sizeof(sd_state.bounce));
    if(s->sd_crc_actual!=s->sd_crc_expected) {
        s->sd_last_result=KUI_VMU_OBSERVE_SD_CRC;goto fail;
    }
    sd_state.verified=1u;
    return 0;
fail:
    loan->ready=false;
    return -1;
}

static int sd_read_block(void *context,uint16_t block,uint8_t out[512]) {
    struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    if(context!=&sd_state || !out || sd_state.failed || sd_state.inflight) {
        s->sd_last_result=KUI_VMU_OBSERVE_SD_CONTEXT;
        sd_state.failed=1u;increment(&s->sd_faults);
        return KUI_VMU_EMU_IO_ERROR;
    }
    s->sd_crc_actual=0u;
    if(kui_vmu_sd_profile_map_lba(&sd_state.profile,block,&s->sd_last_lba)) {
        s->sd_last_result=KUI_VMU_OBSERVE_SD_MAPPING;
        sd_state.failed=1u;increment(&s->sd_faults);return KUI_VMU_EMU_IO_ERROR;
    }
    sd_state.block=block;s->sd_crc_expected=sd_state.profile.block_crc[block];
    sd_state.inflight=1u;sd_state.invoked=sd_state.verified=0u;
    s->sd_last_result=KUI_VMU_OBSERVE_SD_OK;
#ifdef KUI_VMU_MAPLE_OBSERVE_HOST_TEST
    int result=kui_vmu_maple_observer_test_sd_lease(kui_vmu_maple_observer_sd_transaction,&sd_state);
#else
    int result=sd_lease(kui_vmu_maple_observer_sd_transaction,&sd_state);
#endif
    sd_state.inflight=0u;
    if(STACK_BOTTOM[0]!=STACK_PATTERN || STACK_BOTTOM[1]!=STACK_PATTERN) {
        increment(&s->stack_fault);s->sd_last_result=KUI_VMU_OBSERVE_SD_CONTEXT;
        sd_state.verified=0u;
    }
    if(result!=KUI_VMU_SD_LEASE_OK || !sd_state.invoked || !sd_state.verified) {
        sd_state.failed=1u;
        if(result==KUI_VMU_SD_LEASE_BUSY) {
            s->sd_last_result=KUI_VMU_OBSERVE_SD_BUSY;increment(&s->sd_busy);
        } else {
            if(s->sd_last_result==KUI_VMU_OBSERVE_SD_OK) s->sd_last_result=KUI_VMU_OBSERVE_SD_LEASE_FAULT;
            increment(&s->sd_faults);
        }
        return KUI_VMU_EMU_IO_ERROR;
    }
    /* Cleanup/health must succeed before any byte reaches the staged reply.
     * The overlay publishes only after every response in its batch succeeds. */
    for(unsigned i=0;i<512u;i++) out[i]=sd_state.bounce[i];
    increment(&s->sd_reads);return KUI_VMU_EMU_IO_OK;
}
#endif

static int source_span(uint32_t address, uint32_t bytes) {
    return address >= PHYSICAL_BASE && address < PROTECTED_BEGIN
        && !(address & 3u) && bytes <= PROTECTED_BEGIN - address;
}

static int response_span(uint32_t address) {
    return address >= PHYSICAL_BASE && address < RAM_END
        && !(address & 31u) && 1024u <= RAM_END - address
        && !(address < UINT32_C(0x0cfe0000) && address + 1024u > UINT32_C(0x0cfcffe0));
}

static unsigned unit_index(uint8_t address) {
    unsigned unit = address & 0x3fu;
    unsigned index = 1u;
    if(unit == 0x20u)
        return 0u;
    if(!unit || unit >= 0x20u || (unit & (unit - 1u)))
        return 6u;
    while(unit > 1u) {
        unit >>= 1;
        ++index;
    }
    return index;
}

#if KUI_VMU_READONLY_PROOF
#if KUI_VMU_READONLY_PROOF == 3
static int ram_packet(void *context,const uint8_t *request,size_t bytes,
    uint8_t *response,size_t capacity,size_t *response_bytes) {
    return kui_vmu_volatile_handle(context,request,bytes,response,capacity,response_bytes);
}
static int ram_admit(void *context,uint32_t header,uint32_t payload0,uint32_t payload1) {
    return kui_vmu_volatile_admit(context,header,payload0,payload1);
}
#else
static int blank_packet(void *context,const uint8_t *request,size_t bytes,
    uint8_t *response,size_t capacity,size_t *response_bytes) {
    return kui_vmu_readonly_handle(context,request,bytes,response,capacity,response_bytes);
}
#endif
static uint8_t *proof_response(uint32_t physical) {
#ifdef KUI_VMU_MAPLE_OBSERVE_HOST_TEST
    return kui_vmu_maple_observer_test_response(physical);
#else
    return (uint8_t *)(uintptr_t)(physical|UINT32_C(0xa0000000));
#endif
}
/* These fixed arenas are established by the exact owner's initializer and
 * checked against ALL live cached SDK pointers, not inferred from RAM range.
 * RX tables and SDK bookkeeping use P1; the actual RX bytes use P2. */
static int proof_layout(uint32_t source,unsigned completed) {
    const uint32_t m=UINT32_C(0x8c1212a8);
    unsigned bank;
    if(source==UINT32_C(0x0c0ec0a0)) bank=0u;
    else if(source==UINT32_C(0x0c0ef0a0)) bank=1u;
    else return 0;
    if(read_word(UINT32_C(0x8c1223a4))!=m ||
       read_word(m+0xfc4u)!=UINT32_C(0xac0ec0a0) ||
       read_word(m+0xfc8u)!=UINT32_C(0xac0e0080) ||
       read_word(m+0x10a8u)!=UINT32_C(0xac0ec0a0) ||
       read_word(m+0x10acu)!=UINT32_C(0xac0ef0a0) ||
       read_word(m+0x10b0u)!=(source|UINT32_C(0xa0000000)) ||
       read_word(m+0x10b4u)!=(bank^completed) ||
       read_word(m+0x10b8u)!=(bank^completed) ||
       read_word(m+0x10c4u)!=0u ||
       read_word(m+0xfc0u)!=(completed?0u:1u) ||
       read_word(m+0x10f4u)!=UINT32_C(0xa05f6c00) ||
       read_word(UINT32_C(0x8c0c0964))!=UINT32_C(0xac0c0968) ||
       read_byte(UINT32_C(0xac0c0968))!=0x80u ||
       read_word(UINT32_C(0x8c118398))!=0u ||
       read_word(UINT32_C(0xa05f6c10))!=0u ||
       read_word(UINT32_C(0xa05f6c14))!=1u ||
       read_word(UINT32_C(0xa05f6c18))!=0u) return 0;
    for(unsigned i=0;i<48u;i++)
        if(read_word(m+0xfe8u+i*4u)!=UINT32_C(0xac0e0080)+i*1024u) return 0;
    return 1;
}
static void proof_stats(void) {
    struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    const struct kui_vmu_readonly_overlay_stats *p=&overlay.stats;
    s->proof_prepared=p->prepared;s->proof_completed=p->completed;
    s->proof_parents=p->advertisements;s->proof_replies=p->replies;
    s->proof_info=p->devinfo+p->allinfo;s->proof_media=p->media;s->proof_reads=p->reads;
    s->proof_conflicts=!!(p->flags&KUI_VMU_OVERLAY_PHYSICAL_CONFLICT);
    s->proof_refused=p->failures;s->proof_flags|=p->flags;
    s->proof_last_request=p->last_request;s->proof_last_response=p->last_reply;
    s->proof_last_rx=p->last_rx;
    s->proof_bytes=p->replies*4u+p->devinfo*112u+p->allinfo*192u+p->media*28u+p->reads*520u;
#if KUI_VMU_READONLY_PROOF == 3
    s->proof_last_block=ram_card.protocol.read_block;s->proof_commit_attempts=0u;
    s->ram_write_phases=ram_card.write_phases;s->ram_commits=ram_card.ram_commits;
    s->ram_blocks=ram_card.cached_blocks;s->ram_reads=ram_card.cache_reads;
    s->ram_refused=ram_card.refused;s->ram_full=ram_card.full_faults;
    s->ram_last_block=ram_card.protocol.active?ram_card.protocol.staged_block:
        ram_card.protocol.committed_valid?ram_card.protocol.committed_block:ram_card.protocol.read_block;
    s->ram_faults=ram_card.faulted;
#else
    s->proof_last_block=blank_card.protocol.read_block;
    s->proof_commit_attempts=blank_card.commit_attempts;
#endif
}
static void proof_stop(uint32_t flags) {
    struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
#if KUI_VMU_READONLY_PROOF == 3
    chain_status(CHAIN_FAULT);
#endif
    s->proof_flags|=flags;proof_stats();
    s->stop_reason=KUI_VMU_OBSERVE_PROOF_REFUSED;
    kui_vmu_maple_observer_report();
}
#if KUI_VMU_READONLY_PROOF == 3
static void chain_fail(uint32_t flags) {
    increment(&kui_vmu_maple_observer_snapshot.chain_faults);
    chain_status(CHAIN_FAULT);proof_stop(flags);
}
#endif
#endif

int kui_vmu_maple_observer_init(const struct kui_vmu_maple_observe_config *config) {
    uint32_t *words = (uint32_t *)(void *)&kui_vmu_maple_observer_snapshot;
    uint32_t *stack;
    unsigned i;
    if(!config || config->magic != KUI_VMU_MAPLE_OBSERVE_MAGIC
        || config->version != KUI_VMU_MAPLE_OBSERVE_VERSION
        || config->bytes != sizeof(*config) || !config->stop_after
        || config->stop_after > KUI_VMU_MAPLE_OBSERVE_MAX_STARTS
        || !config->post_after || config->post_after > config->stop_after)
        return -1;
    for(i = 0; i < sizeof(kui_vmu_maple_observer_snapshot) / 4u; ++i)
        words[i] = 0;
    for(i = 0; i < 14u; ++i)
        saved_display.regs[i] = config->display[i];
    for(stack = STACK_BOTTOM; stack < STACK_TOP; ++stack)
        *stack = STACK_PATTERN;
    kui_vmu_maple_observer_snapshot.magic = KUI_VMU_MAPLE_OBSERVE_MAGIC;
    kui_vmu_maple_observer_snapshot.version = KUI_VMU_MAPLE_OBSERVE_VERSION;
    kui_vmu_maple_observer_snapshot.bytes = sizeof(kui_vmu_maple_observer_snapshot);
    kui_vmu_maple_observer_snapshot.stop_after = config->stop_after;
    kui_vmu_maple_observer_snapshot.post_after = config->post_after;
    kui_vmu_maple_observer_snapshot.source_min = UINT32_MAX;
    kui_vmu_maple_observer_snapshot.rx_min = UINT32_MAX;
    kui_vmu_maple_observer_snapshot.source_fingerprint = UINT32_C(2166136261);
    pending_source=0;
#if KUI_VMU_READONLY_PROOF
    kui_vmu_maple_observer_snapshot.proof_mode=KUI_VMU_READONLY_PROOF;
#if KUI_VMU_READONLY_PROOF >= 2
    if(config->sd_profile_bytes!=sizeof(struct kui_vmu_sd_profile) ||
       (config->sd_profile&3u) || (config->sd_profile&UINT32_C(0xff000000))!=UINT32_C(0x8c000000) ||
       !source_span(config->sd_profile&UINT32_C(0x1fffffff),config->sd_profile_bytes) ||
       (config->sd_lease&1u) || config->sd_lease<UINT32_C(0x8c004000) ||
       config->sd_lease>=UINT32_C(0x8c007800)) return -1;
#ifdef KUI_VMU_MAPLE_OBSERVE_HOST_TEST
    const struct kui_vmu_sd_profile *prepared=kui_vmu_maple_observer_test_sd_profile(config->sd_profile);
#else
    const struct kui_vmu_sd_profile *prepared=(const struct kui_vmu_sd_profile *)(uintptr_t)config->sd_profile;
#endif
    if(!prepared || kui_vmu_sd_profile_validate(prepared)) return -1;
    memset(&sd_state,0,sizeof(sd_state));memcpy(&sd_state.profile,prepared,sizeof(*prepared));
    sd_lease=(int (*)(kui_vmu_sd_transaction,void *))(uintptr_t)config->sd_lease;
#else
    if(config->sd_lease || config->sd_profile || config->sd_profile_bytes) return -1;
#endif
#if KUI_VMU_READONLY_PROOF == 3
    memset(&issued_plan,0,sizeof(issued_plan));original_hash=0;chain_status(CHAIN_IDLE);
    if(kui_vmu_volatile_init(&ram_card,sd_read_block,&sd_state) ||
       kui_vmu_readonly_overlay_init_backend(&overlay,ram_packet,ram_admit,&ram_card)) return -1;
#elif KUI_VMU_READONLY_PROOF == 2
    if(kui_vmu_readonly_init_reader(&blank_card,sd_read_block,&sd_state) ||
       kui_vmu_readonly_overlay_init(&overlay,blank_packet,&blank_card)) return -1;
#else
    if(kui_vmu_readonly_init(&blank_card) ||
       kui_vmu_readonly_overlay_init(&overlay,blank_packet,&blank_card)) return -1;
#endif
#else
    if(config->sd_lease || config->sd_profile || config->sd_profile_bytes) return -1;
#endif
    kui_vmu_maple_observer_snapshot.initialized = 1u;
    return 0;
}

#ifndef KUI_VMU_MAPLE_OBSERVE_HOST_TEST
static void report_record(const struct kui_vmu_maple_observe_record *record) {
    char line[46];
    const uint32_t *words = (const uint32_t *)(const void *)record;
    if(!record->ordinal) {
        retail_display_line("EMPTY");
        return;
    }
    for(unsigned w=0;w<5u;w++) {
        for(unsigned digit=0;digit<8u;digit++)
            line[w*9u+digit]="0123456789ABCDEF"[(words[w]>>(28u-digit*4u))&15u];
        line[w*9u+8u]=' ';
    }
    line[45]=0;
    retail_display_line(line);
}
#endif

void kui_vmu_maple_observer_report(void) {
    if(!kui_vmu_maple_observer_snapshot.stop_reason)
        kui_vmu_maple_observer_snapshot.stop_reason=KUI_VMU_OBSERVE_OWNER_TERMINAL;
#ifdef KUI_VMU_MAPLE_OBSERVE_HOST_TEST
    kui_vmu_maple_observer_snapshot.initialized = 2u;
    kui_vmu_maple_observer_test_stop();
#else
    const struct kui_vmu_maple_observe_snapshot *s = &kui_vmu_maple_observer_snapshot;
    uint32_t page;
    for(page = 0;;page = (page + 1u) % (KUI_VMU_READONLY_PROOF==3?5u:4u)) {
        retail_display_restore(&saved_display);
        retail_display_line(KUI_VMU_READONLY_PROOF==3?"VIRTUAL VMU - VOLATILE RAM SAVES ONLY":
            KUI_VMU_READONLY_PROOF==2?"VIRTUAL VMU - READ ONLY SD IMAGE":
            KUI_VMU_READONLY_PROOF?"VIRTUAL VMU - READ ONLY BLANK CARD":"TOY MAPLE OBSERVER - READ ONLY");
        retail_display_values("PAGE SCHEDULES DESCRIPTORS BAD",
            (uint32_t[4]){page, s->starts, s->descriptors, s->malformed}, 4u);
        if(page == 0u) {
            retail_display_values("SOURCE MIN MAX LAST CHANGES", &s->source_min, 4u);
            retail_display_values("MAX HARD STOP STACK NO LAST", &s->descriptors_max, 4u);
            retail_display_values("PORT A B C D", s->ports, 4u);
            retail_display_values("RX MIN MAX BAD MAIN DEVINFO", &s->rx_min, 4u);
            retail_display_values("CMP CALLS MATCH ORPHAN OVERWRITE", &s->completion_calls, 4u);
            retail_display_values("PENDING BUSY MASKBAD RX HEADERS", &s->completion_pending, 4u);
        } else if(page == 1u) {
            retail_display_values("COMMAND 0 1 2 3", s->commands, 4u);
            retail_display_values("COMMAND 4 5 6 7", s->commands + 4u, 4u);
            retail_display_values("COMMAND 8 9 A B", s->commands + 8u, 4u);
            retail_display_values("COMMAND C D E F", s->commands + 12u, 4u);
            retail_display_values("MAIN UNIT1 UNIT2 UNIT3", s->units, 4u);
            retail_display_values("UNIT4 UNIT5 UNSUPPORTED",
                (uint32_t[3]){s->units[4], s->units[5], s->unsupported}, 3u);
        } else if(page == 2u) {
            unsigned total=s->subunit_requests;
            unsigned oldest=total<KUI_VMU_MAPLE_OBSERVE_RECORDS?0u:
                total%KUI_VMU_MAPLE_OBSERVE_RECORDS;
            retail_display_values("SUBSTART CARDSTART POST SUBREQ WHY", &s->first_subunit_start, 5u);
#if KUI_VMU_READONLY_PROOF
            retail_display_values("VIRTUAL REPLIES INFO MEDIA READ",&s->proof_replies,4u);
            retail_display_values("PATCHES CONFLICTS REFUSED BANKBAD",
                (uint32_t[4]){s->proof_parents,s->proof_conflicts,s->proof_refused,s->proof_bank_fail},4u);
            retail_display_values("FLAGS LAST REQUEST RESPONSE RX",
                (uint32_t[4]){s->proof_flags,s->proof_last_request,s->proof_last_response,s->proof_last_rx},4u);
            retail_display_values("BLOCK BYTES SD COMMITS",&s->proof_last_block,3u);
            retail_display_values("MODE PREPARED COMPLETED",&s->proof_mode,3u);
            retail_display_line(KUI_VMU_READONLY_PROOF==3?"RAM SAVES LOST ON POWER OFF / SD UNCHANGED":
                KUI_VMU_READONLY_PROOF==2?
                "FIXED SD IMAGE / NO WRITES OR PERSISTENCE":"SYNTHETIC EMPTY CARD / NO PERSISTENCE");
            retail_display_pause(600u);
            continue;
#endif
            if(s->stop_reason==KUI_VMU_OBSERVE_UNSAFE) {
                retail_display_values("FAULT CURSOR CONTROL RX HEADER",
                    (uint32_t[4]){s->fault_cursor,s->last_control,s->last_rx,s->last_header},4u);
                retail_display_values("SDK NOP WORDS",&s->nop_words,1u);
                retail_display_line("RX HEADER ZERO IF NOT A PACKET");
                retail_display_line("FINAL PENDING DMA IS NOT STARTED");
                retail_display_pause(600u);
                continue;
            }
            retail_display_line("ORDINAL HEADER PAY0 PAY1 RX");
            retail_display_line("FIRST FOUR SUBUNIT REQUESTS");
            for(unsigned i=0;i<KUI_VMU_MAPLE_OBSERVE_RECORDS;i++) report_record(&s->first[i]);
            retail_display_line("LAST FOUR SUBUNIT REQUESTS");
            for(unsigned i=0;i<KUI_VMU_MAPLE_OBSERVE_RECORDS;i++) {
                if(i<total) report_record(&s->last[(oldest+i)%KUI_VMU_MAPLE_OBSERVE_RECORDS]);
                else retail_display_line("EMPTY");
            }
        } else if(page==3u) {
#if KUI_VMU_READONLY_PROOF >= 2
            retail_display_values("SD READS BUSY FAULTS RESULT",&s->sd_reads,4u);
            retail_display_values("LBA EXPECT CRC ACTUAL CRC CID CHECKS",&s->sd_last_lba,4u);
            retail_display_values("PROOF FLAGS REQUEST RESPONSE RX",&s->proof_flags,4u);
#else
            retail_display_values("TRIGGER NONSW ENABLE NONONE", &s->trigger_last, 4u);
            retail_display_values("BUSY NONZERO ENTRY SR MASKS", &s->busy_last, 4u);
            retail_display_values("MANAGER BASE NONMAPLE",
                (uint32_t[2]){s->manager_last, s->manager_other}, 2u);
#endif
            retail_display_values("CMP SR MASKS MAIN-A INFO HDR RX", &s->completion_sr_last, 4u);
            retail_display_values("SUB REQUEST RESPONSE RX ORDINAL", &s->subunit_request, 4u);
            retail_display_values("CTXBAD FLAGS MDSTAR CALLER PR", &s->completion_bad_context, 4u);
            retail_display_line(KUI_VMU_READONLY_PROOF==3?"VOLATILE PROOF STOPPED / PHOTO SCREEN":
                KUI_VMU_READONLY_PROOF?
                "READ-ONLY PROOF STOPPED / PHOTO SCREEN":"FINAL PENDING DMA IS NOT STARTED");
        } else {
            retail_display_values("RAM PHASES COMMITS BLOCKS READS",&s->ram_write_phases,4u);
            retail_display_values("RAM REFUSED FULL BLOCK FAULTS",&s->ram_refused,4u);
            retail_display_values("CHAIN PREP SWITCH RESTORE BYTES",&s->chain_prepared,4u);
            retail_display_values("CHAIN PHYSICAL CRC FAULTS STATE",&s->chain_physical,4u);
            retail_display_line("RAM SAVES LOST ON POWER OFF / SD UNCHANGED");
        }
        if(page!=2u && page!=3u) retail_display_line(KUI_VMU_READONLY_PROOF==3?
            "PHOTO ALL FIVE PAGES THEN POWER OFF":"PHOTO ALL FOUR PAGES THEN POWER OFF");
        retail_display_pause(600u);
    }
#endif
}

static void record_subunit(uint32_t cursor,uint32_t header,uint32_t rx,unsigned words) {
    struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    struct kui_vmu_maple_observe_record record={s->descriptors,header,0,0,rx};
    uint32_t n=s->subunit_requests;
    /* Diagnostic records contain only function/location. Mode 3 separately
     * captures BLOCKWRITE bytes into the exclusive private request snapshot. */
    if(words) record.payload0=read_word((cursor+12u)|UINT32_C(0xa0000000));
    if(words>1u) record.payload1=read_word((cursor+16u)|UINT32_C(0xa0000000));
    if(n<KUI_VMU_MAPLE_OBSERVE_RECORDS) s->first[n]=record;
    s->last[n%KUI_VMU_MAPLE_OBSERVE_RECORDS]=record;
    increment(&s->subunit_requests);
    if(!s->first_subunit_start) s->first_subunit_start=s->starts;
    /* Discovery can happen immediately with a VMU already inserted. Wait
     * for actual memory-card media/read/write/sync requests before starting
     * the shorter capture window, leaving time to navigate a LOAD menu. */
    if(!s->first_card_start && words && record.payload0==UINT32_C(0x02000000) &&
       (header&255u)>=10u && (header&255u)<=13u)
        s->first_card_start=s->starts;
}

void kui_vmu_maple_observer_visit(uint32_t inherited_sr) {
    struct kui_vmu_maple_observe_snapshot *s = &kui_vmu_maple_observer_snapshot;
    uint32_t source, cursor;
    unsigned count = 0, last = 0, unsafe_schedule = 0, entries;
    if(s->initialized != 1u)
        return;
    if(STACK_BOTTOM[0] != STACK_PATTERN || STACK_BOTTOM[1] != STACK_PATTERN) {
        increment(&s->stack_fault);
        s->stop_reason=KUI_VMU_OBSERVE_STACK_FAULT;
        kui_vmu_maple_observer_report();
        return;
    }
    increment(&s->starts);
    s->trigger_last = read_word(UINT32_C(0xa05f6c10));
    if(s->trigger_last != 0u) increment(&s->trigger_other);
    s->enable_last = read_word(UINT32_C(0xa05f6c14));
    if(s->enable_last != 1u) increment(&s->enable_other);
    s->busy_last = read_word(UINT32_C(0xa05f6c18));
    if(s->busy_last != 0u) increment(&s->busy_active);
    s->entry_sr_last = inherited_sr;
    s->entry_masks |= UINT32_C(1) << ((inherited_sr >> 4) & 15u);
    {
        uint32_t manager = read_word(UINT32_C(0x8c1223a4));
        if((manager&UINT32_C(0xff000000))==UINT32_C(0x8c000000) &&
           source_span(manager&UINT32_C(0x1fffffff), UINT32_C(0x10f8)))
            s->manager_last = read_word(manager + UINT32_C(0x10f4));
        else
            s->manager_last = 0u;
        if(s->manager_last != UINT32_C(0xa05f6c00)) increment(&s->manager_other);
    }
    source = read_word(UINT32_C(0xa05f6c04));
    cursor=source;s->fault_cursor=source;
    s->last_control=0;s->last_rx=0;s->last_header=0;
    if(s->starts <= 8u)
        s->source_samples[s->starts - 1u] = source;
    if(s->source_last != source && s->starts > 1u)
        increment(&s->source_changes);
    s->source_last = source;
    if(source < s->source_min) s->source_min = source;
    if(source > s->source_max) s->source_max = source;
    if((source & 31u) || !source_span(source, 12u)) {
        increment(&s->malformed);
        unsafe_schedule = 1u;
        goto done;
    }
#if KUI_VMU_READONLY_PROOF
    if(s->completion_pending || !proof_layout(source,0u)) {
        increment(&s->proof_bank_fail);proof_stop(UINT32_C(0x10000));return;
    }
#if KUI_VMU_READONLY_PROOF == 3
    if(chain_state!=CHAIN_IDLE && chain_state!=CHAIN_RESTORED) {
        chain_fail(UINT32_C(0x200000));return;
    }
#endif
#endif
    for(entries=0;entries<256u && count<32u;entries++) {
        uint32_t control, rx, header, bytes;
        unsigned command, destination, sender, words, unit, port;
        s->fault_cursor=cursor;s->last_control=0;s->last_rx=0;s->last_header=0;
        if(!source_span(cursor, 4u)
#if KUI_VMU_READONLY_PROOF
            || cursor-source>UINT32_C(0x3000)-4u
#endif
           ) {
            increment(&s->malformed);
            unsafe_schedule = 1u;
            break;
        }
        control = read_word(cursor | UINT32_C(0xa0000000));
        s->last_control=control;
        /* The exact SDK helper at 8c0bf93c emits one-word 00000700
         * padding between repeated-port packets. It has no RX or header.
         * Other pattern/control encodings remain refused. */
        if(control==UINT32_C(0x00000700)) {
#if KUI_VMU_READONLY_PROOF
            if(cursor-source>UINT32_C(0x3000)-4u) {
                increment(&s->malformed);unsafe_schedule=1u;break;
            }
#endif
            increment(&s->nop_words);cursor+=4u;continue;
        }
        if(control & ~UINT32_C(0x800300ff)) {
            increment(&s->unsupported);
            unsafe_schedule = 1u;
            break;
        }
        bytes = 12u + 4u * (control & 255u);
        if(!source_span(cursor, bytes)
#if KUI_VMU_READONLY_PROOF
            || cursor-source>UINT32_C(0x3000)-bytes
#endif
           ) {
            increment(&s->malformed);
            unsafe_schedule = 1u;
            break;
        }
        rx = read_word((cursor + 4u) | UINT32_C(0xa0000000));
        header = read_word((cursor + 8u) | UINT32_C(0xa0000000));
        s->last_rx=rx;s->last_header=header;
        command = header & 255u;
        destination = (header >> 8) & 255u;
        sender = (header >> 16) & 255u;
        words = header >> 24;
        port = (control >> 16) & 3u;
        unit = unit_index((uint8_t)destination);
        if(words != (control & 255u) || (destination >> 6) != port
            || sender != port << 6 || unit == 6u) {
            increment(&s->malformed);
            unsafe_schedule = 1u;
            break;
        }
        increment(&s->descriptors);
        increment(&s->ports[port]);
        increment(&s->units[unit]);
        if(command < 16u)
            increment(&s->commands[command]);
        if(command == 1u && unit == 0u)
            increment(&s->controller_seen);
        if(rx < s->rx_min) s->rx_min = rx;
        if(rx > s->rx_max) s->rx_max = rx;
        if(!response_span(rx)) {
            increment(&s->rx_bad);
            unsafe_schedule = 1u;
        }
#if KUI_VMU_READONLY_PROOF
        {
            unsigned bank=source==UINT32_C(0x0c0ec0a0)?0u:1u;
            uint32_t expected=UINT32_C(0x0c0e0080)+bank*UINT32_C(0x6000)+(port*6u+unit)*1024u;
            if(rx!=expected) {
                increment(&s->proof_bank_fail);s->proof_flags|=UINT32_C(0x20000);
                unsafe_schedule=1u;
            }
            proof_frames[count].header=header;
            proof_frames[count].payload0=proof_frames[count].payload1=0u;
            proof_frames[count].write_payload=NULL;
            /* Snapshot metadata before MDST; every retained MAIN controller
             * GETCOND needs its original function word in the filtered chain. */
            if(destination==1u || unit==0u) {
                if(words) proof_frames[count].payload0=read_word((cursor+12u)|UINT32_C(0xa0000000));
                if(words>1u) proof_frames[count].payload1=read_word((cursor+16u)|UINT32_C(0xa0000000));
            }
#if KUI_VMU_READONLY_PROOF == 3
            captured_chain[count].control=control;captured_chain[count].rx=rx;
            captured_chain[count].header=header;
            captured_chain[count].payload0=proof_frames[count].payload0;
            if(destination==1u && command==12u && words==34u) {
                for(unsigned i=0;i<32u;i++)
                    write_payloads[count][i]=read_word((cursor+20u+i*4u)|UINT32_C(0xa0000000));
                proof_frames[count].write_payload=(const uint8_t *)write_payloads[count];
            }
#endif
            proof_frames[count].rx_physical=rx;proof_frames[count].rx_capacity=1024u;
            proof_frames[count].rx_bytes=proof_response(rx);
        }
#endif
        s->source_fingerprint = (s->source_fingerprint ^ control) * UINT32_C(16777619);
        s->source_fingerprint = (s->source_fingerprint ^ rx) * UINT32_C(16777619);
        s->source_fingerprint = (s->source_fingerprint ^ header) * UINT32_C(16777619);
        s->last_control = control;
        s->last_rx = rx;
        s->last_header = header;
        if(!unsafe_schedule) {
            pending[count].rx=rx;pending[count].header=header;pending[count].ordinal=s->descriptors;
        }
        if(unit && !unsafe_schedule) record_subunit(cursor,header,rx,words);
        ++count;
        cursor += bytes;
        /* Until live ownership is established, never start a DMA chain
         * which might read our allocator header/worker as descriptors or
         * receive over it. A bounded parse failure also leaves later RX
         * addresses unproved. Report before replaying the original MDST. */
        if(unsafe_schedule) {
            last = !!(control & UINT32_C(0x80000000));
            break;
        }
        if(control & UINT32_C(0x80000000)) {
            last = 1u;
            break;
        }
    }
    if(count > s->descriptors_max) s->descriptors_max = count;
    if(!last) {
        increment(&s->incomplete);
        unsafe_schedule = 1u;
    }
done:
    if(unsafe_schedule) s->stop_reason=KUI_VMU_OBSERVE_UNSAFE;
    else if(s->starts>=s->stop_after) s->stop_reason=KUI_VMU_OBSERVE_HARD_LIMIT;
    else if(s->first_card_start && s->starts-s->first_card_start>=s->post_after)
        s->stop_reason=KUI_VMU_OBSERVE_CARD_WINDOW;
    if(s->stop_reason)
        kui_vmu_maple_observer_report();
    else {
#if KUI_VMU_READONLY_PROOF
        if(kui_vmu_readonly_overlay_prepare(&overlay,proof_frames,count,source,cursor-source,
            PROTECTED_BEGIN,UINT32_C(0x10020))) {
            proof_stop(0u);return;
        }
        proof_stats();
#endif
        /* Publish only a complete schedule that will really reach MDST.
         * The final diagnostic schedule is counted but never published. */
        if(s->completion_pending) increment(&s->completion_overwritten);
        pending_source=source;s->completion_pending=count;
#if KUI_VMU_READONLY_PROOF == 3
        uint32_t physical=chain_physical();
        if(!physical || kui_vmu_volatile_chain_build(&issued_plan,captured_chain,count,
            source,cursor-source,issued_chain,sizeof(issued_chain),physical) ||
            kui_vmu_volatile_chain_verify(&issued_plan,issued_chain,sizeof(issued_chain))) {
            chain_fail(UINT32_C(0x200000));return;
        }
        original_hash=original_fingerprint(source,cursor-source);
        increment(&s->chain_prepared);s->chain_bytes=issued_plan.output_bytes;
        s->chain_physical=physical;s->chain_crc=issued_plan.crc32;chain_status(CHAIN_PREPARED);
        /* The authored assembly hook alone replays MDST exactly once. Only
         * MDSTAR changes here; all SDK source bytes/selectors remain original. */
        write_word(UINT32_C(0xa05f6c04),physical);
        if(read_word(UINT32_C(0xa05f6c04))!=physical) {
            chain_fail(UINT32_C(0x200000));return;
        }
        increment(&s->chain_switched);chain_status(CHAIN_ISSUED);
#endif
    }
}

void kui_vmu_maple_observer_complete(uint32_t inherited_sr,uint32_t inherited_pr) {
    struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    uint32_t flags=0,busy,source,manager,lock;
    if(s->initialized!=1u) return;
    increment(&s->completion_calls);
    s->completion_sr_last=inherited_sr;
    s->completion_masks|=UINT32_C(1)<<((inherited_sr>>4)&15u);
    s->completion_pr_last=inherited_pr;
    busy=read_word(UINT32_C(0xa05f6c18));
    source=read_word(UINT32_C(0xa05f6c04));s->completion_source=source;
    if(((inherited_sr>>4)&15u)!=15u) {increment(&s->completion_bad_mask);flags|=8u;}
    if(busy) {increment(&s->completion_busy);flags|=4u;}
    if(inherited_pr!=KUI_VMU_MAPLE_OBSERVE_COMPLETION_PR) flags|=1u;
#if KUI_VMU_READONLY_PROOF == 3
    if(s->completion_pending && source!=issued_plan.output_physical) flags|=2u;
    if(chain_state!=CHAIN_ISSUED) flags|=128u;
#else
    if(s->completion_pending && source!=pending_source) flags|=2u;
#endif
    /* Dynamic SDK words are cached P1 state; only its TAS lock byte is P2.
     * Exact ownership and software-triggered idle DMA are required before
     * reading any previously validated RX header. IMASK cannot stop a
     * hardware-triggered DMA from modifying a buffer underneath this hook. */
    manager=read_word(UINT32_C(0x8c1223a4));
    lock=read_word(UINT32_C(0x8c0c0964));
    if(manager!=UINT32_C(0x8c1212a8) || lock!=UINT32_C(0xac0c0968)) flags|=16u;
    else if(read_word(manager+UINT32_C(0x0fc0))!=0u ||
            read_word(manager+UINT32_C(0x10f4))!=UINT32_C(0xa05f6c00) ||
            read_byte(lock)!=0x80u) flags|=16u;
    if(read_word(UINT32_C(0xa05f6c10))!=0u ||
       read_word(UINT32_C(0xa05f6c14))!=1u) flags|=32u;
    if(STACK_BOTTOM[0]!=STACK_PATTERN || STACK_BOTTOM[1]!=STACK_PATTERN ||
       s->completion_pending>32u) flags|=64u;
    s->completion_guard_flags|=flags;
    if(flags&UINT32_C(0x73)) increment(&s->completion_bad_context);
    if(!s->completion_pending) {
        increment(&s->completion_orphan);
#if KUI_VMU_READONLY_PROOF
        proof_stop(UINT32_C(0x100000));
#endif
        return;
    }
    if(flags) {
#if KUI_VMU_READONLY_PROOF
        proof_stop(UINT32_C(0x80000));
#endif
        return;
    }
#if KUI_VMU_READONLY_PROOF
    if(!proof_layout(pending_source,1u)) {
        increment(&s->proof_bank_fail);proof_stop(UINT32_C(0x10000));return;
    }
#if KUI_VMU_READONLY_PROOF == 3
    if(issued_plan.source_physical!=pending_source ||
       issued_plan.frame_count!=s->completion_pending ||
       issued_plan.output_physical!=chain_physical() ||
       kui_vmu_volatile_chain_verify(&issued_plan,issued_chain,sizeof(issued_chain)) ||
       original_fingerprint(pending_source,issued_plan.source_bytes)!=original_hash) {
        chain_fail(UINT32_C(0x200000));return;
    }
    /* Validate the entire physical result before any RAM write phase, commit
     * or virtual reply. A1 slots are untouched by hardware and are not read. */
    for(unsigned i=0;i<s->completion_pending;i++)
        if(!physical_response_valid(&pending[i])) {
            chain_fail(UINT32_C(0x400000));return;
        }
    /* Restore the owner's original DMA address before its SDK consumer can
     * resume. DMA is proved idle above, and all source ownership guards hold. */
    write_word(UINT32_C(0xa05f6c04),pending_source);
    if(read_word(UINT32_C(0xa05f6c04))!=pending_source) {
        chain_fail(UINT32_C(0x200000));return;
    }
    increment(&s->chain_restored);chain_status(CHAIN_RESTORED);
#endif
    if(kui_vmu_readonly_overlay_complete(&overlay)) {proof_stop(0u);return;}
    proof_stats();
#endif
    for(unsigned i=0;i<s->completion_pending;i++) {
        uint32_t header=read_word(pending[i].rx|UINT32_C(0xa0000000));
        unsigned address=(pending[i].header>>8)&255u;
        increment(&s->completion_rx_headers);
        if(address==0x20u && (pending[i].header&255u)==1u) {
            s->main_a_response=header;s->main_a_rx=pending[i].rx;
        }
        if((address&0x3fu)!=0x20u) {
            s->subunit_request=pending[i].header;s->subunit_response=header;
            s->subunit_rx=pending[i].rx;s->subunit_ordinal=pending[i].ordinal;
        }
    }
    s->completion_pending=0;increment(&s->completion_matched);
}
