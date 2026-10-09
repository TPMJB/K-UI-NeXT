# Separate SCI mapper-cache prototype

Base: `56df229`. This experiment is separate from the eight-block-ring
candidate and is absent from every resident/worker/package build input.
It changes no ordinary 1.8.5 reader, Start/menu behavior, SCI instruction,
IRQ integration, raw-sector callback or GD dispatch limit.

The current synchronous callback acquires the shared physical bus, streams
the validated request with CMD18, stops with CMD12 and restores the previous
SCI owner before returning. Keeping CMD18 live across returns would also
require retaining the shared SCI lease across restored-SR intervals. No such
native-owner exclusion/session contract is established, so this prototype
preserves every acquisition, stop, deselection and release boundary.

The existing mapper keeps one CRC-checked 512-byte physical block. Adjacent
2352-byte audio sectors usually share their boundary block. An intervening
GD data read replaces that cache, causing the next audio callback to reread
the shared block. The prototype keeps one trailing block per logical
audio/data owner, restoring the appropriate tail before invoking the exact
existing mapper and CRC-checking SD protocol. It fetches no extra blocks,
batches no audio sectors and uses no persistent physical lease.

Each tail has a physical LBA and validity tag. The cache binds an image,
immutable manifest, image callback/context identity and explicit lifecycle
epoch. Transport callback/context identity binds on first use. A stale epoch
or changed identity is rejected before acquisition and cannot silently rebind.
Reset clears both tails and the existing mapper cache. A callback snapshots
its transport descriptor so cleanup releases the exact owner it acquired.
It commits a tail only after the mapper succeeds, stream cleanup succeeds,
release returns and the lifecycle revision still matches. Acquisition,
read, header/mode or cleanup failure invalidates both tails. Reset during
acquire/read/stop/release causes the in-flight result to be refused and prevents
old-epoch cache publication; it does not asynchronously cancel a synchronous
transfer already in progress.

As in the existing resident, acquisition failure means the transport left no
new lease or borrowed controls owned by the caller. The cache does not call
release after failed acquisition, since that could release another owner's
lease. Failure cleanup/restoration belongs to the transport; the cache layer
does not independently prove it.

## Reproduction and physical read counts

Prototype checkpoint: `62835ff84a570dfa02824ab34af6b7e030f682c8`, based on
`56df22966bbb9093374372f8dad7a2e09a05fcb7`. The eight-block ZIP includes its
complete source patch as `experiments/sci-interleave-cache.patch`. Apply it
to a separate copy of the included source snapshot, excluding this evidence
document because the snapshot already contains it:

```sh
git apply --exclude=docs/evidence/cdda-sci-interleave-cache-prototype-2026-10-08.md /path/to/experiments/sci-interleave-cache.patch
python3 tests/test_retail_interleave_cache.py
```

Alternatively, from the exact prototype checkout:

```sh
python3 tests/test_retail_interleave_cache.py
```

The script compiles and runs six strict-warning ASan/UBSan configurations:
the prototype with serial and folded CRC implementations, the unchanged
mapper, unchanged SD protocol, unchanged ordinary SCI bus, and the pilot's
unchanged receive-paced SCI diagnostic bus with TDRE reuse disabled.
The prototype fixture alternates 32 one-sector audio callbacks and 32
two-sector cooked-data callbacks. It compares every logical output byte
against independently authored source bytes and checks output guards.

| Zero-offset fixture | Baseline | Separate tails |
|---|---:|---:|
| Audio physical 512-byte blocks | 178 | 147 |
| Data physical 512-byte blocks | 256 | 256 |
| Total physical 512-byte blocks | 434 | 403 |
| CMD18 starts, contiguous extents | 64 | 64 |
| CMD18 starts, three-block fragmented extents | 187 | 177 |
| Acquisitions / cleanup calls / releases | 64 / 64 / 64 | 64 / 64 / 64 |

In this synthetic interleave, audio block reads fall by 31/178 (approximately
17.42%) and total block reads by 31/434 (approximately 7.14%). Fragmented
extents also need ten fewer CMD18 starts. These are exact physical/protocol
counts in a host model, not hardware elapsed-time or audible-continuity gains.

Tests also cover all 512 container offsets; aligned and unaligned outputs;
contiguous/fragmented extents; raw/cooked data tracks; scalar and bulk SD
payload paths; repeated source wraps; non-contiguous seeks; same-owner reads;
token, CRC, partial-payload, acquisition, mode/header and stop faults;
recursive entry; lifecycle reset at four callback boundaries; image/map/
callback/context replacement; transport identity mismatch; descriptor mutation;
stale epochs; natural revision/epoch wrap; terminal unbind; and preflight
rejections without bus traffic. Same-owner sequential/wrapped workloads retain
the baseline physical read count. The actual SD reader supplies CRC validation,
bounded request/extent runs and CMD12 cleanup; the new helper does not replace
those protocol rules. The separate unchanged SCI-bus regression checks its
byte reversal, CRC, pacing, alignment, error handling and ownership restoration.

## Integration limits

The new cache contains 1024 bytes of saved physical payload plus tags/bindings
and alignment padding (1184 bytes on the host ABI, 1152 bytes on SH-4). The helper performs two
512-byte SH-memory copies per successful callback when a tail is restored.
Native code size, BSS placement, stack depth and this copy cost require a
linked resident/worker audit and console measurement before integration.
The current low resident ends at `0x8c0077e8`, only 24 bytes below its
`0x8c007800` owner limit. This additional cache cannot simply be placed in
that resident. Future integration must explicitly bind leased worker storage
or establish enough freed resident space and repeat the layout/stack audit.
An isolated SH-4 GCC 15.2 `-Os -m4-single-only -ffreestanding -fno-builtin`
compile reports 708 bytes of object text, a 76-byte local read frame and a
20-byte local reset frame. Those local frames exclude mapper, protocol and
transport callback depth and are not a linked stack bound.

The immutable-card/map requirement is unchanged. Hot replacement or source
modification must explicitly reset/rebind the cache with a new lifecycle epoch.
A same-address physical card replacement cannot be inferred from a physical
LBA tag. The caller must retain existing SR, resident lock, data priority,
generation, driver and STOP admission; this helper supplies none of those
permissions and cannot authorize a raw read in a forbidden context.

Benefits depend on actual interleaving and source alignment. A continuously
serviced audio owner already enjoys the original tail cache, so this experiment
does not reduce its physical read count. It does not shorten command setup or
SCI ownership gaps for contiguous extents, solve sparse service opportunities,
refresh cursor proofs or prove the hardware's maximum throughput. A persistent
or asynchronous mixed audio/data stream remains a separate shared-owner
architecture change.

Only the excluded prototype and reproducible host tests are prepared here.
No runtime using this cache has been console-linked, packaged or tested.
