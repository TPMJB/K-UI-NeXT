# Armada CE cache snapshot performance candidate

The 433 controls establish a performance regression even when the virtual VMU
does not publish any replies. Gate-only was better than full A2 but worse than
the user's baseline. Proof-only progressively stalled video and repeated audio.
Both reached their planned 1,200-completion terminal report: reason 6, phase 2.
Proof-only completed 1,199 initial/final ownership queries, with equal counters;
all 1,200 DMA fence leases restored and no recorded fault. See the two
`evidence/armada-433-*-console.json` transcriptions.

These observations implicate time spent in the common sampler/fence and the
additional ownership checks. They do not prove a DMA restoration failure, a
backend failure, or an audio implementation fault. The working theory is that
expensive inspection while masked and running from P2 delays time-sensitive CE
work. Repeating audio is consistent with delayed production/refill, but neither
masked duration nor an underrun has been measured. The terminal report itself
deliberately stops normal execution; the game continuing to repeat audio during
that report is not evidence of a second unplanned crash.

## Candidate change

The common sampler previously inspected ten metadata words separately in each
of two independent passes. Each word used four tag reads, data, four tag reads,
and repeated data. These fields normally occupy four 32-byte lines. Group only
the requested words on each line, sharing both tag scans while retaining both
data reads. Support all eight possible word alignments of the extension.

A bounded assembly leaf now performs each selected line snapshot directly in
P2 without callback dispatch. All C code, staging and stack remain in P2. The
leaf admits the exact linked 60-byte private job, normal live CCR profile,
IMASK15 and matching VA/P1/PTE geometry. It refuses dirty alternate aliases,
changed tags or changed selected data. It changes no cache state, SR mode/mask
or guest memory; results copy out only after success. Its complete 488 bytes,
including its one relocatable job literal, are checked against a byte contract.
The 416-byte instruction portion contains no callback. Array-reading exits
retain eight terminal NOPs before a possible return to cached execution.

The native RX query also uses this leaf. It now repeats selected data after the
second tag scan, strengthening the prior RX line witness. Page-table walks,
allocation/context witnesses, descriptor witnesses and final query comparison
remain outside and around each fresh snapshot. No cached source survives a
table walk. The original interrupt mask, MDEN disable/restore sequence, CE
handler and reservation are unchanged.

| Representative operation | 433 | Candidate |
| --- | ---: | ---: |
| Common metadata, both passes: budgeted loads | 200 | 104 |
| Four-record common sampler: budgeted loads | 291 | 195 |
| Four-record sampler: scalar read callbacks | 291 | 91 plus 8 bulk leaf calls |
| RX ownership query fixture: budgeted loads | 264 | 330 |
| RX query fixture: scalar read callbacks | 264 | 102 plus 12 bulk leaf calls |

These are source-fixture counts, not CPU cycles, total memory instructions or
measured FPS. The RX load increase is deliberate repeated-data validation;
callback overhead still falls. Successful bulk snapshots charge the original
read budget for every selected data/tag load. Refused snapshots conservatively
charge their planned maximum. Bounds, first-error details and zero-on-refusal
outputs are covered by tests using the extracted production C wrapper.

## Verification and limits

- ASAN/UBSAN host suites pass, including 24,377 masked-line checks, 83,019
  sampler checks and 53,550 actual-wrapper scenarios / 871,429 checks.
- The SH instruction model passes 5,705 checks; all 488 one-byte changes to
  the pinned leaf are rejected, and five removed semantic guards are detected.
- Integrated helper instruction, callback, layout and stack audits pass:
  18,688 bytes code, 3,284 bytes BSS, 556 bytes free before the private stack.
  Worst retained C call path is 968 bytes within the 1,536-byte stack budget.
- Ordinary native, CE async, CE audio observer and CE stage preprocessed outputs
  remain identical with this isolated experiment disabled; see the evidence.

Hardware performance is pending. Do not treat this as a writable VMU release.
The normal 140-byte WRITE phase still hits the read-only request guard. There
is no SD virtual-save I/O here. Full write staging/commit, durable storage,
automatic adapter detection and a resident physical GD-ROM bootstrap remain
separate implementation work. No ordinary release branch has been updated.

## Next comparison

Use only the new `01-gate-only` module first. Keep the Armada image, settings
and physical A1 unchanged. Back up `KUI/apps/games/ce-probe.kui`, replace that
one module and cold boot. Let the FMV run until either its behavior is clear or
the automatic report appears; skip it if it becomes unusable. Record whether
it approaches the known baseline and photograph the five terminal pages.
No A2 save selection is expected from gate-only.

Expected report: new build ID, MODE 1, FIRST/LAST zero, AUTO reason 6 / phase 2
after 1,200 completions, paired fence counters and no fault. With the same
four-record descriptor shape, TRAFFIC READS should be 195 (`000000C3`), versus
291 (`00000123`) in 433. Other descriptor shapes can have different counts.
Unexpected reason 5 means a recorded refusal/fault and needs interpretation.

The matched proof-only and full modules are retained for a later targeted
comparison; they are not additional runs requested by this candidate. Only
their immutable four-byte selector differs in the common helper. If the new
gate-only path is still substantially below baseline, the next discriminator
is a matched bypass of the sampler/fence, retaining the wrapper and reservation,
before making broader assumptions about VMU protocol work.

Reproduce the three modes with the commands in
`armada-ce-performance-controls.md`, using separate clean build directories.
Run the host suites with `make -f Makefile.armada-maple-test a2-tests auto-tests`.
With the pinned toolchain active, `make -f Makefile.armada-maple-test
cache-snapshot-native-tests` checks the leaf instruction model; pass `--elf`
directly to its Python test to inspect a linked helper. The release archive
binds each module and clean source commit to the linked native audit.

## First hardware result: optimized gate-only

Build 9ed8d6d90cfd mode 1 reached the planned reason 6 / phase 2 report.
All 1,200 sampler visits were accepted; READS was 195 (0xC3), matching the
predicted count for this source. All 1,200 fence leases restored with no
recorded refusal/fault, active hold or flags. A2 and proof counters stayed
zero as expected. The user reported better playback than the preceding
control, still below baseline but usable. See
`evidence/armada-9ed8-gate-only-console.json`.

This is hardware support for reducing the common-path checking cost, not
baseline parity or a measured mask-duration improvement. Gate-only does not
exercise the new native RX ownership-query path. The next comparison uses
the already prepared `held-comparisons/02-proof-only` module from the same
archive/build, with the same image/settings and physical A1. Expect MODE 2,
nonzero/equal FIRST and LAST counters, and no A2 publication. If it remains
usable, full readonly A2 publication can then be evaluated separately.

## Second hardware result: optimized proof-only fails playback

Build 9ed8d6d90cfd mode 2 completed 1,199 matching initial/final ownership
checks and 1,200 paired DMA fence windows with no recorded fault. A2
publication remained zero. The user reported early video stoppage and repeated
audio: a severe performance failure. The reason 6 / phase 2 terminal report
was planned, but playback had already failed before it. See
`evidence/armada-9ed8-proof-only-console.json`. Hold the prepared full mode.

The common 195-read counter does not include either ownership query. A
representative parent-only initial/final pair adds 660 budgeted inspection
loads and 24 bulk snapshot calls, plus uncached C instruction fetch, mapping
structure copies, stack traffic and bytewise output clearing. Matching proof
counters do not establish acceptable latency. The next implementation must
reduce this execution cost while preserving fresh mapping/ownership checks.
