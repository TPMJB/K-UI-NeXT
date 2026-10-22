# Native VMU runtime cost and bounded peripheral queries

These host measurements compare the hardware-tested cached-code baseline
`2e5314b` with the next native worker. They count actual guest RAM/MMIO reader
callbacks in production C, excluding fixture setup and test inspection reads.
They measure work, not SH4 cycles or frame rate. Power Stone 2 hardware results
from the baseline do not establish another title's performance or compatibility.

| Workload | Baseline reads | New reads |
| --- | ---: | ---: |
| Steady two-controller pair, one free heap node | 450 | 344 |
| Steady four-controller pair, one free heap node | 476 | 366 |
| Two controllers plus virtual INFO | 458 | 350 |
| Two controllers plus virtual block READ, including SD boundary proofs | 880 | 766 |
| Two-controller pair, 64 free heap nodes | 702 | 596 |
| First READ with five descriptors and 251 padding NOPs | 2,936 | 1,861 |
| One unrelated release, 256 free heap nodes | 1,056 | 543 |
| 64 unrelated releases, 256 free heap nodes | 67,584 | 34,752 |

The first two-controller pair costs 395 reads because it constructs the
launch-local prepared geometry once. It is never loaded from the address cache.
Steady four-controller work is `362 + 4 * free_nodes` reads per pair; unrelated
releases cost `31 + 2 * free_nodes` each. Heap fragmentation and free frequency
therefore still affect performance. The maximum list remains bounded at 1,024
walk entries. Malformed, overlapping, mixed-alias and unowned release inputs
remain refused.

The prepared geometry lives in final worker BSS and pins its own address. Its
preparation calls the existing full layout binder and runtime validator. Later
submit/completion checks retain both samples of initializer inputs, all 48 live
RX entries, manager/lock pointers, callbacks, bank selectors, SR/PR and initial
and final Maple MMIO samples. Static allocation/provenance intersections are
proved once rather than repeated every poll. In the variable layout, this also
removes 276 repeated RX pair intersections and 66 initializer-buffer pair
intersections per boundary. The combined boundary reader cost is 149 to 98.
Uniform layouts retain their two original scalar initializer literals and all
48 exact RX entries.

The full original layout/runtime/heap/hook/TX/filtered-chain checks still run
immediately before acquisition and after restoration of every external SCI
loan. The exact original TX capture is compared at completion and at these
external boundaries. The persistent path reuses the descriptor control word
already read under uninterrupted IMASK15 and skips recomputing a reporting hash
after every word has compared equal. Passive and bounded read-only chain paths
retain their previous behavior.

The storage oracle still sees two sector reads on the first virtual block
READ, one on subsequent reads, and a final save SYNC with one readback, four
sector writes and eight status checks. No optimization removes journal,
identity, CRC, readback or release checks. Invalid SD-boundary mutations still
prevent a virtual ACK and RX publication.

The new query scope is deliberately bounded:

| Request | Behavior |
| --- | --- |
| Virtual A1 RESET, no payload | Discard unfinished RAM transaction; exact four-byte OK; no SD access |
| Other physical one-hot subunit INFO / ALLINFO | Keep physical descriptor and response; verify endpoint, exact size and nonzero function bitmap |
| Other physical subunit GETCOND | One-word controller, memory-card, LCD, clock or rumble function only; verify exact function echo and bounded DATA response, zero-word AGAIN/BADCMD/BADFUNC, or timeout |
| Physical writes, SETCOND, RESET/KILL, storage MEDIA/READ | Refuse before DMA |
| Virtual KILL | Deferred; killed-silence and parent-advertisement lifetime need coordinated handling |
| A batch without the real port-A parent, or an all-virtual batch | Deferred; existing asynchronous DMA/consumer contract is unchanged |

Physical A1 remains filtered and a real A1 conflict still refuses the entire
virtual publication. Other physical replies remain byte-identical. The
function masks use the existing project's pinned KOS Maple declarations; no
SDK implementation or opcode source was imported.

Validation includes the production persistent worker/SPI model, 256 sustained
block reads, save/reopen recovery, both bank/layout families, all three RAM
aliases, per-live-read failure/value mutations, wrong peripheral reply shapes,
maximum padding, varied descriptor/free counts and a frozen two-walk release
reference. See `tests/test_vmu_native_live.c`,
`tests/test_vmu_native_peripherals.c`, `tests/test_vmu_native_stress.c` and
`tests/test_vmu_native_lifetime_stress.c`. These checks do not replace a real
save, power cycle and reload on each newly admitted title.
