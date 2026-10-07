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

## Matching DMA-eligible run

The user subsequently supplied five photographs from the same build
`482efe87c56d`, identified by **PROFILE14 DMA /**. These include **14 STOP —
IMAGE READ FAILED** followed by the four observation pages. The user reports
that this run crashed with looping audio and mentions a clip. The numerical
record here uses the five photographs; it contains no independent listening
result. The earlier attempt described as DMA used the ordinary reader rather
than this comparison build and remains excluded from the paired result.

### DMA-eligible terminal read trace

| Legend | Raw hexadecimal words |
|---|---|
| `FN ARG FLAG G BAD` | `00000002 00000000 00000000 4B554947 00000000` |
| `CMD LBA N BPS` | `00000011 00069263 0000000D 00000800` |
| `IO LBA N DONE STEP` | `00069269 00000003 00003000 00000003` |
| `IMG PRE STOP SD` | `00000005 00000003 00000003 00000003` |
| `DST BLK ERR CARD` | `0C87CA20 000025E8 00000001 03A044FC` |

| Field | Decoded value |
|---|---|
| Function / argument / latched hook fault | GD EXEC `2 / 0 / 0` |
| First guard / mismatch mask | Expected `4B554947`; all four inspected guard words match (`BAD = 0`) |
| Command | BIOS DMAREAD, decimal `17` |
| Request LBA / sectors / bytes per sector | `430691 / 13 / 2048` |
| Failing callback LBA / sectors | `430697 / 3` |
| Credited bytes / current sector budget | `12288 / 3` |
| Image result | `5 = KUI_GAME_IO` |
| Storage result before outer stop | `3 = KUI_LOADER_SD_TIMEOUT` |
| Outer stop result / final storage result | `3 / 3 = KUI_LOADER_SD_TIMEOUT` |
| Original destination | `0C87CA20` |
| Successful physical-block fetches | `9704` |
| GD service error | `1 = KUI_GD_ERROR_IO` |
| Last attempted physical card LBA | `60835068` |

The requested 13 sectors total 26,624 bytes. The credited 12,288 bytes are a
six-sector prefix: `430691 + 6 = 430697`, matching the failing callback LBA.
The [synchronous GD read path](../../src/core/retail_gd.c) advances its credited
count only after a whole callback succeeds. The failing three-sector callback
requests 6,144 bytes, and seven request sectors (14,336 bytes) remain
uncredited. The failing callback may have partially touched its destination;
these photographs do not establish how many uncredited bytes were written.

The request range `[430691,430704)` is within track 15's logical data range
`[377422,549150)` recorded by
[test 13](cdda-preflight-hardware-2026-10-07.md). The physical card LBA is a
separate address. The result names follow the
[image](../../include/kui/game_image.h),
[SD](../../src/loader/sd_reader.h) and
[GD service](../../include/kui/gd_service.h) definitions.

As in the [previous diagnostic](cdda-retail-diagnostic-hardware-2026-10-07.md),
the primary storage failure precedes explicit outer cleanup. The failed
read-run closes its stream; the later
[storage-stop wrapper](../../src/loader/retail_storage_impl.h) returns TIMEOUT
when the SCI lease fails its [health check](../../src/loader/sci_sd_bus.c).
The outer stop result therefore does not establish a second independent
CMD12 timeout. The exact transport wait and its sampled status remain unknown.

### DMA-eligible commands and observations

| Legend | Raw hexadecimal words |
|---|---|
| `CALL 20 21 BAD` | `0000038F 00000000 00000000 00000000` |
| `20F` | `00000000 00000000 00000000` |
| `20L` | `00000000 00000000 00000000` |
| `21F` | `00000000 00000000 00000000` |
| `21L` | `00000000 00000000 00000000` |
| `CPU TMU` | `00000019 00000008` |

`0000038F` is **911** observed GD calls. No accepted PLAY20 or PLAY21 request
was recorded before the stop; the latched hook-fault word is zero.
**AUDIO NOT SAMPLED** appears on the first and final observation pages.
The user's reported looping audio is not identified as CDDA by this record.

### DMA-eligible CPU snapshots

| Field | First | Latest |
|---|---|---|
| SR | `60000101` | `60000060` |
| VBR | `8C00F400` | `8C00F400` |
| GBR | `8C000000` | `8C000000` |
| Caller PR | `8C08D26C` | `8C08D55A` |
| Caller SP | `8C00F39C` | `8C00EDF0` |
| MMUCR | `00000000` | `00000000` |

`C N` is `00000019 0000038F`: SR, caller PR and caller SP changed, with
911 CPU samples.

### DMA-eligible timer snapshots

| Field | First | Latest |
|---|---|---|
| TSTR | `00000001` | `00000001` |
| FRQCR | `00000E0A` | `00000E0A` |
| TCOR0 | `FFFFFFFF` | `FFFFFFFF` |
| TCNT0 | `FFFCAAB6` | `FFC21325` |
| TCR0 | `00000002` | `00000002` |
| TCOR1 | `FFFFFFFF` | `FFFFFFFF` |
| TCNT1 | `FFFFFFFF` | `FFFFFFFF` |
| TCR1 | `00000000` | `00000000` |
| TCOR2 | `00BE41F0` | `00BE41F0` |
| TCNT2 | `005E5401` | `005E5401` |
| TCR2 | `00000020` | `00000020` |

`C N` is `00000008 0000038F`: only TCNT0 changed, with 911 timer samples.
These snapshots do not calibrate transfer latency or identify a clock defect.

## Paired result and limits

| Variant | Photographs | Recorded outcome |
|---|---|---|
| PIO | Four observation pages | User reports “made it but super laggy”; one accepted PLAY20 request; no failure screen supplied |
| DMA eligible | Failure screen and four observation pages | Image-read I/O failure after a credited six-sector prefix; user reports looping audio; no accepted PLAY recorded |

The matching build and variant labels now establish a paired comparison.
The different outcomes implicate a difference in the payload paths or their
timing, without proving that a DMA transfer actually ran or identifying the
exact failing wait. The DMA-eligible reader retains guarded PIO fallback.
Both variants preserve command/token framing and CRC checks, omit live audio
register sampling, and provide no proof of integrated CDDA output or sound
resource ownership. No specific gameplay stage or calibrated lag measurement
is established by the supplied outcome descriptions.
