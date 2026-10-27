<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE Maple mapping test

This isolated CMP2 test checks the live backing of a small Maple code range
and records two independently resolved driver-root values. It remains a
read-only diagnostic. Virtual A2 is unavailable, and this is not a save test.

## Install and test

1. With the console off, back up the current
   `KUI/apps/games/ce-probe.kui` from the SCI SD card to your PC. Keep that
   backup: this package does not include an exact rollback module.
2. Install the test's `KUI/apps/games/ce-probe.kui`. This is the only installed
   file. Keep existing physical VMUs and virtual-card files as they are; do
   not recreate or format a card.
3. Cold boot and select **Original Armada**, using the same original raw GDI.
   Set **Background loading On, CD music Off, virtual VMU Off**.
4. Stay at the game menu for about **30 seconds**, then play for about
   **one minute**. Note whether controls, effects and loading behave normally.
5. Press **A+B+X+Y+Start**. The terminal alternates **DISCOVERY** and
   **MAPPING** every 300 frames, approximately five seconds. Photograph the
   whole screen on **both pages**, including their FAULT rows and the BUILD
   label. Wait for another cycle if either photograph is unclear.
6. Power off after taking both photographs. Restore your backed-up
   `ce-probe.kui` before the next cold boot to return to the previously
   installed module.

## What the pages mean

DISCOVERY retains the module-list and fixed-register observations. The earlier
console photograph showed **4 stable samples, no refusals and 339 reads** in
the last sample. It found MAPLE with attachment mask `8`, while its ROM-backed
MAPLEDEV row was missing. That missing marker was a diagnostic result, not a
crash or proof that MAPLEDEV was absent. Armada's save path uses MAPLEDEV APIs;
an external loaded copy remains a possibility.

MAPPING checks a **32-byte** code fingerprint at Maple RVA `0x5b0c`, with
expected CRC32 **`7bd7ff55`**. It records the code mapping and separately
resolves the current owner's root and the canonical PROCESS0 root at module
RVA `0x72b8`. Their values are reported independently and are never followed.
Matching code bytes establish a sampled identity and backing, not execution
at that address or a callable driver owner.

| MAPPING row | Meaning |
| --- | --- |
| STATUS READS EXPECT CRC MASK | Mapping result, underlying read count, expected and observed CRC, completed-step mask |
| CODE VA SLOT BLOCK PAGE PHYSICAL | Code address and translation witnesses |
| OWNER PROCESS SLOT HANDLE ATTACH ROOT VA | Validated process context and owner-root address |
| ROOT OWNER P1 PAGE VALUE CANON P1 VALUE | Root backing and the two sampled values |
| MAP STATUS CODE OWNER CANON WORD CANON VA | Individual mapping statuses, first code word and canonical-root address |
| FAULT WHY AT VALUE EXPECT DETAIL | Refusal reason and its evidence |

Send both pages even if the CRC or a root check is refused. Partial rows and
their fault witnesses are useful; zero values alone do not establish success.

## Developer scope and evidence

Sampling remains limited to positively accepted GD PLAY/PLAY2 requests, with
a 32-attempt boot-lifetime cap. The proof uses bounded page-table and coherent
mapped-word observations, repeats its context and mapping witnesses, and
retains raw evidence on refusal. Terminal page changes display the authored
snapshots without resampling the guest.

The fingerprint comes from the exact owner-supplied ROM and external Maple
code: this 32-byte range has no PE HIGHLOW relocation and is byte-identical in
both sources. Only identity metadata is distributed; no owner executable
bytes are included. Existing card files are outside this test's scope.

The probe does not follow the root values, inspect a driver extension, follow
a DMA chain, read VMU payloads, install a driver hook, call CE APIs, or write
guest memory or hardware. CE interrupt ownership, VBlank-triggered DMA timing
and extension lifetime remain unresolved. Repeated observations do not grant
an ownership or hook lease, and the native virtual-A2 backend is not connected.
