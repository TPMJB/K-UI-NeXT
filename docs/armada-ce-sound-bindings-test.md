<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE sound bindings observer

The previous console result, build **b2aadf6bb4e8**, confirmed eight accepted
PLAY requests for track 4 with repeat field 15. This next observer resolves
CE's live sound-heap and mapped sound-RAM holder addresses when those requests
arrive. It retains the SCI DMA correctness fix. It does not implement music,
allocate anything, call CE sound APIs or change GD replies.

## Install and test

1. With the console off, back up `KUI/apps/games/ce-probe.kui`, then merge this
   ZIP's `KUI` folder onto the card. Only that CE loader file is installed.
2. Cold boot on SCI microSD, select **Original Armada**, and launch with **A**
   through normal confirmation. Leave CD audio off.
3. Remain in the main menu for about **30 seconds**, then play for about
   **two minutes**. Note any new problem with effects or FMVs. No CD music is
   expected and this is not an FMV performance update.
4. Return with **A+B+X+Y+Start**. Send **one photograph of the complete return
   screen**, including BUILD, GUARD and the four new rows.

The new values are hexadecimal. `NH` means nominal sound-heap holder;
`RH` means the holder at the ROM o32 real VA. `NS` and `RS` are the respective
mapped sound-RAM holders. Their values are pointers, not memory reservations.

| Row | Five values, left to right |
| --- | --- |
| `TTB PTEH PLAYS STATUS FIRST TTB` | Current translation root, PTEH/ASID sample, accepted PLAY count, eight packed status digits, first translation root |
| `NH P1 VA OBJ REF FIRST VA` | Physical holder, sampled heap pointer, resolved heap-object physical address, sound reference count, first sampled heap pointer |
| `RH P1 VA OBJ REF FIRST VA` | Same fields for the o32 real holder |
| `NS P1 NS VA RS P1 RS VA CCR` | Nominal physical holder/value, real physical holder/value, sampled cache-control register |

`STATUS` packs eight hexadecimal digits. From **right to left** they describe
nominal heap-holder read, nominal heap-object mapping, nominal reference read,
real heap-holder read, real heap-object mapping, real reference read, nominal
sound-RAM holder read and real sound-RAM holder read.

| Digit | Interpretation |
| --- | --- |
| 0 | Admitted coherent read or direct main-RAM object pointer |
| 1 | Invalid/null pointer or argument; object is not followed |
| 2–6 | Translation root, slot, block, page or destination was refused |
| 7 | Bounded RAM read failed |
| 8 | Mapping changed during sampling; no result published |
| 9 | Mapping resolved; holder value deliberately skipped because cache coherence was not established |

A zero value with status **9** is unknown, not proof the driver is absent.
First values are retained from the first PLAY, including skipped values.
No PLAY means the report remains zero. Matching first/latest TTB does not
prove the same process: CE can change the active slot mapping within a root.
A nonzero heap/refcount does not establish ownership or permission to allocate.

## Placement and safety boundaries

The exact original US raw Armada admission, complete kernel SHA-256 and CE
interrupt bindings from the prior observer are retained. No other image is
admitted. Only the separate SCI-background candidate calls the controller.
Normal/native/MDK2/Toy/menu payloads are not installed by this package.

The additional controller occupies a bounded window at `0x8c004000` below
IP metadata; code and BSS must end before `0x8c007c00`. Retained BIOS vector
entries are checked below this window (or in ROM where allowed) before its
first write. After bootstrap 2, both its immutable code and cleared state
are checked before CE entry. A changed window stops instead of being repaired.
It uses the existing guarded GD hook stack, with both call graphs audited.

Only accepted PLAY/PLAY2 requests trigger samples, inside the existing GD
dispatch lock with IRQs masked and the caller's CE mapping retained. The
controller reads TTB, PTEH and CCR, but never writes them. All table and holder
accesses are checked word-aligned main RAM at/above `0x8c010000`, excluding
low firmware/residents, sound RAM, G2 and MMIO. Mapping entries are rechecked
before publishing. Table reads follow the audited kernel's physical walk.

Holder reads require normal cache mode `(CCR & 0xa1) == 1`, a cached page and
matching virtual/physical cache-index colors. Otherwise the controller records
the mapping with status 9 and skips the value. It never purges CE cache lines,
creates an uncached holder alias or follows the published sound-RAM pointer.
There are at most eight bounded bindings per accepted PLAY, each performing
at most seven RAM reads; no storage, timer, allocator or audio access is added.
Console overhead remains unmeasured.

## Restore and verification

Merge `restore-ce/KUI` and cold boot to restore exact DMA-corrected CE build
**fd3bde28d538**, SHA-256
`42e378a7af567c751d9413ef03c6f8509762e90c0001f75a4648a7fba04c4198`.
Your backup restores whichever CE file was installed before this test.

The bounded translation/cache helper has 90 host cases covering malformed
and missing mappings, both CE slots, cache-mode/color refusal, mapping races
and read failures. The package includes committed source, metadata audits,
layout/instruction/stack and production-launch gate evidence, dependency
sources/licenses and `SHA256SUMS`. No private executable, kernel, ARM driver,
music or startup-bundle bytes are distributed. Console acceptance is pending.

The owner's subsequent build37cfb9369b9b photograph counted nine accepted
PLAY requests with guard0, TTB8c1458a0 and CCR105. All six holder lookups
returned BLOCK4; no page or holder was read. The two object lookups returned
ARGUMENT1 because no heap value was available. Thus the raw table entries,
not sound initialization, are the next unresolved observation. See
`evidence/armada-ce-bind-console-2026-10-14.json` and the page-table test.
