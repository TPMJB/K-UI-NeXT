/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_FS_WITNESS_H
#define KUI_VMU_FS_WITNESS_H
#include <stddef.h>
#include <stdint.h>

#define KUI_VMU_FS_WITNESS_MAX_RECORDS 8192u
#define KUI_VMU_FS_WITNESS_INDEX_SLOTS 16384u
#define KUI_VMU_FS_WITNESS_RECORD_BYTES 36u
#define KUI_VMU_FS_WITNESS_HEADER_BYTES 512u
#define KUI_VMU_FS_WITNESS_FILE_BYTES 295424u
#define KUI_VMU_FS_WITNESS_RUN_BLOCKS 32u
#define KUI_VMU_FS_WITNESS_SCRATCH_BYTES (32u*512u)
#define KUI_VMU_FS_WITNESS_DIGEST_OFFSET 120u

/* No filesystem object, device pointer or callback is serialized. The opaque
 * binding SHA must bind the canonical ownership profile AND collection/build
 * identity. A live receipt separately pins the final manifest SHA/generation;
 * persisted witness bytes alone never authorize admission or a write. */
struct kui_vmu_fs_witness_binding {
    uint64_t card_sectors,partition_first,partition_end;
    uint32_t high_capacity,generation;
    uint8_t cid[16],image_id[16],binding_sha256[32];
};
/* Canonical wire record, sortable in place without an endian-dependent dump. */
struct kui_vmu_fs_witness_record {uint8_t lba[4],sha256[32];};
typedef int (*kui_vmu_fs_witness_read_sector)(void *,uint32_t,uint8_t out[512]);
typedef int (*kui_vmu_fs_witness_read_run)(void *,uint32_t,uint32_t,uint8_t *);

enum kui_vmu_fs_witness_result {
    KUI_VMU_FS_WITNESS_OK=0,KUI_VMU_FS_WITNESS_ARGUMENT=-1,
    KUI_VMU_FS_WITNESS_LIMIT=-2,KUI_VMU_FS_WITNESS_CHANGED=-3,
    KUI_VMU_FS_WITNESS_IO=-4,KUI_VMU_FS_WITNESS_FORMAT=-5,
    KUI_VMU_FS_WITNESS_BINDING=-6,KUI_VMU_FS_WITNESS_DIGEST=-7,
    KUI_VMU_FS_WITNESS_ADMISSION=-8
};
/* Temporary high-stage scratch only. Never place this on the low stack.
 * Capture deduplicates with a bounded open-address index. Once finished the
 * records are strictly sorted and the index is no longer used. */
struct kui_vmu_fs_witness_capture {
    struct kui_vmu_fs_witness_record records[KUI_VMU_FS_WITNESS_MAX_RECORDS];
    uint16_t index[KUI_VMU_FS_WITNESS_INDEX_SLOTS];
    struct kui_vmu_fs_witness_binding binding;
    kui_vmu_fs_witness_read_sector read;
    void *context;
    uint32_t count,observed_reads,started,finished;
    int result;
};
struct kui_vmu_fs_witness_stats {uint32_t sectors,commands,bad_lba;int result;};

int kui_vmu_fs_witness_capture_begin(struct kui_vmu_fs_witness_capture *,
    const struct kui_vmu_fs_witness_binding *,kui_vmu_fs_witness_read_sector,void *);
/* Wrap the EXACT read callback used by the complete ordinary fs_admit scan.
 * A capture overflow/conflict disables retention but keeps forwarding reads,
 * so the caller can finish ordinary admission and fall back without truncation.
 * Underlying I/O failures still fail the read. No additional read occurs. */
int kui_vmu_fs_witness_capture_read(void *,uint32_t,uint8_t out[512]);
/* admission_result must be the actual complete fs_admit result. Outputs are
 * zero on refusal. A successful result is only a replayable observation set;
 * the lifecycle owner still establishes provenance and exclusive ownership. */
int kui_vmu_fs_witness_capture_finish(struct kui_vmu_fs_witness_capture *,
    int admission_result,uint8_t header[512],uint8_t manifest_sha256[32]);
/* Header shape/count only; this grants no authority and does not accept a
 * partial manifest. The exact count determines the meaningful record bytes. */
int kui_vmu_fs_witness_header_count(const uint8_t header[512],uint32_t *);
/* expected_manifest_sha256 must be the separate live receipt pin, disjoint
 * from the untrusted header/records. The header's own digest is not a pin. */
int kui_vmu_fs_witness_validate(const uint8_t header[512],
    const struct kui_vmu_fs_witness_record *,size_t records_bytes,
    const struct kui_vmu_fs_witness_binding *,const uint8_t expected_manifest_sha256[32]);
/* Caller owns an exclusive, freshly identified healthy card. read_run must
 * retain all ordinary wire CRC and healthy stream-stop checks and write only
 * its supplied output. Scratch is disjoint, 32-byte aligned and >=16384 bytes.
 * This performs no mount, write, initialization or authority publication. */
int kui_vmu_fs_witness_replay(const uint8_t header[512],
    const struct kui_vmu_fs_witness_record *,size_t records_bytes,
    const struct kui_vmu_fs_witness_binding *,const uint8_t expected_manifest_sha256[32],
    kui_vmu_fs_witness_read_run,void *,void *scratch,size_t scratch_bytes,
    struct kui_vmu_fs_witness_stats *);
#endif
