<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE page-table checkpoints

Build **37cfb9369b9b** counted nine accepted PLAY requests with no guard fault,
but all six sound-holder lookups stopped at the block-pointer check. Their
zero outputs did not establish absence of a sound driver. This next observer
records the raw table entries needed to explain those refusals.

## Test

1. With the console off, back up `KUI/apps/games/ce-probe.kui` and merge the
   ZIP's `KUI` folder onto the card. Only that CE loader is installed.
2. Cold boot on SCI microSD. Launch **Original Armada with A** through the
   normal confirmation; leave CD audio off.
3. Stay at the menu for **30 seconds**, then play for about **two minutes**.
4. Return with **A+B+X+Y+Start** and send **one full-screen photograph**.
   Mention any new issue with effects or video. CD music remains disabled.

| Row | Five hexadecimal values, left to right |
| --- | --- |
| `TTB PTEH PLAYS CCR MASK LO` | Translation root, PTEH/ASID sample, accepted PLAY count, cache register, negative-block mask for slots 0–31 |
| `N SLOT BLOCK AT BLOCK ST MASK HI` | Slot0 table pointer, second-level entry address, raw entry value, trace status, negative-block mask for slots 32–63 |
| `R SLOT BLOCK AT BLOCK ST NEG` | Same checkpoint for slot1, followed by the number of slots with stable negative entries |
| `PICK VA SLOT BLOCK AT BLOCK ST` | A selected diagnostic VA, its slot table pointer, entry address, raw value and status |

The sampled offset is `0x01d550ac`; all six sound holders share its second-level
table index. The verified kernel initializes 64 address-space slots. These are
not asserted to be 64 processes. Each mask bit records a stable negative entry
at that offset, including unsupported aliases; it does not identify a sound
owner or grant permission to allocate.

Status **0** means the raw negative entry is an aligned P1 main-RAM pointer.
Status **4** means a zero/positive/special entry or an unsupported block pointer.
Statuses **2/3** refuse the translation root/slot pointer, **7** reports a read
failure, and **8** reports a mapping that changed during sampling. Raw captured
fields are retained even after refusal, so the photograph distinguishes zero
entries from unsupported aliases. A status8 trace is not used in the masks.

`PICK` shows the first stable negative P1 pointer, if one exists; otherwise it
shows the first stable negative pointer in an unsupported domain. With no
negative entry, that row remains zero. No block pointer is dereferenced,
regardless of its status. These values do not prove the published sound heap
is initialized, that its cache values are coherent, or that it is ours to use.

## Scope and verification

The previous exact raw Armada/kernel admission, low-window firmware-vector
checks, bootstrap survival checks and SCI DMA fix remain. The controller
continues to use the existing guarded GD hook stack, with its call graph and
integer-only linked instructions checked. It samples only after accepted
PLAY/PLAY2, under the existing dispatch lock with IRQs masked.

All followed table addresses must be aligned P1 main RAM at/above
`0x8c010000`; P2/P3 aliases, low reserved RAM, G2 and MMIO are refused. Each
trace uses at most four read-only RAM accesses and checks that the two table
entries remained stable. One sample is bounded to 64 traces (256 RAM reads).
It never reads a MEMBLOCK header, page entry, sound holder or sound RAM, calls
CE APIs, changes MMU/cache/timer/interrupt registers, or alters GD replies.
Console overhead is unmeasured. No music or FMV improvement is promised.

`restore-ce/KUI` restores exact DMA-corrected CE build **fd3bde28d538** after
a cold boot, SHA-256
`42e378a7af567c751d9413ef03c6f8509762e90c0001f75a4648a7fba04c4198`.
Your own backup restores the immediately preceding file. Menu/native/MDK2/Toy
payloads are not installed by this archive. Source, host/build/launch evidence,
licenses and checksums are included; private game/kernel/audio bytes are not.
