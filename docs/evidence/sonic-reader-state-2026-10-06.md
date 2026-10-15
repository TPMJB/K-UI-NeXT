# Sonic reader-state diagnostic

## Evidence and purpose

The `0adde074c45e` hardware test reached the return from Sonic's scoped startup routine. Its private stack bounds/canary, first asset size check and first two audio waits passed. The strict snapshot stopped at `0x8c00af64`; see [the photographed values](sonic-scoped-stack-2026-10-06.md).

That address has different meanings in the shipped standard readers:

| Transport | First changed address maps to | Why more evidence is needed |
| --- | --- | --- |
| SCI | `service.step` | The adapter updates this budget during EXEC activity. The old/new value and accompanying state changes distinguish normal updates from damage. |
| SCIF | `image.block[228]` | Earlier service counters and cache metadata matched. An ordinary cache refill would have changed earlier counters, so a cache-only change is unexplained. |
| IDE | `image.block[484]` | Cache data requires the same distinction between valid reads, incoherent observations and damage. |

All three shipped code/RO ranges end before the first mismatch, so reader instructions stayed intact in this run. The original comparison stopped at the first changed byte; it did not establish the extent of later changes or manifest integrity. No data guard is relaxed based on this result.

## Diagnostic behavior

This retains the exact-owner gate, four one-shot RAM hooks, scoped private stack and strict whole-snapshot comparison from the preceding candidate. Successful paths continue as before. A changed resident byte still stops before returning to the owner caller.

On a snapshot mismatch, the diagnostic now scans the entire captured range, recording the first and last changed address and the number of changed bytes. It captures the original aligned word, the coherent P1 word and the uncached P2 word before any reporting cache purge. It reports the selected storage transport and reader. Separate code/IP-prefix CRCs cover `0x8c008000` through `RESIDENT_ADDRESS + resident_bytes`; full snapshot CRCs remain available.

The mismatch screen uses the existing five value rows: CPU, stack, transport/change extent, code/full CRCs and aligned words. It preserves the first changed address in `DETAIL`. Other stack, asset and audio-wait failures retain their existing report rows. No reader state is restored or discarded, and no immutable manifest search or new resident ABI is introduced.

`TYPE` is the storage enum (`0` SCIF, `1` SCI, `2` IDE). `READER` is `0` standard, `1` background or `2` eager background. Displayed numeric values are hexadecimal. P1/P2 differences can reflect cache coherency; they do not establish which observed value is correct. The checked snapshot remains authoritative for stopping this diagnostic.

The snapshot code-end bounds are validated before reading. Original image files, owner instruction bytes and disc sectors remain outside source and deliverables. Ordinary modes and Windows CE exclude this diagnostic. A later hang without an observed guard failure remains outside its reporting coverage.

## Console test

Replace the whole `KUI` folder on the SD card and restart using the existing compatible boot disc. Launch the same original Sonic GDI with the standard reader. Photograph the next diagnostic screen in full. The transport row is sufficient; no separate transport-selection answer is required.

This build adds evidence to the existing candidate. It does not claim the data mutation or Sonic gameplay is fixed.

## Validation

All 44 focused UBSan cases pass, including changes at multiple scattered bytes, the entire snapshot, the IP prefix and reader code; first/last/count and original/P1/P2 words; transport/reader labels; preserved code CRCs for data-only changes; changed code CRCs for prefix changes; and invalid prefix bounds. Independent C review and whitespace validation pass.

Strict SH-4 compilation/linking and an 8,471-instruction audit pass. Ordinary native trace modes 0–3 and CE modes 0/3 match parent allocated sections and relocations exactly; scoped assembly also matches the preceding candidate. Bookkeeping stack `0x8ce145c0..0x8ce155c0` and owner stack `0x8ce155c0..0x8ce1d5c0` remain disjoint. BSS ends at `0x8ce4a728`, below `0x8cf00000`. Checkpoint/return/report C frames are 56/36/124 bytes, with a maximum report call path of 304 bytes inside the 4 KiB bookkeeping stack. The report still fits the original 16 lines.

The delivered archive records its exact GitHub build and checksum checks. Hardware interpretation remains pending the next owner photo.

## Owner hardware result: `37c7815d8c41`

The owner supplied the next photograph on 2026-10-06. The selected transport was SCI (`1`), using the standard reader (`0`). The scoped routine again reached its return with stack bounds/canary intact and all hook windows restored.

| Field | Photographed value |
| --- | --- |
| Point, frame, SR, PR, CCR | `00000004`, `8ce1d56c`, `60000101`, `ace00358`, `00000105` |
| Private stack low/high, original SP, active/completed | `8ce155c0`, `8ce1d5c0`, `8c00f3a4`, `00000001`, `00000000` |
| Transport, reader, changed bytes, last change, restored mask | `00000001`, `00000000`, `00000005`, `8c00babc`, `0000000f` |
| Code end, old/new code CRC, old/new full CRC | `8c00ae80`, `51113e0f`, `51113e0f`, `9da03c11`, `262f0b28` |
| Aligned word, old, coherent P1, uncached P2, G2 | `8c00af68`, `0000014f`, `00000153`, `00000151`, `0000000e` |
| First changed address | `8c00af68` |

The exact shipped SCI reader was matched against its local ELF after replacing only the build identifier. `0x8c00af68` is `service.diag.calls`; the coherent value records four more dispatches than the snapshot. The uncached value lags by two calls while the counter is dirty in P1 cache. `0x8c00babc` is `pace.still`, a mutable video-pacing counter. Both are normal reader activity. The unchanged prefix CRC confirms the observed reader code/RO bytes still matched.

This exposes an overly strict diagnostic policy: whole-BSS equality rejects normal GD dispatch/pacing updates. The next candidate preserves the scoped stack and checks immutable code and the exact resident manifest, allowing mutable reader state to evolve. This does not assume that unreported manifest bytes stayed intact; the next guard verifies them explicitly. See [the boot continuation test](sonic-boot-continuation-2026-10-06.md).
