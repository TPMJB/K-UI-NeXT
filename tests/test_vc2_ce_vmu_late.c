/* SPDX-License-Identifier: GPL-3.0-only */
/* Authored FAT-map/raw-block fixture, not a filesystem or console model. */
#include "kui/vc2_ce_vmu_late.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FILE_BYTES KUI_VC2_VMU_RESERVE_BYTES
#define SECTORS KUI_VC2_VMU_LATE_BLOCKS
#define READ_ERROR UINT32_C(0xdead1234)
static uint8_t source[FILE_BYTES];
static _Alignas(32) uint8_t guarded[FILE_BYTES+1024u];
#define destination (guarded+512u)
static uint32_t expected_lba[SECTORS],calls,fail_at,initializers,prepared_crc;

static uint32_t raw_read(uint32_t lba,uint8_t out[512]) {
    assert(calls<SECTORS && lba==expected_lba[calls]);
    assert(out==destination+calls*512u);
    ++calls;
    if(calls==fail_at) {memset(out,0xcc,512u);return READ_ERROR;}
    memcpy(out,source+(calls-1u)*512u,512u);
    return 0u;
}
static void fresh(void) {
    for(uint32_t i=0u;i<23300u;++i)source[i]=(uint8_t)(i*29u+(i>>8));
    memset(source+23300u,0,FILE_BYTES-23300u);
    prepared_crc=kui_vc2_ce_vmu_crc32(source,FILE_BYTES);
    memset(guarded,0xa5,sizeof(guarded));
    calls=fail_at=initializers=0u;
}
static void expected_runs(uint32_t first,uint32_t split,uint32_t second) {
    for(uint32_t i=0u;i<SECTORS;++i)
        expected_lba[i]=i<split?first+i:second+i-split;
}
static void guard_unchanged(void) {
    for(uint32_t i=0u;i<512u;++i)assert(guarded[i]==0xa5u);
    for(uint32_t i=512u+FILE_BYTES;i<sizeof(guarded);++i)assert(guarded[i]==0xa5u);
}
/* This authored caller proves the pure header contract only. The production
 * gate has its own execution test of read -> CRC -> initializer. */
static uint32_t admit_and_read(uint32_t first,uint32_t split,uint32_t second,
    uint64_t begin,uint64_t end,uint32_t *failed_lba) {
    if(!kui_vc2_vmu_late_map_valid(first,split,second,begin,end))return 1u;
    uint32_t status=kui_vc2_vmu_late_read(first,split,second,destination,raw_read,failed_lba);
    if(status)return status;
    if(kui_vc2_ce_vmu_crc32(destination,FILE_BYTES)!=prepared_crc)return 2u;
    ++initializers;return 0u;
}
static void one_and_fragmented_files(void) {
    for(uint32_t split=1u;split<=SECTORS;++split)for(uint32_t reverse=0u;reverse<2u;++reverse) {
        fresh();uint32_t failed=UINT32_MAX;
        const uint32_t first=reverse?1000u:100u;
        const uint32_t second=split==SECTORS?0u:(reverse?100u:1000u);
        expected_runs(first,split,second);
        assert(!admit_and_read(first,split,second,100u,2000u,&failed));
        assert(calls==SECTORS && initializers==1u && failed==UINT32_MAX);
        assert(!memcmp(destination,source,FILE_BYTES));
        guard_unchanged();
    }
    /* The final valid physical32-bit LBA does not wrap into an extra read. */
    fresh();expected_runs(UINT32_MAX-SECTORS+1u,SECTORS,0u);
    uint32_t failed=UINT32_MAX;
    assert(!admit_and_read(UINT32_MAX-SECTORS+1u,SECTORS,0u,UINT32_MAX-SECTORS+1u,
        UINT64_C(0x100000000),&failed));
    assert(calls==SECTORS && initializers==1u);guard_unchanged();
}
static void errors_stop_at_exact_block(void) {
    for(uint32_t ordinal=1u;ordinal<=SECTORS;++ordinal) {
        fresh();expected_runs(100u,19u,1000u);fail_at=ordinal;
        uint32_t failed=UINT32_MAX;
        assert(admit_and_read(100u,19u,1000u,100u,2000u,&failed)==READ_ERROR);
        assert(calls==ordinal && failed==expected_lba[ordinal-1u] && !initializers);
        assert(!memcmp(destination,source,(ordinal-1u)*512u));
        for(uint32_t i=(ordinal-1u)*512u;i<ordinal*512u;++i)assert(destination[i]==0xccu);
        for(uint32_t i=ordinal*512u;i<FILE_BYTES;++i)assert(destination[i]==0xa5u);
        guard_unchanged();
    }
}
static void bad_maps_do_not_read(void) {
#define REFUSE(first,split,second,begin,end) do { \
    fresh();uint32_t failed=UINT32_MAX; \
    assert(admit_and_read(first,split,second,begin,end,&failed)==1u); \
    assert(!calls && !initializers && failed==UINT32_MAX); \
    for(uint32_t i=0u;i<sizeof(guarded);++i)assert(guarded[i]==0xa5u); \
} while(0)
    REFUSE(100u,0u,1000u,100u,2000u);
    REFUSE(100u,97u,1000u,100u,2000u);
    REFUSE(100u,UINT32_MAX,1000u,100u,2000u);
    REFUSE(100u,96u,1000u,100u,2000u); /* Single run must have no second LBA. */
    REFUSE(100u,95u,0u,0u,2000u); /* Missing second run cannot name sector0. */
    REFUSE(99u,19u,1000u,100u,2000u);
    REFUSE(1982u,19u,1000u,100u,2000u);
    REFUSE(100u,19u,99u,100u,2000u);
    REFUSE(100u,19u,1924u,100u,2000u);
    REFUSE(100u,19u,100u,100u,2000u); /* Physical runs may not overlap. */
    REFUSE(100u,19u,118u,100u,2000u);
    REFUSE(1000u,19u,924u,100u,2000u);
    REFUSE(UINT32_MAX,2u,100u,0u,UINT64_C(0x100000000));
    REFUSE(100u,94u,UINT32_MAX,0u,UINT64_C(0x100000000));
    REFUSE(100u,96u,0u,100u,100u);
    REFUSE(100u,96u,0u,2000u,100u);
    REFUSE(100u,96u,0u,100u,UINT64_C(0x100000001));
#undef REFUSE
    assert(kui_vc2_vmu_late_map_valid(100u,19u,119u,100u,196u));
    assert(kui_vc2_vmu_late_map_valid(1000u,19u,923u,923u,1019u));
    assert(kui_vc2_vmu_late_map_valid(100u,95u,UINT32_MAX,0u,UINT64_C(0x100000000)));
}
static void prepared_crc_failure_never_initializes(void) {
    for(uint32_t last=0u;last<2u;++last) {
        fresh();expected_runs(100u,96u,0u);source[last?FILE_BYTES-1u:0u]^=1u;
        uint32_t failed=UINT32_MAX;
        assert(admit_and_read(100u,96u,0u,100u,196u,&failed)==2u);
        assert(calls==SECTORS && !initializers && failed==UINT32_MAX);
        guard_unchanged();
    }
}
int main(void) {
    _Static_assert(SECTORS==96u,"Complete prepared-file block count");
    one_and_fragmented_files();errors_stop_at_exact_block();
    bad_maps_do_not_read();prepared_crc_failure_never_initializes();
    puts("PASS late helper header:1/2 FAT runs, all95 splits in both physical orders, all96 failing blocks, nonoverlap/partition/output bounds, full48KiB CRC refusal before authored initializer");
    return 0;
}
