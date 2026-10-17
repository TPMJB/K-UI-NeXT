/* SPDX-License-Identifier: GPL-3.0-only */
/* Read-only observation of the admitted game's pre-MDST schedule. */
#include "kui/vmu_maple_observe.h"
#include "retail_display.h"

#define PHYSICAL_BASE UINT32_C(0x0c000000)
#define WORKER_BEGIN UINT32_C(0x0cfd0000)
#define PROTECTED_BEGIN (WORKER_BEGIN - UINT32_C(32))
#define RAM_END UINT32_C(0x0d000000)
#define STACK_PATTERN UINT32_C(0x564d5531)

struct kui_vmu_maple_observe_snapshot kui_vmu_maple_observer_snapshot;
static struct retail_display_state saved_display;

#ifdef KUI_VMU_MAPLE_OBSERVE_HOST_TEST
static uint32_t host_stack[KUI_VMU_MAPLE_OBSERVE_STACK_BYTES / 4u];
#define STACK_BOTTOM host_stack
#define STACK_TOP (host_stack + sizeof(host_stack) / sizeof(host_stack[0]))
static uint32_t read_word(uint32_t address) {
    return kui_vmu_maple_observer_test_read(address);
}
#else
extern uint32_t stack_bottom[] __asm__("__vmu_observe_stack_bottom");
extern uint32_t stack_top[] __asm__("__vmu_observe_stack_top");
#define STACK_BOTTOM stack_bottom
#define STACK_TOP stack_top
static uint32_t read_word(uint32_t address) {
    return *(volatile const uint32_t *)(uintptr_t)address;
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

int kui_vmu_maple_observer_init(const struct kui_vmu_maple_observe_config *config) {
    uint32_t *words = (uint32_t *)(void *)&kui_vmu_maple_observer_snapshot;
    uint32_t *stack;
    unsigned i;
    if(!config || config->magic != KUI_VMU_MAPLE_OBSERVE_MAGIC
        || config->version != KUI_VMU_MAPLE_OBSERVE_VERSION
        || config->bytes != sizeof(*config) || !config->stop_after
        || config->stop_after > UINT32_C(1000000))
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
    kui_vmu_maple_observer_snapshot.source_min = UINT32_MAX;
    kui_vmu_maple_observer_snapshot.rx_min = UINT32_MAX;
    kui_vmu_maple_observer_snapshot.source_fingerprint = UINT32_C(2166136261);
    kui_vmu_maple_observer_snapshot.initialized = 1u;
    return 0;
}

void kui_vmu_maple_observer_report(void) {
#ifdef KUI_VMU_MAPLE_OBSERVE_HOST_TEST
    kui_vmu_maple_observer_snapshot.initialized = 2u;
    kui_vmu_maple_observer_test_stop();
#else
    const struct kui_vmu_maple_observe_snapshot *s = &kui_vmu_maple_observer_snapshot;
    uint32_t page;
    for(page = 0;;page = (page + 1u) % 4u) {
        retail_display_restore(&saved_display);
        retail_display_line("TOY MAPLE OBSERVER - READ ONLY");
        retail_display_values("PAGE STARTS FRAMES BAD",
            (uint32_t[4]){page, s->starts, s->descriptors, s->malformed}, 4u);
        if(page == 0u) {
            retail_display_values("SOURCE MIN MAX LAST CHANGES", &s->source_min, 4u);
            retail_display_values("MAX STOP STACK NO LAST", &s->descriptors_max, 4u);
            retail_display_values("PORT A B C D", s->ports, 4u);
            retail_display_values("RX MIN MAX BAD CONTROLLER", &s->rx_min, 4u);
        } else if(page == 1u) {
            retail_display_values("COMMAND 0 1 2 3", s->commands, 4u);
            retail_display_values("COMMAND 4 5 6 7", s->commands + 4u, 4u);
            retail_display_values("COMMAND 8 9 A B", s->commands + 8u, 4u);
            retail_display_values("COMMAND C D E F", s->commands + 12u, 4u);
        } else if(page == 2u) {
            retail_display_values("MAIN UNIT1 UNIT2 UNIT3", s->units, 4u);
            retail_display_values("UNIT4 UNIT5 UNSUPPORTED",
                (uint32_t[3]){s->units[4], s->units[5], s->unsupported}, 3u);
            retail_display_values("SOURCE FIRST FOUR", s->source_samples, 4u);
            retail_display_values("HASH CONTROL RX HEADER", &s->source_fingerprint, 4u);
        } else {
            retail_display_values("TRIGGER NONSW ENABLE NONONE", &s->trigger_last, 4u);
            retail_display_values("BUSY NONZERO ENTRY SR MASKS", &s->busy_last, 4u);
            retail_display_values("MANAGER BASE NONMAPLE",
                (uint32_t[2]){s->manager_last, s->manager_other}, 2u);
            retail_display_line("FINAL PENDING DMA IS NOT STARTED");
        }
        retail_display_line("PHOTO ALL FOUR PAGES THEN POWER OFF");
        retail_display_pause(600u);
    }
#endif
}

void kui_vmu_maple_observer_visit(uint32_t inherited_sr) {
    struct kui_vmu_maple_observe_snapshot *s = &kui_vmu_maple_observer_snapshot;
    uint32_t source, cursor;
    unsigned count = 0, last = 0, unsafe_schedule = 0;
    if(s->initialized != 1u)
        return;
    if(STACK_BOTTOM[0] != STACK_PATTERN || STACK_BOTTOM[1] != STACK_PATTERN) {
        increment(&s->stack_fault);
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
        uint32_t manager = read_word(UINT32_C(0xac1223a4)) & UINT32_C(0x1fffffff);
        if(source_span(manager, UINT32_C(0x10f8)))
            s->manager_last = read_word((manager + UINT32_C(0x10f4)) | UINT32_C(0xa0000000));
        else
            s->manager_last = 0u;
        if(s->manager_last != UINT32_C(0xa05f6c00)) increment(&s->manager_other);
    }
    source = read_word(UINT32_C(0xa05f6c04));
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
    cursor = source;
    for(count = 0; count < 32u; ++count) {
        uint32_t control, rx, header, bytes;
        unsigned command, destination, sender, words, unit, port;
        if(!source_span(cursor, 12u)) {
            increment(&s->malformed);
            unsafe_schedule = 1u;
            break;
        }
        control = read_word(cursor | UINT32_C(0xa0000000));
        if(control & ~UINT32_C(0x800300ff)) {
            increment(&s->unsupported);
            unsafe_schedule = 1u;
            break;
        }
        bytes = 12u + 4u * (control & 255u);
        if(!source_span(cursor, bytes)) {
            increment(&s->malformed);
            unsafe_schedule = 1u;
            break;
        }
        rx = read_word((cursor + 4u) | UINT32_C(0xa0000000));
        header = read_word((cursor + 8u) | UINT32_C(0xa0000000));
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
        s->source_fingerprint = (s->source_fingerprint ^ control) * UINT32_C(16777619);
        s->source_fingerprint = (s->source_fingerprint ^ rx) * UINT32_C(16777619);
        s->source_fingerprint = (s->source_fingerprint ^ header) * UINT32_C(16777619);
        s->last_control = control;
        s->last_rx = rx;
        s->last_header = header;
        cursor += bytes;
        /* Until live ownership is established, never start a DMA chain
         * which might read our allocator header/worker as descriptors or
         * receive over it. A bounded parse failure also leaves later RX
         * addresses unproved. Report before replaying the original MDST. */
        if(unsafe_schedule) {
            last = !!(control & UINT32_C(0x80000000));
            ++count;
            break;
        }
        if(control & UINT32_C(0x80000000)) {
            last = 1u;
            ++count;
            break;
        }
    }
    if(count > s->descriptors_max) s->descriptors_max = count;
    if(!last) {
        increment(&s->incomplete);
        unsafe_schedule = 1u;
    }
done:
    if(unsafe_schedule || s->starts >= s->stop_after)
        kui_vmu_maple_observer_report();
}
