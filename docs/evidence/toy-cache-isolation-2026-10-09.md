<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Toy SCI: private-state cache isolation

This implements the first cache comparison in
[the CDDA plan](toy-cdda-path-forward-2026-10-09.md).
It is an exact-title experiment. Hardware playback and performance remain
unmeasured for these candidates.

## Controlled profiles

Both candidates use `PRIVATE_P2=1`, `GD_FIXED_STEP=2`, `SHARED_SCI=0`,
`ASYNC_CDDA=0`, and `SCI_REUSE_TDRE=0`. R uses `NATIVE_CACHE=0` and the
retained `0x101` write-through policy. C changes only `NATIVE_CACHE=1` to
retain the admitted original `0x105` copy-back policy. Default build flags
remain private isolation off and native cache off. Native cache without
isolation, and isolation with shared SCI, are refused.

The package derives distinct visible build IDs from the same source commit
and all six profile flags. Its `build.json`, ELF evidence, checksum tree,
and exact source archive identify both results. It also retains the exact
`7b55156aafa2` audio fallback. Follow
[the console comparison checklist](../cdda-toy-cache-test.md).

## Ownership and cache boundaries

- Code and immutable data stay in P1. Writable low-resident and high-worker
  sections, guards, anchors and private stacks use P2 VMAs. Compact LMAs
  and exclusion ranges retain their original physical reservations.
- The existing stage writes back and invalidates all RAM cache lines before
  low P2 initialization. The worker installer publishes its complete owned
  range, including stacks, before P2 initialization; verification also uses
  P2 so it cannot refill an initialized-data alias.
- Live authored saves on inherited native stacks are published over their
  bounded cache-line intersections before native calls or unmasking.
  The native calls retain their original SP and exact inherited SR.
- Two low frames need no publication: the heap guard's four-byte SR word
  is popped while BL and IMASK15 still hold; the 36-byte GD caller frame is
  fully popped before restoring SR. This exception is limited to the
  synchronous profile whose dispatch has no native SDK/cache callback.
- Mandatory revocation remains direct under masking, even if a worker is
  suspended. Its transient C frame dies before restoration. Ordinary worker
  calls use the protected P2 stack and retain the existing reentrancy gate.
- Authored writes to native SH-RAM sound producer, command counter,
  allocation records and free count are published inside their existing
  masked transactions. Adjacent native dirty bytes are written back too.
- SDK CHECK/REQ_STAT capabilities require the exact current P2 caller SP,
  PR, direction and 16-byte span. The existing GD core canonicalizes the
  parameter before the physical exclusion mapper. No broader guest access
  is admitted.

The low reservation still ends at physical `0x8c007800`; its stack still
ends at `0x8c007d00`. The high worker keeps its 8,192-byte guarded audio
stack inside `0x8cfd0000..0x8cfe0000`. The linked audits check actual
runtime aliases separately from physical ownership.

## Verification scope

Host tests cover alias admission, invalid profiles, cache-discard injection,
native metadata publication order, neighboring dirty bytes, lifecycle,
sample ownership and real P2 GD scratch calls. Linked audits check VMA/LMA,
imports/FPU instructions, frame publication, restoration, native scratch,
cache-policy admission and conservative stack budgets. Negative mutations
must be rejected. These checks constrain authored code; they do not emulate
Dreamcast interrupt timing or establish smooth video/audio.

The retained worker also passes all 72 cases in the existing synthetic
cadence sweep with ASan/UBSan and no lost baseline cases. That host sweep
uses the existing timing model; it does not measure P2 memory-access costs
or the native cache policy on hardware.

SCI wire code, read protocol, two-sector GD cadence, ring geometry, ARM
firmware and AICA PIO transfers remain the comparison's controls. Cache
ownership and publication are independent of the transport. A later G1
implementation can reuse those contracts while supplying its own physical
DMA, completion and bus-release rules. This change does not admit a G1
transfer owner or an AICA DMA lease.
