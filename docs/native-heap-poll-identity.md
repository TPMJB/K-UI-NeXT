# Native heap identity between storage boundaries

The ordinary controller path uses a constant-size heap identity witness after
validating the installed initializer and release hooks. This is separate from
the complete free-list proof, which remains mandatory at arm, every original
release, and before and after every external SCI storage loan. The identity
witness is not sufficient to admit a release or a storage transaction.

The reviewed tail32 allocator obtains the worker lease from the end of its
arena. Its allocated header and payload remain at the same raw U0/P1/P2 alias
returned by the allocator. The lifetime argument is inductive:

1. Postallocation and arm perform a coherent complete ownership proof. No
   current free block overlaps the lease, including its allocated header.
2. Normal allocation removes a free block or splits an existing free block.
   It cannot extend any free interval, and so cannot reach the allocated lease.
3. Every original release is intercepted before its original code runs. The
   release admission still performs a complete coherent walk, rejects release
   chunks that overlap the lease or SDK-owned protected ranges, and rejects
   chunks overlapping current free blocks. Coalescing the admitted chunk with
   adjacent free chunks preserves the lease exclusion.
4. Every subsequent initializer entry, direct or indirect, stops before it
   can reset the arena. The original initializer entry is patched in addition
   to the startup pointer cell. Supported terminal entries also stop rather
   than resuming an unobserved lifetime.
5. Each controller boundary freshly checks these installed hooks, the original
   heap arena/dispatch identity and the lease's allocated size word. DMA/RX
   geometry, live controller context and physical-chain checks remain separate
   and are still performed. A scoped storage loan continues to renew the full
   ownership proof independently before and after the external call.

This relies on the existing reviewed SDK/caller contract: the allocator code
remains the reviewed implementation; allocations enter that implementation;
ordinary release callers supply actual allocated pointers; and there are no
unobserved writes into allocator control or hook code. Even the complete
release validator cannot prove that an invented aligned interior pointer was
returned by allocate. The identity witness is deliberately not advertised as
a generic detector for arbitrary free-list corruption outside that contract.
Such corruption is still rejected by the complete release/storage checks, but
it no longer causes an unrelated controller-only poll to walk the entire
list. It must not be reused for a new transport or lifetime model without
establishing the same preconditions.

`tests/test_vmu_native_lifetime.c` runs 1,024 allocation/release steps per raw
alias using an independent interval model, including splits, insertion,
coalescing, link changes and changing cursor positions. Both validators must
accept every resulting valid state and the complete release validator must
admit every tracked chunk before release. It also exercises every bit of all eight live
identity words, malformed lease geometry, raw aliases, independently observed
initialization counts and every read-failure position. It also explicitly
tests the limitation: corrupt free lists can preserve the identity witness,
while the full validator rejects them. The bounded witness uses exactly eight
guest memory reads for 1, 8, 64, 256 and 512 free blocks. Full ownership uses
`10 + 2 * free_blocks` reads, so replacing two controller-boundary walks saves
`4 + 4 * free_blocks` reads per poll pair without reducing release/storage
validation.

Reader counts describe algorithmic work, not SH-4 time or hardware performance.
Save/load and controller latency still need hardware confirmation for each
new release.
