/* SPDX-License-Identifier: GPL-3.0-only */
/* Read-only observation of the admitted game's pre-MDST schedule. */
#include "kui/vmu_maple_observe.h"
#if KUI_VMU_READONLY_PROOF
#include "kui/vmu_readonly.h"
#include "kui/vmu_readonly_overlay.h"
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
static struct kui_vmu_readonly blank_card;
static struct kui_vmu_readonly_overlay overlay;
static struct kui_vmu_readonly_overlay_frame proof_frames[32];
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
static int blank_packet(void *context,const uint8_t *request,size_t bytes,
    uint8_t *response,size_t capacity,size_t *response_bytes) {
    return kui_vmu_readonly_handle(context,request,bytes,response,capacity,response_bytes);
}
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
    s->proof_last_rx=p->last_rx;s->proof_last_block=blank_card.protocol.read_block;
    s->proof_bytes=p->devinfo*116u+p->allinfo*196u+p->media*32u+p->reads*524u;
    s->proof_commit_attempts=blank_card.commit_attempts;
}
static void proof_stop(uint32_t flags) {
    struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    s->proof_flags|=flags;proof_stats();
    s->stop_reason=KUI_VMU_OBSERVE_PROOF_REFUSED;
    kui_vmu_maple_observer_report();
}
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
    kui_vmu_maple_observer_snapshot.initialized = 1u;
    pending_source=0;
#if KUI_VMU_READONLY_PROOF
    kui_vmu_maple_observer_snapshot.proof_mode=1u;
    if(kui_vmu_readonly_init(&blank_card) ||
       kui_vmu_readonly_overlay_init(&overlay,blank_packet,&blank_card)) return -1;
#endif
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
    for(page = 0;;page = (page + 1u) % 4u) {
        retail_display_restore(&saved_display);
        retail_display_line(KUI_VMU_READONLY_PROOF?
            "VIRTUAL VMU - READ ONLY BLANK CARD":"TOY MAPLE OBSERVER - READ ONLY");
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
            retail_display_values("BLOCK BYTES COMMITS",&s->proof_last_block,3u);
            retail_display_values("MODE PREPARED COMPLETED",&s->proof_mode,3u);
            retail_display_line("SYNTHETIC EMPTY CARD / NO PERSISTENCE");
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
        } else {
            retail_display_values("TRIGGER NONSW ENABLE NONONE", &s->trigger_last, 4u);
            retail_display_values("BUSY NONZERO ENTRY SR MASKS", &s->busy_last, 4u);
            retail_display_values("MANAGER BASE NONMAPLE",
                (uint32_t[2]){s->manager_last, s->manager_other}, 2u);
            retail_display_values("CMP SR MASKS MAIN-A INFO HDR RX", &s->completion_sr_last, 4u);
            retail_display_values("SUB REQUEST RESPONSE RX ORDINAL", &s->subunit_request, 4u);
            retail_display_values("CTXBAD FLAGS MDSTAR CALLER PR", &s->completion_bad_context, 4u);
            retail_display_line(KUI_VMU_READONLY_PROOF?
                "READ-ONLY PROOF STOPPED / PHOTO SCREEN":"FINAL PENDING DMA IS NOT STARTED");
        }
        if(page!=2u && page!=3u) retail_display_line("PHOTO ALL FOUR PAGES THEN POWER OFF");
        retail_display_pause(600u);
    }
#endif
}

static void record_subunit(uint32_t cursor,uint32_t header,uint32_t rx,unsigned words) {
    struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    struct kui_vmu_maple_observe_record record={s->descriptors,header,0,0,rx};
    uint32_t n=s->subunit_requests;
    /* Only the function and location words are sampled. In particular,
     * the 128-byte BLOCKWRITE save payload is never read by this observer. */
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
            /* Snapshot only A1 function/location or A-controller function;
             * never copy save data or later reread the live source chain. */
            if(destination==1u || destination==0x20u) {
                if(words) proof_frames[count].payload0=read_word((cursor+12u)|UINT32_C(0xa0000000));
                if(words>1u) proof_frames[count].payload1=read_word((cursor+16u)|UINT32_C(0xa0000000));
            }
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
    if(s->completion_pending && source!=pending_source) flags|=2u;
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
