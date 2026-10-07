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

## SCI first-fault and paced comparison — build `9e281cede0da`

Three later local photographs were directly inspected: `01-image-1791412087979.jpg`
shows the baseline STOP screen, `02-image-1791412105157.jpg` shows
**PROFILE14 SCI FAULT /**, and `03-image-1791412301173.jpg` shows
**PROFILE14 SCI PACED /**. All display build `9e281cede0da`. Only the numerical
transcription is recorded here; the photographs and game content are not
published. The [SCI test checklist](../cdda-sci-fault-test.md) describes these
two variants, which add different telemetry from the earlier PIO/DMA pair.

The user's paced-run observation was: “Paced seemed to work, but in the intro
the video would eventually freeze and it would skip the video. Not sure if
that was a problem in the old reader (it was in some games).” This is a
reported intro-video problem and uncertainty about its history, not a
confirmed gameplay result, calibrated performance measurement or CDDA result.

### Baseline STOP trace

The first photograph reports **14 STOP — IMAGE READ FAILED**.

| Legend | Raw hexadecimal words |
|---|---|
| `FN ARG FLAG G BAD` | `00000002 00000000 00000000 4B554947 00000000` |
| `CMD LBA N BPS` | `00000011 00069319 0000000D 00000800` |
| `IO LBA N DONE STEP` | `0006931F 00000003 00003000 00000003` |
| `IMG PRE STOP SD` | `00000005 00000003 00000003 00000003` |
| `DST BLK ERR CARD` | `0C7F4500 0000292F 00000001 03A04840` |

| Field | Decoded value |
|---|---|
| Function / argument / latched hook fault | GD EXEC `2 / 0 / 0` |
| First guard / mismatch mask | Expected `4B554947`; all four inspected guard words match (`BAD = 0`) |
| Command | BIOS DMAREAD, decimal `17` |
| Request LBA / sectors / bytes per sector | `430873 / 13 / 2048` |
| Failing callback LBA / sectors | `430879 / 3` |
| Credited bytes / current sector budget | `12288 / 3` |
| Image / pre-stop / outer stop / final storage results | `5 / 3 / 3 / 3`: image I/O error and storage TIMEOUT results |
| Original destination | `0C7F4500` |
| Successful physical-block fetches | `10543` |
| GD service error | `1 = KUI_GD_ERROR_IO` |
| Last attempted physical card LBA | `60835904` |

The 26,624-byte request has a credited six-sector prefix:
`430873 + 12288 / 2048 = 430879`. The failing callback requests three sectors
(6,144 bytes); seven request sectors (14,336 bytes) remain uncredited. As
above, a failed callback may partially touch its destination, and its
uncredited bytes are not completed data. The request `[430873,430886)` is
within the recorded track 15 logical range. The repeated outer TIMEOUT does
not establish a second independent CMD12 timeout.

### Baseline SCI fault trace

| Legend | Raw hexadecimal words |
|---|---|
| `PHASE REASON EXPECT POLLS` | `00000002 00000002 00000080 00000002` |
| `SSR SCR SMR BRR` | `000000E4 00000070 00000080 00000000` |
| `CHCR1 TCR1 DMAOR CHCR2 TCR2` | `00004911 000001C2 00008201 000012C1 00001307` |
| `DMA START OK FALLBACK` | `00002930 0000292F 00000000` |
| `CALL PLAY20 PLAY21 BAD` | `000005A8 00000000 00000000 00000000` |

The [source-defined enums](../../src/loader/sci_sd_bus.h) decode phase `2` as
the DMA transmit-clock feed and reason `2` as SCI error bits. Expected `80`
is the TDRE mask, and polls `2` means two software loop samples for the
captured wait. In the [feed implementation](../../src/loader/sci_sd_bus.c),
SSR `E4` intersects the `ERRORS = 38` mask at bit `20`; that error check takes
priority over the expected flag. The captured branch therefore reports an
SCI error, rather than reaching its software poll limit.

The [Renesas SH7750 hardware manual](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware),
Rev.7.02, section 15.2.7, pages 668–670, identifies bit `20` as the SCI
receive-overrun flag ORER. Thus `E4` records TDRE, RDRF, ORER and TEND;
framing and parity error flags are clear. ORER is set when another byte
finishes arriving while the previous receive register remains full. This
identifies the immediate failure as receive overrun; it does not establish
the arbitration delay or which other device caused it.

The channel 1 remaining count is `450` (`1C2`); channel 2's is `4,871`
(`1307`). The other register words are preserved raw. The snapshot was
captured before driver cleanup, but its successive register reads are not
atomic and an active channel can progress between reads. It does not
establish permanent channel ownership or the underlying cause of the SCI
error.

The resident's linked bus instance reports **10,544** started DMA payload
blocks and **10,543** successful bus completions, with zero guarded PIO
fallbacks. The GD-call count is **1,448**, with no accepted PLAY20 or PLAY21
and a zero latched hook-fault word. These counters exclude the separate high
launch stage. A successful bus completion does not itself prove the later
SD CRC comparison or whole image request succeeded.

The printed footer **PHASE 0: NO SCI FAULT CAPTURED** is an unconditional
legend in the [report renderer](../../src/loader/retail_sci_observe.inc).
It applies only when the numeric phase is zero. This baseline's numeric
phase is `2`, so the footer does not negate its captured fault.

### Paced SCI trace

| Legend | Raw hexadecimal words |
|---|---|
| `PHASE REASON EXPECT POLLS` | `00000000 00000000 00000000 00000000` |
| `SSR SCR SMR BRR` | `00000000 00000000 00000000 00000000` |
| `CHCR1 TCR1 DMAOR CHCR2 TCR2` | `00000000 00000000 00000000 00000000 00000000` |
| `DMA START OK FALLBACK` | `0000610E 0000610E 00000000` |
| `CALL PLAY20 PLAY21 BAD` | `00001B35 00000001 00000000 00000000` |

Numeric phase zero means no SCI fault was captured for the current lease;
the zero register fields are capture-state fields, not live zero-valued
hardware measurements. Successful reacquire clears the numeric phase, and
these fields must not be treated as a continuous fault-free history.

The reported totals are **24,846** DMA starts and **24,846** successful bus
completions, zero guarded PIO fallbacks, and **6,965** GD calls. One PLAY20
request and no PLAY21 request were accepted; the latched hook-fault word is
zero. This diagnostic records accepted-command counts without the PLAY20
parameter triple. It does not establish actual CDDA playback or audio output.

### What this additional pair establishes

The baseline now identifies a specific software failure branch: SCI error
bits during an already-started receive DMA's transmit-clock feed. The paced
report records matching start/completion totals and no captured SCI fault
for its current lease, while the user still reports intro-video freezing and
skipping. The data narrows the receive/feed investigation; it does not prove
the pacing change fixes all reads or explains the remaining video behavior.
No calibrated throughput, confirmed gameplay stage or integrated CDDA result
is established by these three photographs.
