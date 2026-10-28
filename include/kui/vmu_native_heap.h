/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_HEAP_H
#define KUI_VMU_NATIVE_HEAP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KUI_VMU_NATIVE_HEAP_VERSION 1u
#define KUI_VMU_NATIVE_HEAP_TAIL32 1u
#define KUI_VMU_NATIVE_HEAP_DISPATCH2 1u
#define KUI_VMU_NATIVE_HEAP_FREE_LIMIT 1024u
enum kui_vmu_native_heap_result {
    KUI_VMU_NATIVE_HEAP_NONE=0, KUI_VMU_NATIVE_HEAP_FOUND=1,
    KUI_VMU_NATIVE_HEAP_INVALID=-1, KUI_VMU_NATIVE_HEAP_AMBIGUOUS=-2
};
/* Immutable family metadata only. This grants no permission to call code,
 * patch an initializer, overwrite RAM, or retain memory across a reset.
 * A zero initializer_slot means no immutable pointer was present. Multiple
 * pointer copies require exactly one proved startup call among at most eight
 * candidates; an unused export-table copy cannot select an initializer. */
struct kui_vmu_native_heap {
    uint32_t version,bytes,family,layout,image_bytes,load;
    uint32_t initializer,initializer_slot,initializer_target,allocate,release;
    uint32_t dispatch_cell,dispatch_table,sentinel,cursor_cell;
    uint32_t begin_cell,bytes_cell,begin_copy_cell;
};
enum kui_vmu_native_heap_result kui_vmu_native_heap_discover(
    const void *,size_t,uint32_t,struct kui_vmu_native_heap *);
struct kui_vmu_native_heap_call {uint32_t load,call,return_pr,entry_begin,entry_bytes;};
/* Resolve a bounded immutable argument path, including a forward conditional
 * guard only when TST operands prove its decision before the selected target
 * load. The selected pointer load must execute and remain untransformed until
 * JSR samples it; argument preparation in the delay slot is decoded separately.
 * The patched word has one encoded PC-relative consumer and no immutable
 * address-of-slot references; its replacement target cannot feed data operands.
 * Unknown state or multiple complete calls cannot authorize a hook. */
bool kui_vmu_native_heap_call_discover(const void *,size_t,
    const struct kui_vmu_native_heap *,struct kui_vmu_native_heap_call *);
/* Some executables carry the reviewed allocator but never initialize it, so
 * no pointer names its initializer. For such an exact executable (bytes and
 * SHA-256), a reviewed plan names one startup call the installer borrows and
 * an arena that console RAM maps showed the game never writes. The installer
 * then initializes the game's own allocator over that arena before the
 * borrowed call proceeds, and every ordinary lease check applies unchanged.
 * The call record names the borrowed site (load, call, return) and the arena
 * (entry_begin/entry_bytes); hook receives the borrowed literal's address and
 * original target. The site, its single PC-relative consumer and the absence
 * of any immutable address-of-slot word are checked here, as for an
 * initializer call. RAM-map evidence is not a whole-program proof. */
struct kui_vmu_native_heap_fixed_hook {uint32_t slot,target;};
bool kui_vmu_native_heap_fixed_plan(const void *image,size_t n,const uint8_t sha256[32],
    const struct kui_vmu_native_heap *,struct kui_vmu_native_heap_call *,
    struct kui_vmu_native_heap_fixed_hook *hook);

/* The caller must exclude allocator mutation across the entire check; the
 * reader must return cached owner state coherently, without changing it.
 * Every requested address is an aligned U0/P1/P2 main-RAM word. No actual
 * game calls or writes occur in this module. */
typedef bool (*kui_vmu_native_heap_read)(void *,uint32_t,uint32_t *);
struct kui_vmu_native_heap_lease {
    uint32_t version,bytes,phase,initializations;
    struct kui_vmu_native_heap owner;
    uint32_t begin,heap_bytes,lease_bytes,header,payload,units,remaining;
};
/* initializations is an independently maintained, live initializer-entry
 * count. Exactly the first initialization is supported. A caller must keep
 * observing reinitialization; metadata cannot detect an unobserved reset.
 * These checks establish this one allocator lease, never overall lifetime,
 * DMA exclusion, stage placement, or the validity of a worker relocation. */
bool kui_vmu_native_heap_fresh(const struct kui_vmu_native_heap *,
    kui_vmu_native_heap_read,void *,uint32_t begin,uint32_t heap_bytes,
    uint32_t initializations,uint32_t lease_bytes,struct kui_vmu_native_heap_lease *);
bool kui_vmu_native_heap_preallocate(const struct kui_vmu_native_heap_lease *,
    kui_vmu_native_heap_read,void *,uint32_t initializations);
bool kui_vmu_native_heap_postallocate(struct kui_vmu_native_heap_lease *,
    kui_vmu_native_heap_read,void *,uint32_t initializations,uint32_t actual_payload);
/* Reserve the same owned allocation at the arena's bottom without changing
 * its initializer arguments, control identity, or upper allocation boundary.
 * The caller holds IMASK15 throughout all checks and original SDK calls:
 *
 * bottom_fresh -> original allocate(heap_bytes - lease_bytes - 64)
 * -> bottom_placeholder -> original allocate(lease_bytes)
 * -> bottom_allocated -> full lease_valid_with_release(placeholder)
 * -> original release(placeholder) -> bottom_ready.
 *
 * The placeholder payload is begin + lease_bytes + 64. The retained payload
 * is begin + 32, with its original 32-byte allocation header at begin. These
 * functions only read coherent owner state; they never call or modify it.
 * A failed mutating transition invalidates the local lease. No operation may
 * be skipped or retried after refusal. The intermediate phase3 is not an
 * armed/runtime lease; bottom_allocated publishes the ordinary phase2 only
 * after both original allocations and the empty free list are verified.
 * bottom_ready then requires precisely the restored upper free block. */
bool kui_vmu_native_heap_bottom_fresh(const struct kui_vmu_native_heap *,
    kui_vmu_native_heap_read,void *,uint32_t begin,uint32_t heap_bytes,
    uint32_t initializations,uint32_t lease_bytes,struct kui_vmu_native_heap_lease *);
bool kui_vmu_native_heap_bottom_placeholder(struct kui_vmu_native_heap_lease *,
    kui_vmu_native_heap_read,void *,uint32_t initializations,uint32_t actual_placeholder);
bool kui_vmu_native_heap_bottom_allocated(struct kui_vmu_native_heap_lease *,
    kui_vmu_native_heap_read,void *,uint32_t initializations,uint32_t actual_payload);
bool kui_vmu_native_heap_bottom_ready(const struct kui_vmu_native_heap_lease *,
    kui_vmu_native_heap_read,void *,uint32_t initializations);
/* Admit the complete reviewed original allocation/free bodies and decoded
 * literal roles before the bottom transaction calls them. This is an
 * additional cold-code contract; heap discovery/identity alone does not
 * establish the placeholder's bounded metadata-only write footprint. */
bool kui_vmu_native_heap_bottom_code_valid(const struct kui_vmu_native_heap *,
    kui_vmu_native_heap_read,void *);
/* Constant-size identity witness, NOT a free-list ownership proof. This checks
 * the original arena/dispatch identity and allocated lease header without
 * traversing mutable free blocks. A runtime may use it between full proofs
 * only while retaining its independent lifetime contract: the reviewed
 * allocator and its valid allocation/free callers remain unchanged, every
 * reinitializer is intercepted, and every release is admitted against a full
 * coherent free-list walk before its original code runs. Normal allocation
 * only removes/splits existing free blocks; admitted releases cannot extend
 * them into the protected lease. Initial arm and external storage boundaries still
 * require kui_vmu_native_heap_lease_valid. The caller excludes mutation and
 * supplies coherent reads throughout either check. */
bool kui_vmu_native_heap_lease_identity_valid(const struct kui_vmu_native_heap_lease *,
    kui_vmu_native_heap_read,void *,uint32_t initializations);
bool kui_vmu_native_heap_lease_valid(const struct kui_vmu_native_heap_lease *,
    kui_vmu_native_heap_read,void *,uint32_t initializations);
/* One coherent ownership walk with an optional release candidate. A nonzero
 * payload must use the arena's raw alias and its complete allocated chunk must
 * exclude the lease and every current free block. This retains the SDK's
 * ordinary valid-free contract; an invented aligned interior pointer is not
 * proved to have been returned by allocate. NULL payload preserves the lease
 * validator's behavior. On success release_bytes, when nonnull, receives the
 * whole chunk size including its 32-byte header (zero for NULL payload).
 * The caller excludes allocator mutation for the complete call. */
bool kui_vmu_native_heap_lease_valid_with_release(const struct kui_vmu_native_heap_lease *,
    kui_vmu_native_heap_read,void *,uint32_t initializations,uint32_t payload,
    uint32_t *release_bytes);
#endif
