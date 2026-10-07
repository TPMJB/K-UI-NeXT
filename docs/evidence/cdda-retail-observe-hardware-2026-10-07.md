# Profile 14 stopped retail observation — 2026-10-07

The user ran the retained observer at build `324c330bdb6c` after the corrected
profile 13 preflight. The game reached approximately its second introductory
screen and played sound before the reader stopped. The user described the
sound as repeating roughly every three seconds. This is a listening report,
not a measured period or an identification of the sound's source.

The values below reproduce the reviewed photograph transcription. No separate
image reading, resource-ownership measurement or audio analysis is claimed.
The photographs are not copied into this source record. Register values and
report masks are preserved as hexadecimal words.

## Stopped path

| Screen field | Raw hexadecimal values |
|---|---|
| Build | `324c330bdb6c` |
| `FN ARG GUARD` | `00000002 00000000 00000000` |

Function `00000002` is GD EXEC. The standard native reader's
[`step` path](../../src/loader/retail_resident.c) reports this stop when
`service.error == KUI_GD_ERROR_IO`; the corresponding
[image-read callback](../../src/core/retail_gd.c) sets that error when the read
operation fails. The reported stop is therefore an image-service I/O failure.
The photographed guard-fault word is zero; this is not a reported guard or
memory-validation stop.

This latched zero is not a fresh inspection of all four stack-guard words:
the terminal read-error path does not return through the assembly exit check.
The replacement diagnostic must inspect the current guard before rendering.

The screen does not expose the underlying storage error, failing logical LBA,
resolved track/file or physical card LBA. The cause of the read failure remains
unidentified.

## Command and sample totals

| Report legend | Raw hexadecimal values | Safe interpretation |
|---|---|---|
| `CALL 20 21 BAD N` | `00000841 00000000 00000000 00000000 00000009` | 2,113 observed GD calls; zero accepted PLAY20 and PLAY21 requests; guard fault zero; nine sound samples |
| `20F` | `00000000 00000000 00000000` | No recorded first PLAY20 parameters |
| `20L` | `00000000 00000000 00000000` | No recorded latest PLAY20 parameters |
| `21F` | `00000000 00000000 00000000` | No recorded first PLAY21 parameters |
| `21L` | `00000000 00000000 00000000` | No recorded latest PLAY21 parameters |
| `CPU TMU AICA SKIP` | `00000019 00000008 00000072 00000000` | Changed-field masks `19`, `08`, `72`; zero skipped sound sweeps |
| `KEY0 KEY1` | `00000003 00000000` | Configured KYONB bits for channels 0 and 1 were sampled; no upper-half key bits were recorded |

The zero accepted PLAY counters do not attribute the user's reported repeating
sound to CDDA. The sampled KYONB bits record configuration; they do not prove
that those voices were active or audible. This observer starts no CDDA audio.

## CPU snapshots

| Field | First | Latest |
|---|---|---|
| SR | `60000101` | `60000000` |
| VBR | `8C00F400` | `8C00F400` |
| GBR | `8C000000` | `8C000000` |
| Caller PR | `8C08D26C` | `8C08D55A` |
| Caller SP | `8C00F39C` | `8C00EDE4` |
| MMUCR | `00000000` | `00000000` |

`C N` is `00000019 00000841`: change-mask bits 0, 3 and 4 identify SR, caller
PR and caller SP; the CPU sample count is 2,113. The first/latest differences
are consistent with that mask. No caller timing or stack high-water bound is
derived from these snapshots.

## Timer snapshots

| Field | First | Latest |
|---|---|---|
| TSTR | `00000001` | `00000001` |
| FRQCR | `00003E0A` | `00003E0A` |
| TCOR0 | `FFFFFFFF` | `FFFFFFFF` |
| TCNT0 | `FFFCAAB5` | `FFA8AFA4` |
| TCR0 | `00000002` | `00000002` |
| TCOR1 | `FFFFFFFF` | `FFFFFFFF` |
| TCNT1 | `FFFFFFFF` | `FFFFFFFF` |
| TCR1 | `00000000` | `00000000` |
| TCOR2 | `00BE41F0` | `00BE41F0` |
| TCNT2 | `004528B9` | `004528B9` |
| TCR2 | `00000020` | `00000020` |

`C N` is `00000008 00000841`: only the observed TCNT0 field changed; the timer
sample count is 2,113. These call-bound snapshots do not measure the longest
interval without a GD call or establish a timer that a CDDA engine may own.

## Sound snapshots

| Field | First | Latest |
|---|---|---|
| Master control | `00000010` | `00000010` |
| ARM control | `00000301` | `00000300` |
| G2 DMA enable/start mask | `00000000` | `00000000` |
| ARM IRQ enable | `00000040` | `00000040` |
| ARM IRQ pending | `000005C0` | `00000780` |
| Selected channel-register hash | `813B0345` | `980266F3` |
| Configured key mask, channels 0–31 | `00000000` | `00000003` |
| Configured key mask, channels 32–63 | `00000000` | `00000000` |

`C N` is `00000072 00000009`: change-mask bits 1, 4, 5 and 6 identify ARM
control, ARM IRQ pending, the channel-register hash and lower-half key mask;
nine sound samples were recorded. `DMA F L OR CHG` is
`00000000 00000000 00000000 00000000`.

Nine samples agree with the observer's first-call/every-256-calls schedule for
2,113 calls and no accepted PLAY requests. The sweeps are sparse and sequential,
not atomic. Zero observed DMA masks and zero skipped sweeps do not establish
that DMA, sound channels or sound RAM are available between samples.

## Next diagnostic

This run records an early reader stop with an I/O error and
preserves the available observation state. It does not establish retail CDDA
compatibility or a root cause for the repeating sound.

The next diagnostic should retain the failing read's logical LBA, sector
count/size and committed progress, resolved track/file and physical card LBA,
and the underlying storage failure code. Those values are needed to distinguish
mapping/read-request failures from a card-transport failure. They cannot be
reconstructed from these four observation pages alone.
