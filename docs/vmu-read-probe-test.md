# Physical VMU read cadence probe

This separate Card tools program compares physical reads without connecting
to SD or writing a VMU. It observes cadence and control blocks; differing
bytes do not identify their cause and are never accepted as a save image.
The existing importer and bounded reader are unchanged.

The probe includes the bootstrap's initialized storage handoff record so Card
tools can admit it from SCIF, SCI or IDE. The record is patched only in RAM;
the probe does not consume it or link the SD implementation. Package creation
checks the exact C envelope parser, payload CRC and storage handoff patcher on
all three transports. The generic envelope check alone does not validate that
handoff, and the earlier probe package lacked it, preventing SCI/IDE launch.

Preserve the usual `KUI/tools.kui` outside `KUI`, then install only this
probe's `KUI/tools.kui`. Open **Card tools**, select the black VMU's slot with
Left/Right, and press X once. Keep that same VMU connected until the probe
finishes or stops. No SD import, formatting, deletion or repair is involved.
Leave the white VMU and both existing virtual-card pairs unchanged.

The probe runs 32 rounds at each requested gap: 0, 20 and 100 milliseconds.
Every round reads block 0, then FAT block 254, then root block 255. Positive
gaps are between consecutive reads within a cadence, giving 95 gaps per
cadence. The fast cadence has no requested sleep. Maple transactions and
occasional progress rendering add time; the table records actual start times
and durations in milliseconds. The first accepted sample of each block is
the fixed baseline for all three cadences.

Once FAT block 254 has been read, the header also shows its first-sample FAT
entry for logical block 0 (`fffc` means free). This uses bytes already captured;
it causes no additional reads. Check the FAT block's own differences and
transitions before interpreting that observation. A free marker alone does not
authorize skipping a block or accepting inconsistent data. The importer still
requires an exact, stable whole-card capture.

The device pointer, functions and 30 product-name bytes are pinned before the
first read. The VMU filesystem lock spans all reads, gaps and progress
rendering. A device change, reader refusal, malformed response or timeout
stops the entire run. The application does not retry a possibly in-flight
frame. There is no hotplug retry or second run within the same launch.

Read counts and table rows count calls to the bounded reader, not every Maple
wire submission. The unchanged pinned KOS IRQ path can internally resubmit an
`AGAIN` response before the helper's callback. This probe does not count those
internal resubmissions. Actual durations include any such activity; a terminal
helper refusal still stops the application without another read call.

After completion, Left/Right selects the three retained summary pages. Each
block reports successful samples, reader failures, guard refusals and samples
different from its baseline, plus baseline/last CRC32. The separate `trans`
count compares each accepted sample with the previous sample of that block
within the same cadence. Its first sample has no transition. A cadence can
therefore report 32 baseline differences and zero transitions if its data is
stable but disagrees with an earlier baseline. Totals count differing
bytes and bits across samples. The byte-XOR syndrome XORs every differing
byte mask; a zero syndrome can coexist with changed bytes. The first differing
sample records its round/time, byte/bit counts, syndrome and first two changed
indices with baseline/current values and XOR masks. Index 512 means there was
no second changed byte. These counts describe comparisons, not corrected data.

Each page freezes the first and last bounded-reader diagnostics, including
type/word count, function/block fields, lock/state information and raw-before,
raw-after and copied-output CRC32. X cycles Summary, Changes and CRC table.
Changes retains each block's first differing sample and its full reader
diagnostic, including all three CRC32 fingerprints; later reads and rendering
do not replace that evidence. Up/Down moves ten table rows, and Left/Right
still selects cadence. The table keeps up to 96 attempted bounded-reader calls
per cadence, including a terminal reader refusal. A guard refusal before a
reader call has no table row.

Photograph all three summaries and any stop, plus the Changes page and
relevant table rows when differences occur. Reader failures show stage/result
details. A run that stops early leaves later pages unstarted. No additional probing occurs while paging
through results. Power off and restore the usual Card tools module when done.

Host tests compile this exact application logic with mocked Maple, time and
display boundaries, covering stable data, bit changes, framing/timeout/device
faults, exact cadence counts, a bad first baseline followed by stable data,
retained first-change diagnostics and result-page bounds. They do not establish
the physical cause of the black VMU observations. The native build retains compiler
callgraphs so its project calls can be checked for read-only access.
Host package cases also reject absent, duplicate, misaligned or malformed
handoff records even when their surrounding envelopes have valid checksums.

Build from the repository root:

```sh
make -f Makefile.vmu-read-probe host-test
source .deps/kos/environ.sh
make -f Makefile.vmu-read-probe BUILD=build/vmu-read-probe package
```
