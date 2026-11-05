/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_UPDATE_H
#define KUI_UPDATE_H
#include "kui/apps.h"
#define KUI_UPDATE_HEADER_BYTES 128u
#define KUI_UPDATE_ENTRY_BYTES 128u
#define KUI_UPDATE_ENTRY_V2_BYTES 160u
#define KUI_UPDATE_V1_MAX_ENTRIES 64u
#define KUI_UPDATE_MAX_ENTRIES 256u
#define KUI_UPDATE_MAX_PAYLOAD (128u*1024u*1024u)
#define KUI_UPDATE_PATH_BYTES 80u
#define KUI_UPDATE_BUILD_BYTES 13u
#define KUI_UPDATE_ACTIVATION_BYTES 128u
#define KUI_UPDATE_RECEIPT_MAX (KUI_UPDATE_HEADER_BYTES+KUI_UPDATE_MAX_ENTRIES*KUI_UPDATE_ENTRY_V2_BYTES)
#define KUI_UPDATE_SYSTEM "0:/KUI/system"
#define KUI_UPDATE_ACTIVE0 KUI_UPDATE_SYSTEM "/active0.bin"
#define KUI_UPDATE_ACTIVE1 KUI_UPDATE_SYSTEM "/active1.bin"
#define KUI_UPDATE_CAPABILITY_BYTES 16u
#define KUI_UPDATE_CAPABILITY_INITIALIZER {'K','U','I','B','A','N','K','1',1,0,0,0,254,255,255,255}
#define KUI_UPDATE_ENGINE_BYTES 16u
#define KUI_UPDATE_ENGINE_INITIALIZER {'K','U','I','E','N','G','2','!',2,0,0,0,253,255,255,255}
#define KUI_UPDATE_FULL 1u
enum kui_update_member_policy {
    KUI_UPDATE_REPLACE=0, KUI_UPDATE_APPROVED_LEGACY=1, KUI_UPDATE_INHERIT_ROOT=2,
    KUI_UPDATE_KEEP=2, KUI_UPDATE_SELF_KUI=3, KUI_UPDATE_RAW=4, KUI_UPDATE_REMOVE=5
};
enum kui_update_result {
    KUI_UPDATE_OK, KUI_UPDATE_ARGUMENT, KUI_UPDATE_NOT_FOUND, KUI_UPDATE_FORMAT,
    KUI_UPDATE_CHECKSUM, KUI_UPDATE_COHORT, KUI_UPDATE_RUNTIME, KUI_UPDATE_IO,
    KUI_UPDATE_CANCELLED, KUI_UPDATE_SPACE, KUI_UPDATE_STALE, KUI_UPDATE_PROTECTED,
    KUI_UPDATE_MEMORY, KUI_UPDATE_SEQUENCE, KUI_UPDATE_ENGINE, KUI_UPDATE_LIMIT
};
struct kui_update_entry {
    char path[KUI_UPDATE_PATH_BYTES];
    uint64_t bytes;
    uint8_t sha256[32];
    uint8_t policy; /* enum kui_update_member_policy */
    uint64_t payload_offset; /* absolute byte offset in .kup */
    char build[KUI_UPDATE_BUILD_BYTES]; /* policy3 envelope identity, independent of package ID */
};
struct kui_update_package {
    char build[KUI_UPDATE_BUILD_BYTES];
    uint8_t channel,flags,manifest_sha256[32]; /* channel: 0 stable, 1 testing */
    uint32_t count;
    uint64_t payload_bytes,file_bytes;
    uint32_t version; /* zero denotes v1 for callers constructing legacy descriptors */
    struct kui_update_entry entries[KUI_UPDATE_MAX_ENTRIES];
};
struct kui_update_pin {
    char build[KUI_UPDATE_BUILD_BYTES];
    uint8_t channel,flags,manifest_sha256[32];
    uint64_t file_bytes;
    uint32_t version; /* zero means v1 for legacy pins */
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
 * V2 uses KUIUPD2\0/version2, flags@45 (FULL bit0 only), zero46..63,
 * entries160 with own build12@128 and zero140..159
 * (including the required build terminator@140). Policies3/4 replace .kui/raw
 * software, policy5 preserves absence, and policy2 keeps effective current.
 * Runtime is optional in incoming v2 packages; installation seals the effective
 * GUI into the resulting snapshot. Omitted/KEEP roles retain effective current
 * software; FULL snapshots instead resolve omitted software as absence,
 * require an explicit SELF_KUI GUI, and cannot contain KEEP directives.
 * REMOVE is an explicit absence. Policies2/5 have size0, zero SHA,
 * empty own build and no payload. In v1 runtime.kui remains mandatory first;
 * non-GUI policy2 and omitted roles inherit mutable retained root software.
 * No user data, arbitrary paths or root writes are allowed. */
size_t kui_update_catalog_count(void);
const char *kui_update_catalog_path(size_t index);
/* Bounded software namespace: root runtime/tools/recovery.kui, flat lowercase
 * .kui/.bin under apps/{games,disc,music}, music .ogg/.wav, the exact game probe.dat,
 * redump.db/tosec.db and assets/ with at most four safe lowercase path components.
 * No traversal or user data. V1 wire descriptors retain the legacy namespace. */
bool kui_update_module_path_valid(const char *path);
struct kui_update_legacy {
    const char *path,*build,*sha256;
    uint64_t bytes;
};
const struct kui_update_legacy *kui_update_legacy_for_path(const char *path);
/* V1 policy0 binds to the release ID and policy1 to exact pinned legacy
 * metadata. V2 policy3 binds to the entry's own envelope build. Other policies
 * have no envelope build and return NULL. */
const char *kui_update_entry_build(const char *release_build,const struct kui_update_entry *entry);
uint32_t kui_update_package_version(const struct kui_update_package *package);
size_t kui_update_entry_bytes(const struct kui_update_package *package);
/* Normalize validated descriptor offsets/sizes and recompute manifest SHA.
 * Used to seal a fully materialized v2 snapshot before writing its receipt. */
enum kui_update_result kui_update_package_seal(struct kui_update_package *package);
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
/* Declared-only lookup above preserves exact wire identity. Effective lookup
 * copies the declared descriptor, or synthesizes policy2 for an omitted allowed
 * role (effective current in v2; root in v1), or policy5 for an omitted FULL
 * snapshot role. Unknown paths, invalid packages
 * and output/input overlap are refused. */
bool kui_update_package_member(const struct kui_update_package *,const char *relative,
    struct kui_update_entry *out);
/* Exactly one initialized KUIBANK1/protocol/inverse marker is required in the
 * GUI payload. A valid legacy runtime cannot safely load a selected bank. */
bool kui_update_capability_valid(const void *payload,size_t bytes);
/* Exactly one initialized KUIENG2!/2/inverse marker returns 2; otherwise 0. */
unsigned kui_update_engine_level(const void *payload,size_t bytes);
bool kui_update_activation_encode(const struct kui_update_activation *,uint8_t wire[128]);
bool kui_update_activation_decode(const uint8_t wire[128],struct kui_update_activation *out);
const char *kui_update_result_name(enum kui_update_result result);
/* FatFs caller owns one mounted volume/exclusive storage worker. These APIs
 * never connect devices, execute code, or write anything outside system/.
 * inspect verifies all payload SHA, runtime headers/build/CRC and GUI marker.
 * install re-inspects the exact pin, completes and rereads bank replacements,
 * writes/syncs/rereads package.bin LAST, then publishes an inactive activation.
 * V2 installs materialize effective selected replacements and GUI, preserving
 * omitted/KEEP roles and explicit absence in the normalized snapshot receipt.
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
