/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_REPORT_FILE_H
#define KUI_REPORT_FILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kui/vmu_fs_admission.h"

#define KUI_REPORT_FILE_BYTES 32768u
#define KUI_REPORT_TEXT_BYTES (KUI_REPORT_FILE_BYTES-512u)
#define KUI_REPORT_FILE_EXTENTS 16u
#define KUI_REPORT_FILE_MAGIC UINT32_C(0x3152464b)
struct kui_report_file_extent {uint32_t lba,blocks;};
/* Private controller copy of a freshly proved file. Extents are logical
 * order, cover exactly64 sectors and never authorize filesystem metadata.
 * No stage pointer or callback survives handoff. */
struct kui_report_file_descriptor {
    uint32_t magic,version,bytes,profile;
    uint64_t card_sectors;
    uint32_t high_capacity,extent_count;
    uint8_t cid[16];
    struct kui_report_file_extent extents[KUI_REPORT_FILE_EXTENTS];
    uint32_t checksum,reserved[19];
};
_Static_assert(sizeof(struct kui_report_file_descriptor)==256u,"Report descriptor ABI");
struct kui_report_file_input {
    int (*read_sector)(void *,uint32_t,uint8_t out[512]);
    void *context;
    uint64_t card_sectors,partition_first,partition_end;
    uint32_t high_capacity;
    uint8_t cid[16];
    /* One uppercase ASCII8.3 name, e.g. TC.TXT, under /KUI/reports. */
    const char *file_name;
    uint32_t profile;
    const struct kui_vmu_fs_range *forbidden;
    size_t forbidden_count;
};
/* Read-only, complete bounded FAT32/exFAT ownership admission. Same scratch
 * and result domain as the existing VMU filesystem walker. A refused output
 * is zero. The caller holds the initialized card exclusively throughout. */
int kui_report_file_admit(const struct kui_report_file_input *,
    struct kui_report_file_descriptor *,struct kui_vmu_fs_workspace *);
bool kui_report_file_descriptor_valid(const struct kui_report_file_descriptor *);

struct kui_report_file_io {
    void *context;
    /* begin succeeds only after DATA/audio/CMD18 and owned DMA are fenced.
     * A failed begin acquires nothing. end is called once after success. */
    int (*begin)(void *);
    int (*identity)(void *,uint8_t cid[16],uint64_t *sectors,uint32_t *high_capacity);
    int (*read_sector)(void *,uint32_t,uint8_t out[512]);
    /* Exactly one PIO CMD24 sector. No DMA, filesystem or multiwrite. */
    int (*write_sector)(void *,uint32_t,const uint8_t in[512]);
    int (*sync)(void *);
    int (*end)(void *,bool healthy);
};
struct kui_report_file_work {uint8_t sector[512],check[512];};
enum kui_report_file_result {
    KUI_REPORT_FILE_OK=0,KUI_REPORT_FILE_ARGUMENT=-20,
    KUI_REPORT_FILE_NOT_FROZEN=-21,KUI_REPORT_FILE_CLAIM=-22,
    KUI_REPORT_FILE_IDENTITY=-23,KUI_REPORT_FILE_IO=-24,
    KUI_REPORT_FILE_VERIFY=-25,KUI_REPORT_FILE_RELEASE=-26,KUI_REPORT_FILE_SOURCE=-27
};
/* Return exactly requested bytes, or a smaller count/error SIZE_MAX. Source
 * is a deterministic random-access rendering of the already frozen snapshot.
 * Called only for actual text, never padding; it may be reread for validation
 * and readback. No callback runs until frozen/descriptor/size gates pass. */
typedef size_t (*kui_report_file_read_text)(void *,size_t offset,uint8_t *out,size_t bytes);
int kui_report_file_save_source(const struct kui_report_file_descriptor *,bool frozen,
    size_t bytes,kui_report_file_read_text,void *source_context,
    const struct kui_report_file_io *,struct kui_report_file_work *);
/* Terminal-only. Caller has frozen the source snapshot and will never resume
 * the game. ASCII text is padded to the fixed file size, without changing
 * FAT/directory/bitmap bytes. The last sector is invalidated first; a CRC32
 * completion footer is published only after synced body readback. */
int kui_report_file_save(const struct kui_report_file_descriptor *,bool frozen,
    const char *text,size_t bytes,const struct kui_report_file_io *,struct kui_report_file_work *);
#endif
