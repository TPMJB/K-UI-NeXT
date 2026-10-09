# Controlled GD BIOS command console result

Recorded 2026-10-07 from the owner's photograph
`image-1791390872338.jpg`. The screen identifies profile 08, build
`f7318ac45e3e`, detached SCI, owned AICA and read-only exFAT. It shows
`CDDA TEST COMPLETE`, all eight stages and zero failures / stopped audio.
The photograph remains external to this source record.

The delivered package identifies the tested source as
[`f7318ac45e3e43819ae329794fa4b8eba3ed98a4`](https://github.com/TPMJB/K-UI-NeXT/tree/f7318ac45e3e43819ae329794fa4b8eba3ed98a4),
tree `fc38afa7f95468c2491b1c8455bc41a9ff77aab7`. Its archived local checkpoint
is `51623bf50eedb9ab6d57a9169a219495c919ebe2`, with the same source tree.
The [BIOS-test checklist](../cdda-bios-test.md) defines the numerical gates;
the [linked memory and native-vector audit](cdda-bios-memory-2026-10-07.md)
is separate static and host evidence. The earlier
[profile 07 hardware result](cdda-service-hardware-2026-10-07.md) remains a
separate tested checkpoint.

## Exact photographed counters

| Counter | Value |
| --- | ---: |
| Completed playback stages | 8 |
| Worst half refill, us | 77,076 |
| Maximum service gap, us | 5,624 |
| Minimum refill margin, us | 104,194 |
| Checked card blocks | 34,394 |
| Observed private engine stack use, bytes | 5,304 |
| Client seconds / vector calls | 60 / 17,151 |
| BIOS requests / completions | 3,429 / 3,427 |
| Audio actions / DRIVE checks | 7 / 14 |
| Checked bytes / service errors | 7,002,112 / 0 |
| Client / service stack bytes | 2,868 / 1,820 |
| ABI checks / vector restored | 17,150 / 1 |
| Expected rejects / cancels | 14 / 1 |
| Failures / stopped audio | 0 |

## Numerical result and counter coherence

**Profile 08 passed its implemented console checks.** The controlled client
reached 60 documented-clock seconds after card/control setup while delivering
commands through the temporarily owned GD vector at `0x8c0000bc`. The seven
audio actions, fourteen DRIVE checks, fourteen expected protocol refusals and
one queued cancellation match the required sequence. The original vector
was restored and verified. No unexpected service/test errors were reported.

Both displayed counter relations are exact:

```text
requests = completions + 2 = 3427 + 2 = 3429
ABI checks = vector calls - 1 = 17151 - 1 = 17150
```

The two accepted requests without successful completion are the deliberately
aborted and reset queued reads. The additional vector call is the owner's
deliberately nested EXEC, which is refused and does not pass through the
client's register probe. Every counted probe returned without an integer
register or PR mismatch.

The known source sequence also accounts exactly for the checked data:

```text
completed reads = completions - audio actions - NOP = 3427 - 7 - 1 = 3419
checked bytes = 3419 * 2048 = 7002112
```

The 7,002,112 bytes exceed the required 65,536 bytes. They are independently
verified successful one-sector reads from selected ranges of the 8 MiB
fixture; this total does not establish a contiguous full-file pass. The
photograph reports the aggregate count, while its equality to the client's
offset-dependent verification is an implemented pass gate.

The observed stack watermarks are below 65,472 usable bytes per guarded
stack. Engine use of 5,304 bytes and worker use of 1,820 bytes are below their
separate conservative static estimates with helper allowance of 6,244 and
6,316 bytes. Client use of 2,868 bytes matches its audited chain estimate and
is below the 3,124-byte estimate with helper allowance. Watermarks describe
the writes made by the exercised paths; they do not replace the static
bounds or establish every possible fault-path stack use. The worst refill
was 77.076 ms, the maximum reported service gap was 5.624 ms and the minimum
reported refill margin was 104.194 ms on this console/card.

## Visible results and additional implemented gates

The photograph directly supplies the counters above, vector-restored flag
and completed status. Completion additionally requires three stale CHECK
results, one accepted queued RESET while stopped, one refused nested EXEC,
eight malformed-descriptor refusals, intact 32-byte canaries around the
client read buffer and the controlled CPU-context comparison. Those checks
are inferred from successful completion of the tested program; this screen
does not show their individual measurements or counters.

The context comparison checks VBR, GBR and SR configuration, excluding
ordinary caller-clobbered T/Q/M bits. Final gates require owned audio
STOPPED, an empty BIOS command queue and a retired service lease. The client
sequence includes copied-parameter reads, one-shot EOF, indefinite looping,
pause with a frozen played cursor, RELEASE, STOP and replay. Its 60-second
duration includes protocol work and planned silence; it is not a measurement
of sixty uninterrupted seconds of looping audio.

The delivered runtime contains a 3,084,256-byte initialized payload,
including the gap before separately linked client code, and reserves
3,211,264 bytes from `0x8c010000` through `0x8c320000`. This exact larger
profile 08 image was admitted and executed on the supplied console/card.
The prior profile 07 pass does not substitute for this new command/vector
result.

## Evidence limits and preserved checkpoint

This is an original controlled homebrew client and owned vector adapter.
It supports the exercised raw GD calling convention, request/check/EXEC
queue, bounded one-sector PIO reads and selected audio commands. It does
not establish retail-game execution, interrupt servicing, DMA, multi-sector
reads, finite repeat counts, PLAY2 endpoint fidelity or shared game-owned
RAM, sound drivers and CPU resources. Queued abort and reset retire pending
work; they do not abort an active SCI/DMA transfer. The client uses cached
P1 buffers, so this result does not establish cache coherence for physical
or P2 aliases.

No new listening report accompanied this photograph. It therefore adds no
claim about precise channel order, absolute pitch or audible continuity.
The fixed 12,468,720 Hz TMU reference and unchanged configured AICA pitch 0
remain the program's documented contract, not an independent oscillator
measurement. Ordinary integer probes do not preserve FPU/FPSCR or recover
a call that corrupts its stack pointer or fails to return. Cooperative
service cannot guarantee uninterrupted audio during arbitrary client stalls.

Keep the tested `K-UI-CDDA-BIOS-Test.zip` immutable:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Test ZIP | 16,302,626 | `025b3c28035adb2f896b1350347821a8a152d649021357483f5e6b1c0700960b` |
| Profile 08 runtime | 3,084,320 | `e1773971c79f6e5781e0de887f989b8689d6a02d6f4f52250b21d1c3afcb2404` |

The manifest records preparation before hardware testing. This later record
adds the successful console result without rewriting the archive. No photo
or owned game audio is included in this source evidence. Ordinary 1.8.5
readers remain unchanged, and this result requires no repeat of profiles
00–08 before advancing to the next separately defined test.
