/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_UPDATE_H
#define KUI_UPDATE_H
#include "kui/apps.h"
#define KUI_UPDATE_HEADER_BYTES 128u
#define KUI_UPDATE_ENTRY_BYTES 128u
#define KUI_UPDATE_MAX_ENTRIES 64u
#define KUI_UPDATE_MAX_PAYLOAD (128u*1024u*1024u)
#define KUI_UPDATE_PATH_BYTES 80u
#define KUI_UPDATE_BUILD_BYTES 13u
#define KUI_UPDATE_ACTIVATION_BYTES 128u
#define KUI_UPDATE_RECEIPT_MAX (128u+64u*128u)
#define KUI_UPDATE_SYSTEM "0:/KUI/system"
#define KUI_UPDATE_ACTIVE0 KUI_UPDATE_SYSTEM "/active0.bin"
#define KUI_UPDATE_ACTIVE1 KUI_UPDATE_SYSTEM "/active1.bin"
#define KUI_UPDATE_CAPABILITY_BYTES 16u
#define KUI_UPDATE_CAPABILITY_INITIALIZER {'K','U','I','B','A','N','K','1',1,0,0,0,254,255,255,255}
enum kui_update_member_policy {
    KUI_UPDATE_REPLACE=0, KUI_UPDATE_APPROVED_LEGACY=1, KUI_UPDATE_INHERIT_ROOT=2
};
enum kui_update_result {
    KUI_UPDATE_OK, KUI_UPDATE_ARGUMENT, KUI_UPDATE_NOT_FOUND, KUI_UPDATE_FORMAT,
    KUI_UPDATE_CHECKSUM, KUI_UPDATE_COHORT, KUI_UPDATE_RUNTIME, KUI_UPDATE_IO,
    KUI_UPDATE_CANCELLED, KUI_UPDATE_SPACE, KUI_UPDATE_STALE, KUI_UPDATE_PROTECTED,
    KUI_UPDATE_MEMORY, KUI_UPDATE_SEQUENCE
};
struct kui_update_entry {
    char path[KUI_UPDATE_PATH_BYTES];
    uint64_t bytes;
    uint8_t sha256[32];
    uint8_t policy; /* enum kui_update_member_policy */
    uint64_t payload_offset; /* absolute byte offset in .kup */
};
struct kui_update_package {
    char build[KUI_UPDATE_BUILD_BYTES];
    uint8_t channel,manifest_sha256[32]; /* 0 stable, 1 testing */
    uint32_t count;
    uint64_t payload_bytes,file_bytes;
    struct kui_update_entry entries[KUI_UPDATE_MAX_ENTRIES];
};
struct kui_update_pin {
    char build[KUI_UPDATE_BUILD_BYTES];
    uint8_t channel,manifest_sha256[32];
    uint64_t file_bytes;
};
struct kui_update_activation {
    uint64_t sequence;
    char selected[KUI_UPDATE_BUILD_BYTES],previous[KUI_UPDATE_BUILD_BYTES];
    uint8_t manifest_sha256[32]; /* selected bank; all zero for baseline */
};
/* Package header128: KUIUPD1\0; LE32 version@8=1,size@12=128,count@16,
 * manifest_bytes@20=count*128; LE64 payload_bytes@24; build12@32;
 * channel@44; zero45..63; manifest SHA256@64; zero96..123; CRC32@124
 * over the preceding124 bytes. Entries128: zero-padded path80,size64@80,
 * full-file SHA256@88,policy@120,zero121..127. All payload files follow in order.
 * Pure codec. Every required path occurs exactly once, in catalog order.
 * Policy2 explicitly inherits a non-GUI path from the retained root, with
 * size0, SHAallzero and no payload. Missing root files remain missing. Root
 * inheritance is shared/mutable, not an immutable bank-member guarantee.
 * No user data, arbitrary paths or root writes are allowed. */
size_t kui_update_catalog_count(void);
const char *kui_update_catalog_path(size_t index);
struct kui_update_legacy {
    const char *path,*build,*sha256;
    uint64_t bytes;
};
const struct kui_update_legacy *kui_update_legacy_for_path(const char *path);
/* Policy1 admits only exact pinned adapter metadata, never a general mixed
 * cohort. The returned ID is the only allowed .kui envelope build for a bank
 * replacement. Policy2 has no pinned build and returns NULL. */
const char *kui_update_entry_build(const char *release_build,const struct kui_update_entry *entry);
bool kui_update_build_valid(const char *build);
/* Known withdrawn GUI releases must not be installed or selected again.
 * Activation records retain syntactic validity and their sequence numbers. */
bool kui_update_build_withdrawn(const char *build);
bool kui_update_bank_path(const char *build,const char *relative,char *out,size_t bytes);
enum kui_update_result kui_update_header_decode(const uint8_t header[128],
    uint64_t file_bytes,struct kui_update_package *out);
enum kui_update_result kui_update_manifest_decode(struct kui_update_package *package,
    const void *manifest,size_t bytes);
enum kui_update_result kui_update_receipt_encode(const struct kui_update_package *package,
    void *wire,size_t capacity,size_t *written);
void kui_update_package_pin(const struct kui_update_package *package,struct kui_update_pin *out);
bool kui_update_pin_matches(const struct kui_update_pin *pin,const struct kui_update_package *package);
const struct kui_update_entry *kui_update_package_find(const struct kui_update_package *,const char *relative);
/* Exactly one initialized KUIBANK1/protocol/inverse marker is required in the
 * GUI payload. A valid legacy runtime cannot safely load a selected bank. */
bool kui_update_capability_valid(const void *payload,size_t bytes);
bool kui_update_activation_encode(const struct kui_update_activation *,uint8_t wire[128]);
bool kui_update_activation_decode(const uint8_t wire[128],struct kui_update_activation *out);
const char *kui_update_result_name(enum kui_update_result result);
/* FatFs caller owns one mounted volume/exclusive storage worker. These APIs
 * never connect devices, execute code, or write anything outside system/.
 * inspect verifies all payload SHA, runtime headers/build/CRC and GUI marker.
 * install re-inspects the exact pin, completes and rereads bank replacements,
 * writes/syncs/rereads package.bin LAST, then publishes an inactive activation.
 * Explicit inherited root entries are not opened, copied or verified here.
 * Existing complete same-build banks must match exactly; selected/previous
 * banks are never modified. Stop before activation leaves selection intact. */
enum kui_update_result kui_update_inspect(const char *path,struct kui_update_package *out,
    struct kui_app_status *,kui_log_fn,kui_cancel_fn,kui_app_progress_fn);
enum kui_update_result kui_update_install(const char *path,const struct kui_update_pin *expected,
    struct kui_app_status *,kui_log_fn,kui_cancel_fn,kui_app_progress_fn);
enum kui_update_result kui_update_activation_read(unsigned slot,struct kui_update_activation *,kui_cancel_fn);
enum kui_update_result kui_update_activation_latest(struct kui_update_activation *,unsigned *slot,kui_cancel_fn);
enum kui_update_result kui_update_bank_receipt_read(const char *build,struct kui_update_package *,kui_cancel_fn);
enum kui_update_result kui_update_bank_verify(const char *build,struct kui_update_package *,
    struct kui_app_status *,kui_log_fn,kui_cancel_fn,kui_app_progress_fn);
/* Read-only quota for an inspected package. available is actual free bytes;
 * required is additional allocation after reclaiming only declared members
 * of an incomplete unselected bank. Complete exact-bank reuse requires0. */
enum kui_update_result kui_update_plan_space(const struct kui_update_package *,
    uint64_t *required,uint64_t *available,kui_cancel_fn);
/* select and rollback verify the entire target bank before publishing. Empty
 * build means the immutable root baseline, whose GUI header, payload CRC and
 * updater capability are freshly checked. NULL expected_manifest is allowed
 * only for baseline; rollback checks the previous bank's own receipt. */
enum kui_update_result kui_update_select(const char *build,const uint8_t expected_manifest[32],
    struct kui_app_status *,kui_log_fn,kui_cancel_fn,kui_app_progress_fn);
enum kui_update_result kui_update_rollback(struct kui_app_status *,kui_log_fn,kui_cancel_fn,kui_app_progress_fn);
#endif
