# Pinned playable DOA2 SD baseline, 2026-09-24

The owner explicitly requested that this working state be pinned before
further optimization. Preserve this branch and the exact tested ZIP. Future
experiments belong on the development branch, not by advancing the baseline.

## Exact rollback point

- Build: `6c02bd8b22f4`.
- Pinned branch: `baseline/doa2-sd-6c02bd8b22f4`.
- Exact packaged merge: `6c02bd8b22f48aafc4837a82b4cc0cb13fbdb293`.
- Implementation source: `730cf82157b906846c2a0e3072f4a826ea1d259a`.
- Successful native CI run: `36034511628`; original SD artifact: `10824500117`.
- Archived ZIP: `KUI-DOA2-baseline-6c02bd8b22f4.zip`, 8,131,824 bytes.
- ZIP SHA-256: `6509c536ef8b6887381badf02e599356cc5b60801fdd7712eeadf99f179ad5fb`.
- Runtime CRC32: `c315b21d`; retail-loader CRC32: `30d940d0`.
- Resident memory end: `0x8c00ba90`, 112 bytes below the unchanged guard.
- Conservative stack bound: 928/1232 bytes; linked instruction audit passed.

The archived ZIP is byte-identical to the tested
`KUI-DOA2-short-reads-sd-update.zip`; only its filename changed. The original
GitHub Actions artifact expires, so it is not the sole binary rollback copy.
Restore the ZIP's KUI directory on the SD card, including both runtime.kui and
apps/games/retail-boot.kui. Use the existing boot CD and unmodified game dump.
Verify the displayed build ID before comparing future experiments.

## Owner's latest hardware report

DOA2 T3601N / V1.100 / U, existing raw GDI, serial SD adapter. These are the
owner's approximate intervals, not an instrumented timing trace; do not combine
them into a claimed SD-throughput measurement or change their endpoints.

| Observed interval or behavior | Owner report |
| --- | --- |
| Press Play in K-UI to bootstrap | 14 seconds |
| Bootstrap to game boot | 12 seconds |
| Starting logo to next screen | Another 8 seconds |
| Start screen responsiveness | Appeared promptly and remained responsive |
| Background textures on start screen | Appeared about 10 seconds later |
| Character selection to starting FMV | 30 seconds of black screen |
| Audio in this run | Fluid |
| Beginning of battle | Some lag during the first roughly 5–8 seconds |
| Overall assessment | Getting close; not unplayable |

This supersedes the previous candidate's hardware-untested status. It does not
establish broad game compatibility, peak SD bandwidth, or VMU save/load results.
The tested changes were two-sector EXEC chunks, persistent one-block caching,
and removal of duplicate full-destination cache purges; CRC checks remain on.

## Next optimization decision

1. Measure actual elapsed time for the exact retail backend and compare
   bounded CMD17/CMD18 reads of the same data with CRC checking. Include useful
   KiB/s and longest chunk duration so bandwidth and blocking time are distinct.
   `retail_sd.c`'s work counter is a timeout budget, not a clock. A standalone
   diagnostic may own a real timer before any game starts.
2. CMD18 is the next transport candidate, but it must span consecutive physical
   blocks through the raw-image reader. Merely changing the count>1 branch of
   the SD routine changes nothing: its caller currently always requests one
   block. Keep the existing short game-side EXEC bound; use CMD12 cleanup on
   completion, discontinuity or failure before returning the serial pins.
   Fixed CMD17 framing alone is small compared with 512 data bytes. Card wait
   time and CPU overhead must be measured before promising DreamShell parity.
3. Do not simply enable game interrupts on the private resident stack or leave
   a live stream while restoring/releasing serial-pin ownership. The current
   hook restores the caller's exact SR between complete chunks. Keeping a
   CMD18 stream live across those returns is a separate ownership/reentrancy
   design, not equivalent to stopping the host-generated SPI clock.
4. A separate 2048-byte launch copy would reduce long sequential data traffic
   from 147 to 128 physical blocks per 32 game sectors: 12.93% fewer bytes,
   with an ideal transfer-limited useful-throughput gain of 14.84%. It needs
   format/conversion support and separate treatment of raw requests; it is
   not a change to the archival dump or audio tracks. DreamShell already
   supports 2048-byte sectors and provides a GDI optimization utility.
5. An isolated SCSPTR2 loop can characterize CPU/MMIO cost, but cannot prove
   the complete storage ceiling. Our receive loop performs two writes and one
   read per bit plus shifts, loop/dispatch, RAM stores, CRC and card waits.
   Benchmark the exact pin sequence separately from real SD transfers; label
   isolated-register results accordingly. No more receive-loop changes are
   justified solely by the one-million-access proposal.

No new binary, console test request or broad test suite accompanies this pin.

Primary reference checks:

- KOS SCIF SPI implementation: https://github.com/KallistiOS/KallistiOS/blob/master/kernel/arch/dreamcast/hardware/scif-spi.c
- DreamShell sector-size handling: https://github.com/DC-SWAT/DreamShell/blob/master/modules/isoldr/module.c
- DreamShell GDI utility: https://github.com/DC-SWAT/DreamShell/blob/master/utils/iso_make/optimize_gdi.bat
- Renesas SH7750 hardware manual, SCSPTR2 register and pin control:
  https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware

These references support interface/source observations, not a measured claim
that this reader matches or exceeds another loader's performance.
