<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE Maple header test

The previous driver test passed on the console (build `b5bcde4bcf64`). Its
uncached TX/RX mappings and builder cursors allow this next read-only test to
inspect ordinary Maple request and response headers. Virtual A2 remains off.
This test does not read save payloads or change physical/virtual cards.

## Console test

1. With the console off, back up the currently installed
   `KUI/apps/games/ce-probe.kui` to your PC.
2. Copy the package's replacement `KUI/apps/games/ce-probe.kui` onto the SCI SD
   card. It is the only file to install. Do not copy the source/proof folders
   to the console. No card recreation is needed.
3. Cold boot **Original Armada**, using the same original raw GDI, **SCI SD**,
   **Background loading On**, **CD music Off**, **virtual VMU Off**.
4. Keep the normal controller and physical VMU connected. Stay at the menu for
   about 30 seconds, then play for about a minute, as in the preceding test.
   Note any change in controls, effects or loading.
5. Press **A+B+X+Y+Start**. Photograph all three rotating pages, including the
   build label, counts and fault rows: DRIVER, first four HEADERS, remaining
   four HEADERS. Pages change every 300 frames (about five seconds).
6. Power off and restore the backed-up module before the next cold boot.

Send the pages even if a check is refused. A changing RX header can be normal
DMA activity. This build samples only at accepted GD PLAY/PLAY2 calls; visiting
Armada's save menu does not guarantee capture of its VMU traffic. This is not
a save read/write compatibility test.

## Reading the header pages

Both header pages show the same counts and fault rows. Their middle four rows
show descriptors 1–4 and 5–8 respectively. Unused rows remain zero.

| Field | Meaning |
| --- | --- |
| STATUS READS COUNT BYTES MASK | Result, direct header reads, completed descriptors, TX list byte length and completed pass bits |
| OFFSET CONTROL RX BUS REQUEST RESPONSE | Descriptor byte offset, W0, receive destination bus address, request header and response word0 |
| HEADER FAULT WHY AT VALUE EXPECT DETAIL | Refusal reason, address, observed/expected values and descriptor/word detail |

Header status `1` and pass mask `3` mean that both bounded passes matched.
DRIVER status must also be `1`, with mask `1f` and a clear fault row. A failed
final context/mapping/extension witness revokes a preceding header success.
Response `ffffffff` is retained as no response; it is not followed as a pointer.
The source-level status enum and exact row layout are in
`include/kui/armada_ce_maple_headers.h` and `armada_ce_maple_headers_types.h`.

The parser permits at most eight ordinary descriptors, W0 bits `800300ff`,
matching port and payload lengths, and exact agreement with last/end cursors.
It reads only W0/W1/W2 and RX word0. Payload words are skipped. Receive pointers
must be aligned and inside the separately validated 8 KiB RX allocation.
Special/light-gun formats, explicit receive destinations outside that pool,
more than eight descriptors, and mixed cursor address representations are
conservatively refused. These are probe restrictions, not universal CE rules.

## Implementation and verification

The header phase runs after the driver mappings are admitted and before the
existing final extension, mapping, module and process witnesses. It reuses an
explicit union member in the state guard's otherwise unused mapping scratch;
the original proof workspace stays separate. Up to 64 direct P2 word reads
are charged to the existing 2,048-read state budget. The previously tested
50-node state fixture uses 1,709 reads, leaving room for these 64 reads.

The linked controller remains inside `8c004000..8c007c00`; no reservation was
expanded. The native checker audits its embedded bytes, complete cache-reader
instructions, call graph and CE dispatch stack. Ordinary CE/native routes
preprocess identically with this isolated header mode disabled.

Host tests cover all 64 header read refusals, every second-pass header-word
mutation, malformed geometry, unsupported formats, invalid receive pointers,
maximal payload skipping, exact 4 KiB bounds, and the actual production
wrapper's dormant/capped paths and post-capture revocation. Owner-derived
source evidence is reproducible with:

```sh
python3 tools/armada_ce_maple_headers_audit.py \
  --startup-dir /path/to/private/Armada-startup \
  --check docs/evidence/armada-ce-maple-header-contracts.json
make -f Makefile.armada-maple-test host-tests
```

The package includes only our code, metadata, patch and native proof artifacts.
It contains no owner game binaries or disassembly. These observations do not
pin an allocation, establish DMA idleness, acquire IRQ ownership or install a
CE Maple hook. VBlank DMA can run while CPU interrupts are masked. Two matching
header reads establish only an observed consistent header set; the native A2
backend remains disconnected from CE.


## Console result: build `e98e4a5e61e3`

The owner supplied all three terminal pages. The header capture passed both
matching passes (mask `3`), reading four ordinary descriptors in 32 direct
header reads. The list length was `0x34` bytes; descriptor offsets were `00`,
`10`, `1c` and `28`. The DRIVER observation also passed all final witnesses
(mask `1f`) in 1,061 reads, including those 32 header reads. Both fault rows
were clear. The full transcription and decoded fields are retained in
[the console evidence](evidence/armada-ce-maple-console-e98e4a.json).

Port A's main unit received a one-word GETCOND request (`01002009`) and
returned a three-word DATATRF response (`03200008`), consistent with controller
polling. The payload function identifier was not read. Ports B–D received
main-unit DEVINFO queries and retained the driver's `ffffffff` no-response
sentinel. Descriptors five through eight were unused, explaining the zero
rows on the second header page.

The sampled port A source byte `20` advertises an attached-unit mask of zero.
This describes that response only; it does not establish whether a physical
VMU was connected at other times. No A1/A2 request or VMU save operation was
captured. This result validates the ordinary header geometry and repeated
observations while leaving allocation lifetime, IRQ ownership, save-time
sampling and virtual A2 implementation unresolved.
