/* SPDX-License-Identifier: GPL-3.0-only */
/* Isolated physical VMU read cadence observations. No storage destination. */
#include "kui/core.h"
#include "kui/shell_font.h"
#include "kui/vmu_read_bounded.h"
#include <kos.h>
#include <dc/maple/controller.h>
#include <dc/vmufs.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_IRQ | INIT_CONTROLLER | INIT_VMU | INIT_NO_DCLOAD | INIT_QUIET);
#ifndef KUI_BUILD_ID
#define KUI_BUILD_ID "local"
#endif
#define PROBE_CADENCES 3u
#define PROBE_BLOCKS 3u
#define PROBE_ROUNDS 32u
#define PROBE_READS (PROBE_BLOCKS*PROBE_ROUNDS)
#define TABLE_ROWS 10u
_Static_assert(sizeof(struct kui_vmu_read_bounded_diagnostic)==64u,"bounded diagnostic ABI");
static const uint16_t probe_blocks[PROBE_BLOCKS]={0u,254u,255u};
static const unsigned probe_gaps[PROBE_CADENCES]={0u,20u,100u};
enum probe_fault {PROBE_FAULT_NONE,PROBE_FAULT_DEVICE,PROBE_FAULT_LOCK,
    PROBE_FAULT_READ,PROBE_FAULT_UNLOCK};
enum {PROBE_RESULT_GUARD=-1000};
enum probe_guard_bit {GUARD_MISSING=1u,GUARD_REPLACED=2u,GUARD_INVALID=4u,
    GUARD_FUNCTIONS=8u,GUARD_MEMCARD=16u,GUARD_NAME=32u};
struct probe_difference {unsigned index,before,after,mask;};
struct probe_diag_snapshot {
    bool valid;
    unsigned round,block;
    uint32_t at_ms,duration_ms;
    struct kui_vmu_read_bounded_diagnostic read;
};
struct probe_stats {
    unsigned attempts,success,read_fail,guard_fail,different,transitions;
    unsigned bytes,bits,syndrome;
    uint32_t baseline_crc,last_crc;
    bool have_difference,have_previous;
    unsigned first_round,first_bytes,first_bits,first_syndrome;
    uint32_t first_at_ms;
    struct probe_difference first[2];
    struct probe_diag_snapshot first_changed;
};
struct probe_record {
    unsigned round,block;
    uint32_t at_ms,duration_ms,crc;
    int result;
    unsigned different_bytes,bits,syndrome;
};
struct probe_page {
    unsigned gap_ms,rounds,records;
    struct probe_stats block[PROBE_BLOCKS];
    struct probe_diag_snapshot first,last;
};
static struct probe_page pages[PROBE_CADENCES];
static struct probe_record records[PROBE_CADENCES][PROBE_READS];
static _Alignas(32) uint8_t baseline[PROBE_BLOCKS][512],previous[PROBE_BLOCKS][512],sample[512];
static bool baseline_ready[PROBE_BLOCKS];
static uint32_t baseline_crc[PROBE_BLOCKS];
static unsigned selected_slot,page_index,table_offset;
static bool table_view,detail_view,launch_attempted,complete,source_locked;
static maple_device_t *source_device;
static uint32_t source_functions;
static char source_name[31];
static uint64_t started_at;
static enum probe_fault fault;
static unsigned fault_page,fault_round,fault_block,guard_mask;
static int fault_result;
static uintptr_t guard_actual_device;
static uint32_t guard_actual_functions;
static unsigned guard_valid,guard_name_index,guard_expected_name,guard_actual_name;

static void render(void);
static uint32_t elapsed(uint64_t now) {
    uint64_t value=now-started_at;
    return value>UINT32_MAX?UINT32_MAX:(uint32_t)value;
}
static void set_fault(enum probe_fault kind,unsigned cadence,unsigned round,unsigned block,int result) {
    if(fault) return;
    fault=kind;fault_page=cadence;fault_round=round;fault_block=block;fault_result=result;
}
static bool source_same(void) {
    maple_device_t *now=maple_enum_dev((int)(selected_slot/2u),(int)(selected_slot%2u+1u));
    unsigned mask=0u,valid=0u,name_index=30u,expected_name=0u,actual_name=0u;
    uint32_t functions=0u;
    if(!now) mask|=GUARD_MISSING;
    else {
        if(now!=source_device) mask|=GUARD_REPLACED;
        valid=now->valid;functions=now->info.functions;
        if(!valid) mask|=GUARD_INVALID;
        if(functions!=source_functions) mask|=GUARD_FUNCTIONS;
        if(!(functions&MAPLE_FUNC_MEMCARD)) mask|=GUARD_MEMCARD;
        for(unsigned i=0;i<30u;i++) if((uint8_t)now->info.product_name[i]!=(uint8_t)source_name[i]) {
            mask|=GUARD_NAME;name_index=i;expected_name=(uint8_t)source_name[i];
            actual_name=(uint8_t)now->info.product_name[i];break;
        }
    }
    if(mask && !guard_mask) {
        guard_mask=mask;guard_actual_device=(uintptr_t)now;guard_actual_functions=functions;
        guard_valid=valid;guard_name_index=name_index;
        guard_expected_name=expected_name;guard_actual_name=actual_name;
    }
    return !mask;
}
static void freeze_diagnostic(struct probe_page *page,const struct probe_record *record,
    const struct kui_vmu_read_bounded_diagnostic *read) {
    struct probe_diag_snapshot snap={true,record->round,record->block,
        record->at_ms,record->duration_ms,*read};
    if(!page->first.valid) page->first=snap;
    page->last=snap;
}
static void compare_sample(unsigned index,struct probe_stats *stats,struct probe_record *record,
    const struct probe_diag_snapshot *diagnostic) {
    struct probe_difference first[2]={{512u,0u,0u,0u},{512u,0u,0u,0u}};
    unsigned bytes=0u,bits=0u,syndrome=0u;
    for(unsigned i=0;i<512u;i++) {
        unsigned mask=baseline[index][i]^sample[i];
        if(!mask) continue;
        if(bytes<2u) first[bytes]=(struct probe_difference){i,baseline[index][i],sample[i],mask};
        ++bytes;syndrome^=mask;
        for(unsigned value=mask;value;value>>=1u) bits+=value&1u;
    }
    record->different_bytes=bytes;record->bits=bits;record->syndrome=syndrome;
    if(!bytes) return;
    ++stats->different;stats->bytes+=bytes;stats->bits+=bits;stats->syndrome^=syndrome;
    if(!stats->have_difference) {
        stats->have_difference=true;stats->first_round=record->round;stats->first_at_ms=record->at_ms;
        stats->first_bytes=bytes;stats->first_bits=bits;stats->first_syndrome=syndrome;
        memcpy(stats->first,first,sizeof(first));stats->first_changed=*diagnostic;
    }
}
/* One launch, one pinned device, one filesystem lock. A reader refusal is
 * terminal: an in-flight frame quarantined by the adapter is never retried. */
static bool probe_run(void) {
    if(launch_attempted) return false;
    launch_attempted=true;started_at=timer_ms_gettime64();
    for(unsigned c=0;c<PROBE_CADENCES;c++) pages[c].gap_ms=probe_gaps[c];
    source_device=maple_enum_dev((int)(selected_slot/2u),(int)(selected_slot%2u+1u));
    if(!source_device || !source_device->valid || !(source_device->info.functions&MAPLE_FUNC_MEMCARD)) {
        set_fault(PROBE_FAULT_DEVICE,0u,0u,0u,0);goto finished;
    }
    source_functions=source_device->info.functions;
    memcpy(source_name,source_device->info.product_name,30u);source_name[30]=0;
    int locked=vmufs_mutex_lock();
    if(locked) {set_fault(PROBE_FAULT_LOCK,0u,0u,0u,locked);goto finished;}
    source_locked=true;
    for(unsigned c=0;c<PROBE_CADENCES;c++) {
        struct probe_page *page=&pages[c];page_index=c;render();
        for(unsigned round=0;round<PROBE_ROUNDS;round++) {
            for(unsigned b=0;b<PROBE_BLOCKS;b++) {
                struct probe_stats *stats=&page->block[b];
                if(page->records && page->gap_ms) thd_sleep(page->gap_ms);
                if(!source_same()) {
                    ++stats->guard_fail;set_fault(PROBE_FAULT_DEVICE,c,round+1u,probe_blocks[b],0);goto finished;
                }
                struct probe_record *record=&records[c][page->records++];
                record->round=round+1u;record->block=probe_blocks[b];
                record->at_ms=elapsed(timer_ms_gettime64());++stats->attempts;
                record->result=kui_vmu_read_bounded(source_device,probe_blocks[b],sample);
                record->duration_ms=elapsed(timer_ms_gettime64())-record->at_ms;
                struct kui_vmu_read_bounded_diagnostic read=*kui_vmu_read_bounded_last();
                freeze_diagnostic(page,record,&read);
                if(record->result) {
                    ++stats->read_fail;set_fault(PROBE_FAULT_READ,c,round+1u,probe_blocks[b],record->result);goto finished;
                }
                if(!source_same()) {
                    record->result=PROBE_RESULT_GUARD;++stats->guard_fail;
                    set_fault(PROBE_FAULT_DEVICE,c,round+1u,probe_blocks[b],0);goto finished;
                }
                record->crc=kui_crc32(0,sample,512u);stats->last_crc=record->crc;++stats->success;
                /* Cadence-local transitions do not depend on the global first
                 * baseline. An unstable initial sample cannot make a later
                 * stable cadence appear to keep changing within that cadence. */
                if(stats->have_previous && memcmp(previous[b],sample,512u)) ++stats->transitions;
                memcpy(previous[b],sample,512u);stats->have_previous=true;
                if(!baseline_ready[b]) {
                    memcpy(baseline[b],sample,512u);baseline_crc[b]=record->crc;baseline_ready[b]=true;
                }
                else compare_sample(b,stats,record,&page->last);
                stats->baseline_crc=baseline_crc[b];
            }
            page->rounds=round+1u;
            if(!((round+1u)%8u)) render();
        }
    }
finished:
    if(source_locked) {
        int unlocked=vmufs_mutex_unlock();source_locked=false;
        if(unlocked) set_fault(PROBE_FAULT_UNLOCK,page_index,pages[page_index].rounds,0u,unlocked);
    }
    complete=true;page_index=fault?fault_page:0u;table_offset=0u;return !fault;
}
static void draw(unsigned row,uint16_t color,const char *format,...) {
    char text[100];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
    kui_shell_font_draw(vram_s,24,126+(int)row*16,color,text,false);
}
static void draw_diagnostic(unsigned row,const char *label,const struct probe_diag_snapshot *s) {
    if(!s->valid) {draw(row,0xc618,"%s: no transaction",label);return;}
    const struct kui_vmu_read_bounded_diagnostic *d=&s->read;
    draw(row,0xffff,"%s r%u B%03u t%lu +%lums stage%lu result%d type%d words%lu",label,
        s->round,s->block,(unsigned long)s->at_ms,(unsigned long)s->duration_ms,
        (unsigned long)d->stage,d->result,d->response,(unsigned long)d->data_len);
    draw(row+1u,0xc618,"FUNC %08lx/%08lx BLOCK %08lx/%08lx locks%lu state%lu q%lu",
        (unsigned long)d->expected_function,(unsigned long)d->actual_function,
        (unsigned long)d->expected_location,(unsigned long)d->actual_location,
        (unsigned long)d->lock_attempts,(unsigned long)d->frame_state,(unsigned long)d->queued);
    draw(row+2u,0xc618,"RX %08lx/%08lx COPY %08lx",(unsigned long)d->raw_crc_before,
        (unsigned long)d->raw_crc_after,(unsigned long)d->copy_crc);
}
static void render(void) {
    for(unsigned i=0;i<640u*480u;i++) vram_s[i]=0x0841;
    kui_shell_font_draw(vram_s,24,20,0xfe2d,"Physical VMU - read cadence probe",true);
    char heading[100];snprintf(heading,sizeof(heading),"Build %.12s  Source %c%u  READ ONLY / NO SD",
        KUI_BUILD_ID,'A'+selected_slot/2u,selected_slot%2u+1u);
    kui_shell_font_draw(vram_s,24,54,0xffff,heading,false);
    kui_shell_font_draw(vram_s,24,78,0xc618,"Blocks 0/254/255, 32 rounds each; requested gaps 0/20/100ms.",false);
    if(baseline_ready[1]) {
        char allocation[80];
        unsigned entry=(unsigned)baseline[1][0]|((unsigned)baseline[1][1]<<8);
        snprintf(allocation,sizeof(allocation),"FAT entry 0 (first sample): %04x  [fffc = free]",entry);
        kui_shell_font_draw(vram_s,24,102,0xc618,allocation,false);
    }
    if(!launch_attempted) {
        draw(1u,0xffff,"Choose the physical source slot, then press X once.");
        draw(3u,0xc618,"Every successful sample is compared with that block's first sample.");
        draw(5u,0xc618,"This observes cadence and bit differences; it does not diagnose the cause.");
    } else {
        const struct probe_page *page=&pages[page_index];
        draw(0u,0xfe2d,"Page %u/3  gap %ums  rounds %u/32  attempts %u/96",page_index+1u,
            page->gap_ms,page->rounds,page->records);
        if(table_view) {
            if(!page->records) draw(1u,0xc618,"No transactions in this cadence.");
            else draw(1u,0xc618,"Read table %u..%u; t/start and duration are elapsed milliseconds",table_offset+1u,
                page->records<table_offset+TABLE_ROWS?page->records:table_offset+TABLE_ROWS);
            for(unsigned i=0;i<TABLE_ROWS && table_offset+i<page->records;i++) {
                const struct probe_record *r=&records[page_index][table_offset+i];
                draw(2u+i,0xffff,"%02u r%02u B%03u t%lu +%lu crc%08lx rc%d diff%u bits%u xor%02x",
                    table_offset+i+1u,r->round,r->block,(unsigned long)r->at_ms,(unsigned long)r->duration_ms,
                    (unsigned long)r->crc,r->result,r->different_bytes,r->bits,r->syndrome);
            }
        } else if(detail_view) {
            for(unsigned b=0;b<PROBE_BLOCKS;b++) {
                const struct probe_stats *s=&page->block[b];unsigned row=1u+b*5u;
                draw(row,0xfe2d,"B%03u first change: bytes%u bits%u xor%02x; cadence transitions%u",
                    probe_blocks[b],s->first_bytes,s->first_bits,s->first_syndrome,s->transitions);
                draw_diagnostic(row+1u,"CHANGE",&s->first_changed);
                if(s->have_difference)
                    draw(row+4u,0xc618,"DIFF %u:%02x/%02x^%02x  %u:%02x/%02x^%02x (512 = none)",
                        s->first[0].index,s->first[0].before,s->first[0].after,s->first[0].mask,
                        s->first[1].index,s->first[1].before,s->first[1].after,s->first[1].mask);
            }
        } else {
            for(unsigned b=0;b<PROBE_BLOCKS;b++) {
                const struct probe_stats *s=&page->block[b];unsigned row=1u+b*3u;
                draw(row,0xffff,"B%03u ok%u fail%u guard%u diff%u trans%u %08lx/%08lx",
                    probe_blocks[b],s->success,s->read_fail,s->guard_fail,s->different,s->transitions,
                    (unsigned long)s->baseline_crc,(unsigned long)s->last_crc);
                draw(row+1u,0xc618,"Totals bytes%u bits%u xor%02x; first r%u t%lu n%u bits%u xor%02x",
                    s->bytes,s->bits,s->syndrome,s->first_round,(unsigned long)s->first_at_ms,
                    s->first_bytes,s->first_bits,s->first_syndrome);
                if(s->have_difference)
                    draw(row+2u,0xc618,"DIFF %u:%02x/%02x^%02x  %u:%02x/%02x^%02x (512 = none)",
                        s->first[0].index,s->first[0].before,s->first[0].after,s->first[0].mask,
                        s->first[1].index,s->first[1].before,s->first[1].after,s->first[1].mask);
            }
            draw_diagnostic(10u,"FIRST",&page->first);draw_diagnostic(13u,"LAST",&page->last);
        }
    }
    if(fault) {
        char message[100];snprintf(message,sizeof(message),"STOP fault%u rc%d page%u r%u B%03u guard%02x",
            (unsigned)fault,fault_result,fault_page+1u,fault_round,fault_block,guard_mask);
        kui_shell_font_draw(vram_s,24,398,0xfe2d,message,false);
    }
    kui_shell_font_draw(vram_s,24,420,0xfe2d,complete?
        "COMPLETE / STOPPED. Results retained; source fixed until restart.":launch_attempted?
        "Reading only. Keep this VMU connected; no capture is accepted as a save.":
        "Left/Right Source slot   X Start once",false);
    kui_shell_font_draw(vram_s,24,446,0xc618,complete?
        "Left/Right Cadence   X Summary/Changes/Table   Up/Down Table rows":
        "No SD access, formatting, erase, block writes or repair.",false);
    vid_waitvbl();vid_flip(-1);
}
static void probe_buttons(uint32_t pressed) {
    if(!launch_attempted) {
        if(pressed&(CONT_DPAD_LEFT|CONT_DPAD_RIGHT))
            selected_slot=(selected_slot+((pressed&CONT_DPAD_RIGHT)?1u:7u))%8u;
        if(pressed&CONT_X) (void)probe_run();
        return;
    }
    if(!complete) return;
    if(pressed&(CONT_DPAD_LEFT|CONT_DPAD_RIGHT)) {
        page_index=(page_index+((pressed&CONT_DPAD_RIGHT)?1u:PROBE_CADENCES-1u))%PROBE_CADENCES;
        table_offset=0u;
    }
    if(pressed&CONT_X) {
        if(table_view) table_view=false;
        else if(detail_view) {detail_view=false;table_view=true;}
        else detail_view=true;
        table_offset=0u;
    }
    if(table_view && (pressed&CONT_DPAD_UP))
        table_offset=table_offset>=TABLE_ROWS?table_offset-TABLE_ROWS:0u;
    if(table_view && (pressed&CONT_DPAD_DOWN) && table_offset+TABLE_ROWS<pages[page_index].records)
        table_offset+=TABLE_ROWS;
}
static uint32_t buttons(void) {
    maple_device_t *controller=maple_enum_type(0,MAPLE_FUNC_CONTROLLER);
    cont_state_t *state=controller?maple_dev_status(controller):NULL;
    return state?state->buttons:0u;
}
int main(void) {
    vid_set_mode(DM_640x480|DM_MULTIBUFFER,PM_RGB565);
    uint32_t previous=buttons();
    for(;;) {
        uint32_t now=buttons();probe_buttons(now&~previous);previous=now;
        render();thd_sleep(16u);
    }
    return 0;
}
