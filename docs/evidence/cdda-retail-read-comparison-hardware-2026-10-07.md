# Test 14 SCI read comparison — 2026-10-07

The user supplied four observation photographs from build `482efe87c56d`,
identified by the **PROFILE14 PIO /** title. The user described the outcome as
“It made it but super laggy.” That report does not identify a specific title,
menu or gameplay stage, and no such stage is inferred here. No **14 STOP**
failure screen was supplied for this run.

This record transcribes the reviewed photographs; the photographs are not
published or copied into the repository. Raw words below are hexadecimal.
The [paired-test checklist](../cdda-read-compare-test.md) describes the separate
PIO and DMA-eligible installations.

## PIO commands and observation counts

| Legend | Raw hexadecimal words |
|---|---|
| `CALL 20 21 BAD` | `00002DEA 00000001 00000000 00000000` |
| `20F` | `0000000E 0000000E 00000000` |
| `20L` | `0000000E 0000000E 00000000` |
| `21F` | `00000000 00000000 00000000` |
| `21L` | `00000000 00000000 00000000` |
| `CPU TMU` | `00000019 00000008` |

`00002DEA` is **11,754** observed GD calls. One PLAY20 request and no PLAY21
request were accepted; the latched hook-fault word is zero. First and latest
PLAY20 parameters match: track `14` through track `14`, repeat `0`, meaning a
one-shot request under the recorded PLAY20 convention.

The [observation adapter](../../src/loader/retail_observe.inc) records parameter
words when a PLAY request is accepted. The current
[retail GD implementation](../../src/core/retail_gd.c) accepts and completes
these audio commands without CDDA emulation. The recorded request therefore
does not prove audible CDDA output, actual playback progress or an audio EOF.
The photographs report **AUDIO NOT SAMPLED** and provide no AICA, key-mask or
G2-DMA observations.

## PIO CPU snapshots

| Field | First | Latest |
|---|---|---|
| SR | `60000101` | `60000060` |
| VBR | `8C00F400` | `8C00F400` |
| GBR | `8C000000` | `8C000000` |
| Caller PR | `8C08D26C` | `8C08D55A` |
| Caller SP | `8C00F39C` | `8C00F250` |
| MMUCR | `00000000` | `00000000` |

`C N` is `00000019 00002DEA`: mask bits 0, 3 and 4 identify changes to SR,
caller PR and caller SP. The CPU sample count is 11,754.

## PIO timer snapshots

| Field | First | Latest |
|---|---|---|
| TSTR | `00000001` | `00000001` |
| FRQCR | `00000E0A` | `00000E0A` |
| TCOR0 | `FFFFFFFF` | `FFFFFFFF` |
| TCNT0 | `FFFCAAB9` | `FD544716` |
| TCR0 | `00000002` | `00000002` |
| TCOR1 | `FFFFFFFF` | `FFFFFFFF` |
| TCNT1 | `FFFFFFFF` | `FFFFFFFF` |
| TCR1 | `00000000` | `00000000` |
| TCOR2 | `00BE41F0` | `00BE41F0` |
| TCNT2 | `00733B63` | `00733B63` |
| TCR2 | `00000020` | `00000020` |

`C N` is `00000008 00002DEA`: only the observed TCNT0 field changed, with
11,754 timer samples. These call-bound snapshots do not measure transfer
latency, calibrate elapsed time or establish timer ownership.

## DMA-eligible comparison pending

The matching **PROFILE14 DMA /** run is pending. The user's earlier attempt
described as DMA used the ordinary reader rather than this comparison build;
it is excluded from the paired result.

The PIO photographs and reported lag do not establish whether an eligible
DMA transfer ran in another build or whether DMA caused the earlier read
failure. Both variants retain the same command/token framing and CRC checks;
the DMA-eligible variant may use its guarded PIO fallback. A completed
comparison requires the matching build and variant label, its observation or
failure pages, and the user's outcome for that separate cold boot.
