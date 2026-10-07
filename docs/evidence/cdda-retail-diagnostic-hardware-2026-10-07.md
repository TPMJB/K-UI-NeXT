# Replacement test 14 read failure — 2026-10-07

The user supplied five photographs, identified as 62002–62006, from replacement
observer build `72802334ebfc`. The stopped screen reports **IMAGE READ FAILED**.
The user reports that the reader has stopped at different introductory points:
this replacement run failed on the very first screen, while the original
`324c330bdb6c` run had reached approximately the second intro screen.
The values below reproduce the reviewed photo transcription; the photographs
are not published or copied into this source record. All raw words are
hexadecimal. Decimal counts and source-defined error names are stated separately.

## Terminal read trace

| Legend | Raw hexadecimal words |
|---|---|
| `FN ARG FLAG G BAD` | `00000002 00000000 00000000 4B554947 00000000` |
| `CMD LBA N BPS` | `00000011 0006927D 0000000D 00000800` |
| `IO LBA N DONE STEP` | `00069283 00000003 00003000 00000003` |
| `IMG PRE STOP SD` | `00000005 00000003 00000003 00000003` |
| `DST BLK ERR CARD` | `0C7A6500 00002662 00000001 03A04573` |

The [terminal renderer](../../src/loader/retail_observe.inc) captures these
fields before rendering. Their source-defined meanings are:

| Field | Decoded value |
|---|---|
| Function | GD EXEC, `2` |
| Argument / latched fault | `0 / 0` |
| First stack guard / mismatch mask | Expected `4B554947` guard; all four inspected guard words match (`BAD = 0`) |
| Command | BIOS DMAREAD, decimal `17` |
| Original request LBA | `430717` |
| Original request sectors / bytes per sector | `13 / 2048` |
| Current image-read callback LBA / sectors | `430723 / 3` |
| Credited request bytes | `12288` |
| Current EXEC sector budget | `3` |
| Image result | `5 = KUI_GAME_IO` |
| Storage result before outer cleanup | `3 = KUI_LOADER_SD_TIMEOUT` |
| Explicit outer stop result | `3 = KUI_LOADER_SD_TIMEOUT` |
| Final storage result | `3 = KUI_LOADER_SD_TIMEOUT` |
| Original destination | `0C7A6500` |
| Cumulative successful image-block fetches | `9826` |
| GD service error | `1 = KUI_GD_ERROR_IO` |
| Last physical card LBA passed to the transport | `60835187` |

The enum names follow [the image result definition](../../include/kui/game_image.h),
[SD result definition](../../src/loader/sd_reader.h) and
[GD service error definition](../../include/kui/gd_service.h). The reported
failure is an image/transport I/O error, with no latched fault or guard mismatch.

The request asks for 26,624 bytes. The credited prefix is six complete
2,048-byte sectors: `430717 + 6 = 430723`, exactly the captured callback LBA.
The failing callback requests three sectors, or 6,144 bytes. A further seven
sectors, 14,336 bytes, remain uncredited in the original request. An image read
may partially touch its destination before returning an error; the trace does
not establish how much of the failing callback's destination was written.
Those uncredited bytes are not counted as completed data.

The logical request `[430717,430730)` lies inside the track 15 data range
`[377422,549150)` recorded by the complete
[profile 13 preflight](cdda-preflight-hardware-2026-10-07.md). It is not a
cross-gap request according to that recorded map. The physical card LBA is a
separate transport address; it must not be interpreted as the disc LBA.

## Commands and observation counts

| Legend | Raw hexadecimal words |
|---|---|
| `CALL 20 21 BAD` | `00000305 00000000 00000000 00000000` |
| `20F` | `00000000 00000000 00000000` |
| `20L` | `00000000 00000000 00000000` |
| `21F` | `00000000 00000000 00000000` |
| `21L` | `00000000 00000000 00000000` |
| `CPU TMU` | `00000019 00000008` |

`00000305` is **773** observed GD calls. No accepted PLAY20 or PLAY21 request
was recorded before the stop. The replacement reports **AUDIO NOT SAMPLED**;
it provides no AICA, key-mask or G2-DMA observations. Audio-register sampling
was disabled, yet this run still encountered the image-read failure. That
result does not identify the cause of either run or prove that the earlier
sampler had no effect.

## CPU snapshots

| Field | First | Latest |
|---|---|---|
| SR | `60000101` | `60000060` |
| VBR | `8C00F400` | `8C00F400` |
| GBR | `8C000000` | `8C000000` |
| Caller PR | `8C08D26C` | `8C08D55A` |
| Caller SP | `8C00F39C` | `8C00EDC4` |
| MMUCR | `00000000` | `00000000` |

`C N` is `00000019 00000305`: mask bits 0, 3 and 4 identify changes to SR,
caller PR and caller SP; 773 CPU samples were recorded.

## Timer snapshots

| Field | First | Latest |
|---|---|---|
| TSTR | `00000001` | `00000001` |
| FRQCR | `00000E0A` | `00000E0A` |
| TCOR0 | `FFFFFFFF` | `FFFFFFFF` |
| TCNT0 | `FFFCAAB3` | `FFC07CC9` |
| TCR0 | `00000002` | `00000002` |
| TCOR1 | `FFFFFFFF` | `FFFFFFFF` |
| TCNT1 | `FFFFFFFF` | `FFFFFFFF` |
| TCR1 | `00000000` | `00000000` |
| TCOR2 | `00BE41F0` | `00BE41F0` |
| TCNT2 | `000EE47A` | `000EE47A` |
| TCR2 | `00000020` | `00000020` |

`C N` is `00000008 00000305`: only the observed TCNT0 field changed; 773 timer
samples were recorded. These call-bound snapshots do not measure elapsed read
time, locate the timeout check or establish ownership of a timer.

## What the timeout establishes

The storage result was already TIMEOUT before explicit outer cleanup. The
outer stop also returned TIMEOUT. This is not a successful image read followed
only by an explicit outer cleanup failure; the final result preserves the
earlier storage failure.

The source flow gives a more specific interpretation of `STOP = 3`: the failed
read-run closes its stream, so the later stream-stop call has no active stream
to stop. The [outer storage-stop wrapper](../../src/loader/retail_storage_impl.h)
then returns TIMEOUT when the SCI lease fails its
[health check](../../src/loader/sci_sd_bus.c), which requires an acquired lease
without a latched fault. This is a code-flow deduction of a latched SCI fault
or lost lease, not evidence of a second independent CMD12 timeout. The recorded
fields still do not identify the ready, command-response, data-token, payload
or internal cleanup wait that originally failed.

TIMEOUT is the observed software result, not proof of an underlying clock
defect, a calibrated elapsed-time violation or a faulty card. The exact failing
transport wait remains unresolved. The next paired diagnostic compares the
existing PIO and DMA-eligible payload paths, preserving the primary failure
separately from the outer stop result; it does not capture that first wait.
The current evidence establishes no retail CDDA playback, sound ownership or
service-gap deadline.
