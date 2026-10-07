# Controlled CDDA service console result

Recorded 2026-10-07 from the owner's photograph
`image-1791388042718.jpg`. The screen identifies profile 07, build
`f8f35fab3723`, detached SCI, owned AICA and read-only exFAT. It shows
`CDDA TEST COMPLETE`, all seven stages and zero failures / stopped audio.
The photograph remains external to this source record.

The delivered package identifies the tested source as
[`f8f35fab3723d3c0e985195054df5830fed9a402`](https://github.com/TPMJB/K-UI-NeXT/tree/f8f35fab3723d3c0e985195054df5830fed9a402),
tree `71b4af07235fe429c22aeae256121a8db539ae00`. Its archived local checkpoint
is `10d8e41d203a680daaa8c6b678eb906254b0e6d4`, with the same source tree.
The [service-test checklist](../cdda-service-test.md) defines the numerical
gates; the [linked memory and C-call audit](cdda-service-memory-2026-10-07.md)
is separate static and host evidence.

## Exact photographed counters

| Counter | Value |
| --- | ---: |
| Completed playback stages | 7 |
| Worst half refill, us | 76,917 |
| Maximum service gap, us | 5,058 |
| Minimum refill margin, us | 104,353 |
| Checked card blocks | 49,397 |
| Observed private engine stack use, bytes | 5,304 |
| Client seconds / service calls | 90 / 7,283 |
| Client / service stack bytes | 236 / 1,864 |
| ABI checks / context preserved | 7,285 / 1 |
| Checked bytes / service errors | 9,181,184 / 0 |
| Audio actions / STATUS checks | 6 / 5 |
| Expected gaps / stale refusals | 1 / 2 |
| Descriptor rejects / reentries | 8 / 1 |
| Failures / stopped audio | 0 |

## Numerical result and load admission

**Profile 07 passed its implemented console checks.** The controlled client
reached 90 documented-clock seconds after initialization while explicitly
calling the owned engine. It completed six audio command actions and five
STATUS checks, one expected missed-service refusal, two retired-epoch
refusals, eight descriptor refusals and one busy reentry refusal. Final
checks completed with the command state STOPPED and the service lease
retired. No unexpected service/test errors were reported.

The counter relation is exact: `7285 = 7283 + 2`. The client probes the
expected deadline and two stale calls, which do not increment the worker's
service-call counter; its reported ABI total excludes the REPORT call's own
probe increment. Every counted probe returned without an integer-register
or PR mismatch. The one endpoint context comparison passed for VBR, GBR and
SR configuration, excluding ordinary caller-clobbered T/Q/M bits.

The verified 9,181,184 bytes exceed the required 65,536 bytes and account for
4,483 committed 2,048-byte data jobs. These jobs use bounded selected ranges
of the 8 MiB fixture; the byte total does not establish a contiguous full-file
pass. Completion also indicates that the implemented five-second data
progress watchdog passed, although this screen does not show its maximum
observed progress gap.

All three observed stack watermarks remain within the 65,472 usable bytes
per guarded stack. Engine use of 5,304 bytes and service-worker use of 1,864
bytes are below their separate conservative static estimates with helper
allowance of 6,348 and 6,360 bytes. Client use of 236 bytes matches its audited
chain estimate and is below the 492-byte estimate with helper allowance.
Watermarks measure writes made by these exercised paths, rather than every
reserved stack byte or all possible fault paths. The worst refill was
76.917 ms, the reported maximum regular service gap was 5.058 ms and the
minimum refill margin was 104.353 ms. The deliberately omitted service call
is counted separately as the one expected gap.

This result also establishes successful admission and execution of the
approximately 3 MiB flat runtime on this console and card. The delivered
runtime contains a 3,081,728-byte initialized payload, including the gap
before client code at `0x8c300000`; its envelope reserves 3,211,264 bytes
from `0x8c010000` through `0x8c320000`. Thus the larger load was exercised,
not merely a small engine image with an inflated memory declaration.
The numerical completion joins the linked audit's separate engine, client
and service-worker regions with an actual console run. It does not establish
allocation success under every other bootstrap or game memory layout.

## Evidence limits and preserved checkpoint

The client runs original controlled homebrew code, performs its own integer
work and requests service cooperatively. The console result supports this
explicit C-call contract, guarded stack use, pause/resume from the played
cursor, seek, STATUS, stop/restart and refusal of late, stale and nested
requests. It does not establish retail BIOS command delivery, interrupt
servicing, preemption or coexistence with game-owned RAM, timers, sound
drivers or CPU context. The probe cannot recover a call that corrupts SP
or fails to return, and it makes no FPU/FPSCR preservation claim.

The expected approximately 190 ms omission is refused before resumed refill
work and followed by a fresh playback epoch. Cooperative code cannot undo
audio already heard during an arbitrary CPU stall; this is not a guarantee
of uninterrupted audio when the client stops servicing the engine.

No new listening report accompanied this photograph. It therefore adds no
claim about precise channel order, absolute pitch or audible continuity.
The fixed 12,468,720 Hz TMU reference and unchanged configured AICA pitch 0
remain the program's documented contract, not an independent oscillator
measurement of this console.

Keep the tested `K-UI-CDDA-Service-Test.zip` immutable:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Test ZIP | 16,206,926 | `ec20c7a5fa90dc35149e06646cbe2c9b29affb29a8f9095a9cd2f6c19fc6ea24` |
| Profile 07 runtime | 3,081,792 | `919f6fb9b47f4dedc71adc6009b4c89f066fb0f5b9a0c4663ac5e445f9a9fe5a` |

The manifest records preparation before hardware testing. This later record
adds the successful console result without rewriting the archive. No photo
or owned game audio is included in this source evidence. Ordinary 1.8.5
readers remain unchanged, and this result requires no repeat of profiles
00–07. The next gate remains controlled resource admission and integration
defined in the [roadmap](../cdda-roadmap.md), before any retail CDDA claim.
