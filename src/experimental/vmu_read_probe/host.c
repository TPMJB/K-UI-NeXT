/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the console's actual static capture and input logic. Only display,
 * time, controller, device enumeration and the bounded read are substituted.
 * Every case gets pristine console state in a child process. */
#define _POSIX_C_SOURCE 200809L
#define main kui_vmu_read_probe_console_main
#include "main.c"
#undef main
#include <assert.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

_Static_assert(sizeof(struct kui_vmu_read_bounded_diagnostic)==64u,
               "bounded diagnostic ABI");

enum host_fault {HOST_STABLE,HOST_BITFLIP,HOST_HEADER,HOST_TIMEOUT,
                 HOST_REMOVAL,HOST_REPLACEMENT,HOST_FUNCTIONS,HOST_NAME};
static enum host_fault host_fault;
static unsigned host_flip_mode;
static unsigned host_fault_at=17u,host_reads,host_enumerations;
static unsigned host_locks,host_unlocks,host_sleeps,host_forbidden;
static unsigned host_sleep_values[288];
static uint16_t host_read_blocks[288];
static uint64_t host_read_starts[288],host_clock=100000u;
static bool host_present=true,host_mutex_locked,host_drop_on_lock;
static maple_device_t host_device,host_replacement,host_controller;
static struct kui_vmu_read_bounded_diagnostic host_diagnostic;
static uint8_t host_physical[3][512],host_original[3][512];
static uint16_t host_frame[640u*480u];
uint16_t *vram_s=host_frame;

static unsigned host_block_index(uint16_t block) {
    assert(block==0u || block==254u || block==255u);
    return block==0u?0u:block==254u?1u:2u;
}
static void host_apply_flip(uint8_t out[512]) {
    if(!host_flip_mode) out[11]^=1u;
    else {out[12]^=0x20u;out[18]^=host_flip_mode==1u?0x08u:0x20u;}
}
static void host_fixture(void) {
    host_device=(maple_device_t){.valid=1,.info={.functions=MAPLE_FUNC_MEMCARD}};
    memcpy(host_device.info.product_name,"READ PROBE HOST SOURCE",22u);
    host_replacement=host_device;
    host_controller=(maple_device_t){.valid=1,.info={.functions=MAPLE_FUNC_CONTROLLER}};
    const unsigned blocks[3]={0u,254u,255u};
    for(unsigned block=0;block<3u;block++)
        for(unsigned i=0;i<512u;i++)
            host_physical[block][i]=(uint8_t)(i*17u+blocks[block]*29u+7u);
    memcpy(host_original,host_physical,sizeof(host_original));
}
void vid_waitvbl(void) {
    if(launch_attempted && !complete) {
        assert(host_mutex_locked && source_locked);
        unsigned slot=selected_slot,reads=host_reads;
        /* Progress rendering yields in the real console. Pending navigation
         * or start input cannot select a different source during capture. */
        probe_buttons(CONT_X|CONT_DPAD_RIGHT);
        assert(selected_slot==slot && host_reads==reads);
    }
}
void vid_flip(int page) {(void)page;}
void vid_set_mode(int mode,int format) {(void)mode;(void)format;}
void kui_shell_font_draw(uint16_t *pixels,int x,int y,uint16_t color,
                         const char *text,bool large) {
    (void)pixels;(void)x;(void)y;(void)color;(void)text;(void)large;
}
uint64_t timer_ms_gettime64(void) {return host_clock;}
uint64_t timer_us_gettime64(void) {return host_clock*1000u;}
void thd_sleep(unsigned milliseconds) {
    assert(host_mutex_locked && source_locked && milliseconds>0u);
    assert(host_sleeps<sizeof(host_sleep_values)/sizeof(host_sleep_values[0]));
    host_sleep_values[host_sleeps++]=milliseconds;
    host_clock+=milliseconds;
}
maple_device_t *maple_enum_type(int index,uint32_t functions) {
    (void)index;return functions==MAPLE_FUNC_CONTROLLER?&host_controller:NULL;
}
void *maple_dev_status(maple_device_t *device) {
    static cont_state_t controller;
    assert(device==&host_controller);return &controller;
}
maple_device_t *maple_enum_dev(int port,int unit) {
    ++host_enumerations;
    assert(port==0 && unit==1);
    if(!host_present) return NULL;
    if(host_fault==HOST_REPLACEMENT && host_reads>=host_fault_at)
        return &host_replacement;
    return &host_device;
}
int vmufs_mutex_lock(void) {
    assert(!host_mutex_locked);host_mutex_locked=true;++host_locks;
    if(host_drop_on_lock) host_present=false;
    return 0;
}
int vmufs_mutex_unlock(void) {
    assert(host_mutex_locked);host_mutex_locked=false;++host_unlocks;return 0;
}
const struct kui_vmu_read_bounded_diagnostic *kui_vmu_read_bounded_last(void) {
    return &host_diagnostic;
}
int kui_vmu_read_bounded(maple_device_t *device,uint16_t block,uint8_t out[512]) {
    assert(host_mutex_locked && source_locked && device==&host_device);
    assert(host_reads<288u);
    host_read_blocks[host_reads]=block;
    host_read_starts[host_reads]=host_clock;
    ++host_reads;host_clock+=2u;
    host_diagnostic=(struct kui_vmu_read_bounded_diagnostic){
        .block=block,.stage=KUI_VMU_READ_COMPLETE,.frame_state=3u,.queued=1u,
        .lock_attempts=host_reads,.result=0,.wait_result=0,.response=8,
        .data_len=130u,.expected_function=MAPLE_FUNC_MEMCARD,
        .actual_function=MAPLE_FUNC_MEMCARD,.expected_location=(uint32_t)block<<24,
        .actual_location=(uint32_t)block<<24};
    if(host_reads==host_fault_at && (host_fault==HOST_HEADER || host_fault==HOST_TIMEOUT)) {
        host_diagnostic.stage=host_fault==HOST_HEADER?KUI_VMU_READ_LENGTH:KUI_VMU_READ_WAIT;
        host_diagnostic.result=host_fault==HOST_HEADER?MAPLE_EFAIL:MAPLE_ETIMEOUT;
        host_diagnostic.data_len=host_fault==HOST_HEADER?129u:0u;
        host_diagnostic.response=host_fault==HOST_HEADER?8:-127;
        host_diagnostic.wait_result=host_fault==HOST_HEADER?0:-1;
        return host_diagnostic.result;
    }
    memcpy(out,host_physical[host_block_index(block)],512u);
    if(host_fault==HOST_BITFLIP && host_reads==host_fault_at) host_apply_flip(out);
    host_diagnostic.raw_crc_before=kui_crc32(0,out,512u);
    host_diagnostic.raw_crc_after=host_diagnostic.raw_crc_before;
    host_diagnostic.copy_crc=host_diagnostic.raw_crc_before;
    if(host_reads==host_fault_at) {
        if(host_fault==HOST_REMOVAL) host_present=false;
        if(host_fault==HOST_FUNCTIONS) host_device.info.functions^=1u;
        if(host_fault==HOST_NAME) host_device.info.product_name[0]^=1;
    }
    return MAPLE_EOK;
}

/* Read-only/SD sentinels: a newly introduced forbidden call makes the actual
 * included console fail this suite, rather than silently mutating a fixture. */
int vmu_block_read(maple_device_t *device,uint16_t block,uint8_t *out) {
    (void)device;(void)block;(void)out;++host_forbidden;assert(!"SDK reader reached");return -1;
}
int vmu_block_write(maple_device_t *device,uint16_t block,const uint8_t *in) {
    (void)device;(void)block;(void)in;++host_forbidden;assert(!"physical write reached");return -1;
}
bool kui_sd_connect(void) {++host_forbidden;assert(!"SD connect reached");return false;}
void kui_sd_disconnect(void) {++host_forbidden;assert(!"SD disconnect reached");}
void kui_sd_set_params(unsigned transport,bool crc) {
    (void)transport;(void)crc;++host_forbidden;assert(!"SD setup reached");
}

static void host_unchanged(void) {
    assert(!memcmp(host_original,host_physical,sizeof(host_original)));
    assert(!host_forbidden && !host_mutex_locked && !source_locked);
    assert(host_locks==1u && host_unlocks==1u);
}

static const uint32_t host_expected_crc[3]={
    UINT32_C(0xc728d6bf),UINT32_C(0x29c539bd),UINT32_C(0x8ab73f00)};
static const uint32_t host_flipped_crc[3][3]={
    {UINT32_C(0xe8a2d1da),UINT32_C(0x064f3ed8),UINT32_C(0xa53d3865)},
    {UINT32_C(0x7efb5610),UINT32_C(0x9016b912),UINT32_C(0x3364bfaf)},
    {UINT32_C(0x347e60d7),UINT32_C(0xda938fd5),UINT32_C(0x79e18968)}};
static unsigned host_flip_bytes(void) {return host_flip_mode?2u:1u;}
static unsigned host_flip_syndrome(void) {return !host_flip_mode?1u:host_flip_mode==1u?0x28u:0u;}
static bool host_bad_baseline(unsigned block) {
    return host_fault==HOST_BITFLIP && host_fault_at==block+1u;
}

static void host_attempts(unsigned count) {
    unsigned attempts=0u,success=0u,read_fail=0u,guard_fail=0u;
    for(unsigned c=0;c<3u;c++) {
        attempts+=pages[c].records;
        for(unsigned b=0;b<3u;b++) {
            success+=pages[c].block[b].success;
            read_fail+=pages[c].block[b].read_fail;
            guard_fail+=pages[c].block[b].guard_fail;
        }
    }
    assert(attempts==count && host_reads==count);
    assert(success+read_fail+guard_fail==count);
}
static void host_complete_records(void) {
    uint64_t at_ms=0u;
    for(unsigned c=0;c<3u;c++) {
        const struct probe_page *page=&pages[c];
        assert(page->gap_ms==(c==0u?0u:c==1u?20u:100u));
        assert(page->records==96u && page->rounds==32u);
        for(unsigned i=0;i<96u;i++) {
            const struct probe_record *record=&records[c][i];
            unsigned b=i%3u,number=c*96u+i+1u;
            bool flipped=host_fault==HOST_BITFLIP && number==host_fault_at;
            bool different=host_bad_baseline(b)?!flipped:flipped;
            if(i) at_ms+=page->gap_ms;
            assert(record->round==i/3u+1u && record->block==probe_blocks[b]);
            assert(record->at_ms==at_ms && record->duration_ms==2u);
            assert(record->result==0);
            assert(record->crc==(flipped?host_flipped_crc[host_flip_mode][b]:host_expected_crc[b]));
            assert(record->different_bytes==(different?host_flip_bytes():0u));
            assert(record->bits==(different?host_flip_bytes():0u));
            assert(record->syndrome==(different?host_flip_syndrome():0u));
            assert(host_read_blocks[number-1u]==probe_blocks[b]);
            assert(host_read_starts[number-1u]==100000u+at_ms);
            at_ms+=2u;
        }
        assert(page->first.valid && page->last.valid);
        assert(page->first.round==1u && page->first.block==0u);
        assert(page->last.round==32u && page->last.block==255u);
        assert(page->first.at_ms==records[c][0].at_ms);
        assert(page->last.at_ms==records[c][95].at_ms);
        assert(page->first.duration_ms==2u && page->last.duration_ms==2u);
        assert(page->first.read.lock_attempts==c*96u+1u);
        assert(page->last.read.lock_attempts==(c+1u)*96u);
        assert(page->first.read.stage==KUI_VMU_READ_COMPLETE);
        assert(page->last.read.stage==KUI_VMU_READ_COMPLETE);
        assert(page->first.read.raw_crc_before==records[c][0].crc);
        assert(page->first.read.raw_crc_after==records[c][0].crc);
        assert(page->first.read.copy_crc==records[c][0].crc);
        assert(page->last.read.copy_crc==records[c][95].crc);
        for(unsigned b=0;b<3u;b++) {
            const struct probe_stats *stats=&page->block[b];
            assert(stats->attempts==32u && stats->success==32u);
            assert(!stats->read_fail && !stats->guard_fail);
            assert(stats->baseline_crc==(host_bad_baseline(b)?host_flipped_crc[host_flip_mode][b]:host_expected_crc[b]));
            assert(stats->last_crc==records[c][93u+b].crc && stats->have_previous);
        }
    }
    assert(at_ms==11976u && host_clock==111976u);
    assert(host_sleeps==190u);
    for(unsigned i=0;i<190u;i++) assert(host_sleep_values[i]==(i<95u?20u:100u));
    for(unsigned b=0;b<3u;b++) {
        uint8_t expected[512];memcpy(expected,host_original[b],512u);
        if(host_bad_baseline(b)) host_apply_flip(expected);
        assert(baseline_ready[b]);
        assert(baseline_crc[b]==(host_bad_baseline(b)?host_flipped_crc[host_flip_mode][b]:host_expected_crc[b]));
        assert(!memcmp(baseline[b],expected,512u));
    }
    assert(launch_attempted && complete && !fault && !guard_mask);
    assert(host_enumerations==577u && page_index==0u);
    host_attempts(288u);host_unchanged();
}
static void host_navigation(void) {
    struct probe_page pages_before[3];
    struct probe_record records_before[3][96];
    memcpy(pages_before,pages,sizeof(pages));memcpy(records_before,records,sizeof(records));
    unsigned reads_before=host_reads,enums_before=host_enumerations,slot_before=selected_slot;
    uint64_t clock_before=host_clock;
    /* Unplug and mutate the live diagnostic after completion. Paging must
     * retain its own records and snapshots without touching the source. */
    host_present=false;memset(&host_diagnostic,0xdb,sizeof(host_diagnostic));
    probe_buttons(CONT_DPAD_LEFT);assert(page_index==2u && !table_offset);
    probe_buttons(CONT_DPAD_RIGHT);assert(page_index==0u && !table_offset);
    probe_buttons(CONT_DPAD_RIGHT);assert(page_index==1u && !table_offset);
    probe_buttons(CONT_DPAD_LEFT);assert(page_index==0u && !table_offset);
    assert(!detail_view && !table_view);
    probe_buttons(CONT_X);assert(detail_view && !table_view && !table_offset);render();
    probe_buttons(CONT_DPAD_DOWN|CONT_DPAD_UP);assert(!table_offset);
    probe_buttons(CONT_X);assert(!detail_view && table_view && !table_offset);
    probe_buttons(CONT_DPAD_UP);assert(!table_offset);
    for(unsigned i=0;i<20u;i++) probe_buttons(CONT_DPAD_DOWN);
    assert(table_offset==90u && table_offset<pages[0].records);
    render();
    for(unsigned i=0;i<20u;i++) probe_buttons(CONT_DPAD_UP);
    assert(!table_offset);
    probe_buttons(CONT_DPAD_DOWN);assert(table_offset==10u);
    probe_buttons(CONT_DPAD_RIGHT);assert(page_index==1u && !table_offset);
    probe_buttons(CONT_X);assert(!table_view && !detail_view && !table_offset);render();
    probe_buttons(CONT_A|CONT_B|CONT_START);assert(!table_view && !detail_view);
    assert(!probe_run());
    for(unsigned i=0;i<6u;i++) {probe_buttons(CONT_X);render();}
    assert(selected_slot==slot_before && host_reads==reads_before);
    assert(host_enumerations==enums_before && host_clock==clock_before);
    assert(!memcmp(pages_before,pages,sizeof(pages)));
    assert(!memcmp(records_before,records,sizeof(records)));
    host_unchanged();
}
static void host_stable_case(void) {
    host_fixture();
    probe_buttons(CONT_DPAD_LEFT);assert(selected_slot==7u && !launch_attempted);
    probe_buttons(CONT_DPAD_RIGHT);assert(selected_slot==0u && !host_enumerations);
    probe_buttons(CONT_DPAD_RIGHT);assert(selected_slot==1u);
    probe_buttons(CONT_DPAD_LEFT);assert(selected_slot==0u);
    probe_buttons(CONT_X);host_complete_records();
    for(unsigned c=0;c<3u;c++) for(unsigned b=0;b<3u;b++) {
        const struct probe_stats *stats=&pages[c].block[b];
        assert(!stats->different && !stats->bytes && !stats->bits && !stats->syndrome);
        assert(!stats->have_difference && !stats->first_round && !stats->first_at_ms);
        assert(!stats->transitions && !stats->first_changed.valid);
    }
    host_navigation();
}
static void host_bitflip_case(void) {
    host_fixture();host_fault=HOST_BITFLIP;assert(probe_run());host_complete_records();
    unsigned cadence=(host_fault_at-1u)/96u,index=(host_fault_at-1u)%96u,b=index%3u;
    for(unsigned c=0;c<3u;c++) for(unsigned block=0;block<3u;block++) {
        const struct probe_stats *stats=&pages[c].block[block];
        bool affected=c==cadence && block==b;
        assert(stats->different==(affected?1u:0u));
        assert(stats->bytes==(affected?host_flip_bytes():0u));
        assert(stats->bits==(affected?host_flip_bytes():0u));
        assert(stats->syndrome==(affected?host_flip_syndrome():0u) && stats->have_difference==affected);
        unsigned transitions=(index<3u || index>=93u)?1u:2u;
        assert(stats->transitions==(affected?transitions:0u));
        assert(stats->first_changed.valid==affected);
    }
    const struct probe_stats *stats=&pages[cadence].block[b];
    assert(stats->first_round==index/3u+1u);
    assert(stats->first_at_ms==records[cadence][index].at_ms);
    assert(stats->first_bytes==host_flip_bytes() && stats->first_bits==host_flip_bytes());
    assert(stats->first_syndrome==host_flip_syndrome());
    unsigned first_index=host_flip_mode?12u:11u,first_mask=host_flip_mode?0x20u:1u;
    assert(stats->first[0].index==first_index && stats->first[0].before==host_original[b][first_index]);
    assert(stats->first[0].after==(host_original[b][first_index]^first_mask));
    assert(stats->first[0].mask==first_mask);
    if(host_flip_mode) {
        unsigned second_mask=host_flip_mode==1u?0x08u:0x20u;
        assert(stats->first[1].index==18u && stats->first[1].before==host_original[b][18]);
        assert(stats->first[1].after==(host_original[b][18]^second_mask));
        assert(stats->first[1].mask==second_mask);
    } else assert(stats->first[1].index==512u);
    const struct probe_diag_snapshot *changed=&stats->first_changed;
    assert(changed->valid && changed->round==index/3u+1u && changed->block==probe_blocks[b]);
    assert(changed->at_ms==records[cadence][index].at_ms && changed->duration_ms==2u);
    assert(changed->read.lock_attempts==host_fault_at && changed->read.block==probe_blocks[b]);
    assert(changed->read.stage==KUI_VMU_READ_COMPLETE && !changed->read.result);
    assert(changed->read.data_len==130u && changed->read.response==8);
    assert(changed->read.raw_crc_before==host_flipped_crc[host_flip_mode][b]);
    assert(changed->read.raw_crc_after==host_flipped_crc[host_flip_mode][b]);
    assert(changed->read.copy_crc==host_flipped_crc[host_flip_mode][b]);
    host_navigation();
}
static void host_shared_baseline_case(void) {host_fault_at=97u;host_bitflip_case();}
static void host_last_sample_case(void) {host_fault_at=96u;host_bitflip_case();}
static void host_two_bytes_case(void) {host_flip_mode=1u;host_bitflip_case();}
static void host_cancel_syndrome_case(void) {host_flip_mode=2u;host_bitflip_case();}
static void host_bad_baseline_case(void) {
    host_fixture();host_fault=HOST_BITFLIP;host_fault_at=1u;
    assert(probe_run());host_complete_records();
    for(unsigned c=0;c<3u;c++) for(unsigned b=0;b<3u;b++) {
        const struct probe_stats *stats=&pages[c].block[b];
        unsigned differences=b==0u?(c==0u?31u:32u):0u;
        assert(stats->different==differences && stats->bytes==differences && stats->bits==differences);
        assert(stats->syndrome==(differences&1u));
        assert(stats->transitions==((c==0u && b==0u)?1u:0u));
        assert(stats->have_difference==(b==0u) && stats->first_changed.valid==(b==0u));
        if(b) continue;
        const struct probe_diag_snapshot *changed=&stats->first_changed;
        unsigned record_index=c==0u?3u:0u;
        assert(stats->first_round==(c==0u?2u:1u));
        assert(stats->first_bytes==1u && stats->first_bits==1u && stats->first_syndrome==1u);
        assert(stats->first[0].index==11u && stats->first[0].mask==1u);
        assert(stats->first[0].before==(host_original[0][11]^1u));
        assert(stats->first[0].after==host_original[0][11] && stats->first[1].index==512u);
        assert(changed->round==stats->first_round && changed->block==0u);
        assert(changed->at_ms==records[c][record_index].at_ms && changed->duration_ms==2u);
        assert(changed->read.lock_attempts==c*96u+record_index+1u);
        assert(changed->read.raw_crc_before==host_expected_crc[0]);
        assert(changed->read.raw_crc_after==host_expected_crc[0]);
        assert(changed->read.copy_crc==host_expected_crc[0]);
    }
    host_navigation();
}
static void host_failure_case(enum host_fault injected) {
    host_fixture();host_fault=injected;
    assert(!probe_run());
    assert(launch_attempted && complete && page_index==0u && !table_offset);
    assert(host_reads==17u && !host_sleeps && host_clock==100034u);
    assert(pages[0].records==17u && pages[0].rounds==5u);
    assert(!pages[1].records && !pages[2].records);
    assert(!pages[1].first.valid && !pages[2].first.valid);
    assert(pages[0].block[0].attempts==6u && pages[0].block[0].success==6u);
    assert(pages[0].block[1].attempts==6u && pages[0].block[1].success==5u);
    assert(pages[0].block[2].attempts==5u && pages[0].block[2].success==5u);
    const struct probe_record *last=&records[0][16];
    assert(last->round==6u && last->block==254u && last->at_ms==32u && last->duration_ms==2u);
    assert(!last->crc && !last->different_bytes && !last->bits && !last->syndrome);
    assert(fault_page==0u && fault_round==6u && fault_block==254u);
    assert(pages[0].first.valid && pages[0].first.read.lock_attempts==1u);
    assert(pages[0].last.valid && pages[0].last.read.lock_attempts==17u);
    if(injected==HOST_HEADER || injected==HOST_TIMEOUT) {
        int expected=injected==HOST_HEADER?MAPLE_EFAIL:MAPLE_ETIMEOUT;
        assert(fault==PROBE_FAULT_READ && fault_result==expected && last->result==expected);
        assert(pages[0].block[1].read_fail==1u && !pages[0].block[1].guard_fail);
        assert(pages[0].last.read.stage==(injected==HOST_HEADER?KUI_VMU_READ_LENGTH:KUI_VMU_READ_WAIT));
        assert(pages[0].last.read.result==expected && !guard_mask);
        assert(host_enumerations==34u);
    } else {
        unsigned expected=injected==HOST_REMOVAL?GUARD_MISSING:
            injected==HOST_REPLACEMENT?GUARD_REPLACED:
            injected==HOST_FUNCTIONS?GUARD_FUNCTIONS:GUARD_NAME;
        assert(fault==PROBE_FAULT_DEVICE && !fault_result && last->result==PROBE_RESULT_GUARD);
        assert(!pages[0].block[1].read_fail && pages[0].block[1].guard_fail==1u);
        assert(guard_mask==expected && host_enumerations==35u);
        assert(pages[0].last.read.stage==KUI_VMU_READ_COMPLETE);
        if(injected==HOST_NAME) {
            assert(!guard_name_index && guard_expected_name=='R' && guard_actual_name==('R'^1));
        }
    }
    host_attempts(17u);host_unchanged();
    struct probe_page frozen=pages[0];
    unsigned before=host_enumerations;
    memset(&host_diagnostic,0x97,sizeof(host_diagnostic));host_present=false;
    for(unsigned i=0;i<8u;i++) {probe_buttons(CONT_X|CONT_DPAD_RIGHT);render();}
    assert(!probe_run() && host_reads==17u && host_enumerations==before);
    assert(!memcmp(&frozen,&pages[0],sizeof(frozen)) && selected_slot==0u);
}
static void host_header_case(void) {host_failure_case(HOST_HEADER);}
static void host_timeout_case(void) {host_failure_case(HOST_TIMEOUT);}
static void host_removal_case(void) {host_failure_case(HOST_REMOVAL);}
static void host_replacement_case(void) {host_failure_case(HOST_REPLACEMENT);}
static void host_functions_case(void) {host_failure_case(HOST_FUNCTIONS);}
static void host_name_case(void) {host_failure_case(HOST_NAME);}
static void host_pre_guard_case(void) {
    host_fixture();host_drop_on_lock=true;assert(!probe_run());
    assert(fault==PROBE_FAULT_DEVICE && guard_mask==GUARD_MISSING);
    assert(launch_attempted && complete && host_enumerations==2u);
    assert(!host_reads && !host_sleeps && !pages[0].records && !pages[0].rounds);
    assert(!pages[0].first.valid && !pages[0].last.valid);
    assert(pages[0].block[0].guard_fail==1u && !pages[0].block[0].attempts);
    assert(!baseline_ready[0] && !baseline_ready[1] && !baseline_ready[2]);
    assert(!probe_run() && !host_reads && host_enumerations==2u);host_unchanged();
}
static void host_case(const char *name,void (*test)(void)) {
    fflush(NULL);
    pid_t child=fork();assert(child>=0);
    if(!child) {test();printf("PASS %s\n",name);fflush(stdout);_exit(0);}
    int status;assert(waitpid(child,&status,0)==child);
    assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
}
int main(void) {
    host_case("stable: 288 interleaved reads; 190 cadence gaps; 11976ms; frozen reports and paging",host_stable_case);
    host_case("bitflip: one changed byte/bit; exact CRC, first offset, syndrome and stable baseline",host_bitflip_case);
    host_case("shared baseline: first sample of next cadence still compares to original block",host_shared_baseline_case);
    host_case("last sample: one cadence transition; next cadence has no carryover transition",host_last_sample_case);
    host_case("two changed bytes: exact first two offsets, values, XOR masks; 2 bits and syndrome 0x28",host_two_bytes_case);
    host_case("XOR cancellation: 2 changed bytes and bits remain visible with syndrome 0",host_cancel_syndrome_case);
    host_case("bad first baseline: slower stable cadences have 32 baseline differences, 0 transitions",host_bad_baseline_case);
    host_case("header error: terminal after 17 reads; no retry; frozen helper failure",host_header_case);
    host_case("timeout: terminal after 17 reads; no retry or second run",host_timeout_case);
    host_case("post-read removal: 17 attempts, 16 successes; sample refused",host_removal_case);
    host_case("replacement: pinned device pointer refuses identical metadata",host_replacement_case);
    host_case("functions: pinned function word refuses changed device",host_functions_case);
    host_case("name: pinned 30-byte product identity refuses changed device",host_name_case);
    host_case("pre-read removal: no transaction, one guarded refusal, unlock once",host_pre_guard_case);
    puts("VMU read probe host: 14 cases passed; physical writes/SDK reads/SD calls 0");
    return 0;
}
