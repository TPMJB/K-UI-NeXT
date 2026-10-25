<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native PIO-pair compatibility expansion

The next compatibility candidate now implements the reviewed Evolution 1
route and proves its enlarged lease. Its console acceptance remains pending.
Use [the current test sheet](native-reader-compatibility-test-sheet.md) for
the two delivered variants and all33result rows. The chronology below records
the successful d0aac performance checkpoint and the gates used for expansion.

The console observation in 63371.mp4 makes **d0aac421584c** the successful
Evolution 2 performance checkpoint. Preserve the delivered module SHA256
`f67bd688d0111733f8213bc3082387c6e4bde1c0ad41043219197df6967ac94c`.
The observation and the user's single reported hiccup are recorded in
[evidence/native-read-ahead-pio-pair-observed-2026-10-25.json](evidence/native-read-ahead-pio-pair-observed-2026-10-25.json).

The measured second-block path ran 1,113 times. There were 2,230 checked
fills, zero cache-miss delivery visits, zero abandonments and zero measured
soft-window overruns. The largest whole producer visit was 2.87488ms.
This is a successful diagnostic run, not a measured worst-case bound.

## Separate performance policy from compatibility

Keep the successful two-block admission, CRC validation, demand transport,
SCI priority1 and existing repair limits while adding supported layouts.
The d0aac performance-checkpoint binary admits the verified Evolution 2 identity only.
Changing or omitting `--game` does not authorize it for another title.

The generic virtual-A2 Standard-reader route already supports independent
cold SDK-family discovery. It is separate from SCI Background read-ahead.
A successful generic A2 boot or old title-specific save test does not prove
the new interrupt route or enlarged read-ahead lease.

| Step | Games | Required evidence |
| --- | --- | --- |
| 1 | Evolution 2 | Retain exact successful binary; longer ordinary play and save/power-cycle checks |
| 2 | Evolution 1 | Prove its interrupt route and 384KiB allocation/placement before building a candidate |
| 3 | Grandia II, Sonic Adventure 2, Power Stone 2, Skies of Arcadia | Review each original SDK layout and lease, then repeat the same console checks |
| 4 | Other native families | Extend structural discovery from reviewed families; record unsupported-family refusals |
| Separate | Windows CE and CDDA | Preserve existing implementations and validate separately |

## Minimum Evolution 1 implementation gate

The original Evolution 1 executable is already available locally for analysis.
Its earlier allocator/transfer fixtures cover a 192KiB lease. They do not
establish the 384KiB read-ahead lease.

Before enabling a new title, establish:

- The original vector, common interrupt handler, default ERI/RXI callback
  slots and the live copied vector's ownership.
- The RTS callback ABI, preserved registers and restoration of the original
  SDK interrupt frame on both receive and error routes.
- The enlarged lease through the original allocator and actual stage
  placement, with existing overlap, destination and lifetime checks.
- Negative cases for altered code, mutable callback slots, unknown layout,
  lost ownership, allocation failure and insufficient lifetime.
- Host and linked-instruction checks, clean build and installer checks.

Both the static admission in `src/core/native_sdk_sci_route.c` and the live
binding/readiness in the resident must consume a reviewed layout plan.
Adding a game hash alone is insufficient. Use the second reviewed family
to separate the proven structural rules from Evolution 2's addresses.

## Console checks for each admitted image

Record exact title/revision, image format, build, reader, speed and encoding.
Check FMVs, in-engine scenes, sustained movement, scene/area changes and
loading transitions. Check a physical A1 with virtual A2, load an existing
save, make a new save, power off fully, and reload it.

Each timing comparison uses `--profile-runtime` and one complete report cycle
from **CRC VALIDATED PHYSICAL READ AHEAD** until that heading returns.
Keep the diagnostic run separate from longer ordinary save/persistence play;
the timing run intentionally stops after approximately20seconds.

Promotion to a standard native reader requires repeated console success
across SDK families and access patterns, including saves and power cycles.
A single successful Evolution 2 capture does not establish other games,
other media transports or all SD-card timing behavior.
