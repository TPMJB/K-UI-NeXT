<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE completion-time traffic test

Build 00cec9b6ee9a passed on the console: 6,800 completion entries, 32 bounded
context samples, expected user-to-kernel transition, patched entry CRC match,
and complete MAPPING/DRIVER witnesses. This establishes that the authored
bridge runs in the actual driver worker. It does not establish CE virtual
VMU service. See evidence/armada-ce-maple-console-00cec9.json.

This next isolated build observes ordinary descriptor and response headers
at that completion entry throughout the session. Controller polling cannot
exhaust its capture: it retains the first and latest subunit request and
counts subunit commands. Virtual VMU remains OFF. No save payload is read,
modified or emulated by the observer.

## Install and test

1. With the console off, back up `KUI/apps/games/ce-probe.kui` to the PC.
2. Replace only that file with the package's `KUI/apps/games/ce-probe.kui`.
3. Put a physical VMU in A1. Prefer one containing an Armada save if available;
   keep A2 empty. Cold boot original Armada with the same original raw GDI:
   **SCI SD, Background loading On, CD music Off, virtual VMU Off**.
4. Open Armada's load/save screen. If an existing physical save is present,
   load it. Creating, deleting or overwriting a save is not required.
5. Press **A+B+X+Y+Start**. Photograph all three rotating pages:
   **TRAFFIC, MAPPING and DRIVER**, including the BUILD and bottom rows.
6. Power off, then restore the backed-up module before the next cold boot.

If the game stops before the menu, photograph the last screen, power off and
restore the backup. The archive does not contain your exact rollback module.
No virtual-card recreation is needed.

## Report

| TRAFFIC row | Meaning |
| --- | --- |
| MAGIC VISITS OK REFUSED READS | MTR1 (`3152544d`), total completion visits, accepted/refused observations, budgeted sampler reads on the latest visit |
| SUBUNIT INFO MEDIA READ WRITE OTHER | Counts of command 1, 10, 11, 12 and other commands to subunits, across accepted observations |
| FIRST OFFSET CONTROL RX REQUEST RESPONSE | First accepted subunit descriptor: TX byte offset, control word, RX bus address and request/response headers |
| LAST OFFSET CONTROL RX REQUEST RESPONSE | Most recent accepted subunit descriptor |
| RAW EXT TX BUS RX BUS LAST END | Raw extension argument, TX/RX physical starts, last descriptor byte offset and list end offset |
| TRAFFIC FAULT WHY AT VALUE EXPECT DETAIL | Latest visit result; zero after an accepted observation |

The header fields are observational samples, not unique request counts or a
record of every frame. Repeated command counts can include repeated polls.
No subunit rows means no accepted subunit request was observed, not that a
card is missing. Ordinary controller packets are excluded from these rows.
Counters saturate rather than wrapping. Terminal report freezes only our
private sampler; it does not stop or claim ownership of hardware DMA.
MAPPING still expects patched CRC `576e75e7` and mask `1f`.

## Scope and implementation

The hardware-tested mapped bridge and its existing SetKMode import are
retained. A stack-switching shim runs the new C sampler on an authored
1,536-byte private stack. Two pages (`8cfce000..8cfd0000`, 8 KiB) are reserved
before NK constructs its allocator. Existing OEM globals above `8cfd0000`
are unchanged. The original exact-owner/kernel admissions, zero tail checks,
section-size extension, cache-disabled relay and entry-last publication
remain mandatory. The stage copies only the authored helper and initializes
its remaining reservation to zero.

The sampler uses the actual worker-supplied extension argument. It admits
its current owner slot, mappings and coherent extension fields before any
transfer read. It validates uncached TX/RX pool mappings, disjoint ranges,
ordinary descriptor geometry and bounded receive destinations. Only four
header words per descriptor are inspected; outgoing and incoming payloads
are skipped. Metadata, mappings and header values repeat before an accepted
observation is committed. A changed or refused observation cannot replace
accepted header samples or increment their command counts.

All variable table reads stay in admitted main RAM. Coherent extension words
use the existing pinned P2 cache-array reader: checked physical backing and
cache colors, repeated tags and data, with dirty alternate aliases refused.
There are no direct CE virtual-address loads in the helper. Transfer headers
use main-RAM P2 aliases. There is no Maple register access, DMA control, request injection,
CE completion call, SD I/O, allocation, virtual device or save-data write.
The existing mode API is called by the bridge, not the sampler. Work per
completion is bounded at 16 ordinary descriptors and 512 budgeted sampler
reads, plus fixed entry context/freeze loads; sampling
continues beyond the first 32 calls. The valid 16-descriptor host fixture uses
383 reads. Native audits include the complete cache-array leaf, private C
call graph and bridge/shim preservation.

The point is called before the original ordinary response walker and before
InterruptDone, after its worker sees status bit `1000`. This does not prove
that autonomous VBlank DMA is idle or confer an allocation lifetime lease.
Repeated samples remain a read-only consistency observation. At release, this first
traffic test was pending on hardware.

## Reproduction

Run `make -f Makefile.armada-maple-test traffic-tests`, then source the KOS
environment and build with `make -f Makefile.armada-maple-traffic
build/armada-ce-maple-traffic/entry.elf`. Run the mandatory extracted native
proof audit with `python3 tools/check_armada_ce_maple_traffic_package.py
/path/to/native-proof`. The archive includes the source delta, clean build
provenance and verification evidence, without owner executable bytes.


## Console result: build 2c0fd69e4be7

The owner console run captured **4,875 completion visits**, all accepted, with
zero refused observations. The latest visit used 291 budgeted reads. Subunit
command observations included **22 block reads and one block write**, together
with one device-info, seven media-info and 22 other commands. These counts can
include repeated descriptors; they do not prove unique block operations or a
successful persistent write. The first retained exchange addresses physical
A1 and has device-info request/response headers. The last retained request is
`02000110` (command `10`, in the OTHER bucket), with response `00010007`;
it is not a retained payload or acknowledgement of the block-write command.

MAPPING retained the expected patched CRC `576e75e7`, full mask `1f` and clear
faults. DRIVER also retained full mask `1f` and clear faults. This passes the
bounded completion-time traffic observation test on the exact owner build.
Virtual A2 remained off; neither virtual saves nor DMA ownership, allocation
pinning or a response-lifetime lease are established by this result.
The complete transcription is in
[evidence/armada-ce-maple-console-2c0fd6.json](evidence/armada-ce-maple-console-2c0fd6.json).
