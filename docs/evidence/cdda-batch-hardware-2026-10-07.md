# Controlled BIOS batch-read and deadline console results

Recorded 2026-10-07 from the owner's original photographs
`01-image-1791393442716.jpg` and the clear replacement `image(3).png`, viewed
without cropping, enhancement or other image modification. They clearly
identify profiles 09/10 and build `dcdd845d7628`. The replacement supersedes the
blurred `02-image-1791393552400.jpg` for profile 10 measurements. Both display
`CDDA TEST COMPLETE` and zero failures / stopped audio.
The photographs remain external to this source record.

The delivered bundle identifies the tested source as
[`dcdd845d762852f02d55fb5c458efafa5cbbbb74`](https://github.com/TPMJB/K-UI-NeXT/tree/dcdd845d762852f02d55fb5c458efafa5cbbbb74),
tree `77ef0f49907b01d48a6817855eba8294414bd2e1`, corresponding to local
checkpoint `29301db45981e39484633b7874c04060056a105d`. Both photographed build
identifiers match this bundle. The [batch-test checklist](../cdda-batch-test.md)
defines the gates, while the
[memory and native-vector audit](cdda-batch-memory-2026-10-07.md) records separate
static and host evidence. The preceding
[profile 08 result](cdda-bios-hardware-2026-10-07.md) remains a separate checkpoint.

## Profile 09: exact photographed counters

All values in this table are directly readable from the first photograph.

| Counter | Value |
| --- | ---: |
| Completed playback stages | 8 |
| Worst half refill, us | 75,952 |
| Maximum service gap, us | 12,062 |
| Minimum refill margin, us | 101,405 |
| Checked card blocks | 50,029 |
| Observed private engine stack use, bytes | 5,308 |
| Client seconds / vector calls | 95 / 13,909 |
| BIOS requests / completions | 628 / 626 |
| Checked bytes / full 8 MiB passes | 8,851,456 / 1 |
| Size classes / minimum completions | 8 / 77 |
| Committed chunks / progress checks | 4,322 / 5,212 |
| Client / service stack bytes | 33,788 / 1,832 |
| ABI checks / vector restored | 13,908 / 1 |
| Failures / stopped audio | 0 |

**Profile 09 passed its implemented console gates.** The 95 documented-clock
seconds fall within the required 90–120-second window. One full 8 MiB pass and
all eight requested sizes are directly reported; the minimum of 77 completed
reads per size exceeds the required 16. Completed-pass coverage counts only
contiguous successfully completed logical requests under looping audio, excluding
the deliberately canceled and reset prefixes. That coverage definition is an
implemented gate, rather than an independently photographed trace of every read.

The displayed counter relations are exact:

```text
requests = completions + 2 = 626 + 2 = 628
checked bytes = committed chunks * 2048 = 4322 * 2048 = 8851456
ABI checks = vector calls - 1 = 13909 - 1 = 13908
```

The two unfinished accepted requests are the deliberate partial cancel and
partial reset. Their two confirmed 2 KiB prefixes remain in checked-byte/chunk
totals but are excluded from successful logical completions and full-pass
coverage. The extra vector call is the deliberately nested EXEC refusal, which
does not pass through the client's integer-register probe.

Engine, client and worker watermarks are all below 65,472 usable bytes per
guarded stack. They also fit their separate static estimates with helper
allowance: 6,248 / 34,044 / 6,328 bytes respectively. Client use of 33,788 bytes
matches the audited conservative path before helper allowance. These are writes
observed on exercised paths; they do not replace static bounds for every possible
fault path. Reported worst refill, service gap and minimum margin are 75.952,
12.062 and 101.405 ms respectively on this console/card.

Successful completion additionally requires six audio actions, eleven DRIVE
checks, eight protocol refusals, three stale CHECKs, one partial ABORT while
playing, one partial RESET while stopped, 24 boundary reads and at least eight
random reads. Client verification checks each confirmed prefix, its monotonic
sector-aligned progress, untouched suffix and 32-byte canaries. Eight descriptor
refusals, integer probes, context comparison, restored vector, empty queue,
STOPPED audio and retired service lease are also final gates. These individual
checks are inferred from successful completion of the tested program; their
separate counters are not all shown on the photograph. The 95 seconds include
protocol work and planned silence, rather than 95 uninterrupted seconds of audio.

## Profile 10: exact replacement-photo counters

All values in this table are directly readable from the clear replacement
photograph, which identifies `PROFILE10 DEADLINE / confirmed prefix preserved`.

| Counter | Value |
| --- | ---: |
| Completed playback stages | 8 |
| Worst half refill, us | 0 |
| Maximum service gap, us | 6,770 |
| Minimum refill margin, us | 0 |
| Checked card blocks | 172 |
| Observed private engine stack use, bytes | 5,300 |
| Client seconds / vector calls | 0 / 17 |
| EXPECTED gaps / service errors | 1 / 0 |
| Checked bytes / committed chunks | 2048 / 1 |
| Confirmed prefix / progress checks | 2048 / 2 |
| Client / service stack bytes | 33,360 / 1,832 |
| ABI checks / vector restored | 16 / 1 |
| Expected negatives / stale checks | 4 / 1 |
| Failures / stopped audio | 0 |

**Profile 10 passed its implemented expected-deadline gates.** Eight stages,
one expected deadline, zero service errors, one committed 2 KiB chunk and the
preserved prefix are now directly photographed. Engine/client/worker stack
watermarks fit the 65,472-byte usable stacks and their separate conservative
estimates with helper allowance of 6,240 / 33,720 / 6,328 bytes.

Zero worst-refill and minimum-margin metrics mean this short companion never
performed a post-key-on half refill. They do not establish zero-cost refill or
refill headroom. Client seconds is an integer counter; zero does not mean the
200 ms wait did not occur. The 6.770 ms maximum service gap is the recorded ring
observation gap before deadline refusal; the intentionally omitted 200 ms
service interval is represented by the separate expected-gap counter. Successful
completion also validates the exact refusal/cleanup sequence below; those
individual operations remain source-based inferences, rather than a photograph
of every call or physical transfer.

The client submitted a copied 16-sector READ, confirmed the first sector, then
waited 200 ms without EXEC. The next EXEC had to refuse specifically at
service-enter DEADLINE before a second physical read or audio observation.
Worker-stack cleanup keyed off owned audio and retired the remainder as
FAILED/IO with 2 KiB and ATA 0. The client checked the unchanged suffix/canaries,
acknowledged the terminal once, observed an unknown duplicate, and verified a
bad-count REQUEST and stale ABORT without further physical I/O. Final gates
require audio/service FAULT, an empty queue, intact stack guards, context match
and exact old-vector restoration. The intended deadline is a successful test
outcome; the zero unexpected-failure count does not mean no deadline occurred.
The visible 16 probes and restored-vector flag match this contract.

## Preserved bundle and evidence limits

Both new initialized images were admitted and executed on the supplied
console/card within the same 3,211,264-byte envelope. Profile 09's payload is
3,085,120 bytes; profile 10's is 3,082,656. Keep the delivered bundle immutable:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `K-UI-CDDA-Batch-Tests.zip` | 16,632,085 | `529ff873460fb4c1220060043a3bdf7cef9849ea22ee369cc55b745f23c5c3de` |
| Profile 09 runtime | 3,085,184 | `69766b77179244bba19e9876587cef713aba4a53be6f2f66776cd28a601b5234` |
| Profile 10 runtime | 3,082,720 | `9a045bb9171ef288d05f617f97b758ed83605d8455c09662fc1ae8776cd89ed0` |

The original manifest predates hardware testing. This later evidence adds the
results without rewriting the archive. The clear replacement supplies exact
profile 10 measurements without changing the original tested artifacts. No photo
or owned game audio is included in the source evidence.

These results establish the exercised cooperative homebrew BIOS-vector batch
reads and intentional deadline containment. They do not establish retail-game
execution, IRQ servicing, DMA, active physical-transfer abort, finite audio
repeat counts or shared game-owned RAM/sound/CPU resources. Queued cancel/reset
occurs between EXEC calls. Cached P1 buffers were exercised; other aliases and
cache-coherence contracts remain separate work. No new listening report is
available here, so the photos add no precise pitch/channel/audible-continuity
claim. Ordinary 1.8.5 readers remain unchanged. The passed profile 09/10 gates
can be recorded with exact watermarks and counters from both clear photographs.
