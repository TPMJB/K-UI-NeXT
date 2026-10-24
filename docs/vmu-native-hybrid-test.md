<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Native SCI hybrid scheduling test

This candidate changes the Background reader that gave smooth Evolution 2
movies but still interrupted gameplay in `74958b387827`. It retains background
DMA reception and receive-ahead, with Standard-sized foreground work limits.
Console performance is unverified until you test this build.

## Install

Extract the ZIP, then run from its root:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution2
```

Use the same prepared profile and game image as the previous test. Change the
mount path/profile if needed. `--list-games` lists the supplied mapping names.
The installer updates `KUI/apps/games/retail-boot.kui`; the prepared card image,
profile and journal are preserved. Safely eject, then cold boot.

Use **SCI, Background loading On, Normal speed**, Auto/default encoding,
CD music Off, and RAM patches Off. A real VMU can remain in **A1**; leave
physical **A2 empty**. The virtual card still appears in A2. The Normal and
Aggressive frontend choices use the same eight-block cap in this experiment.
This package contains the native Background module; switching Background Off
does not turn it into the independently linked Standard module.

## Test

1. Play the same opening FMV that was smooth in the previous Background build.
2. Repeat the gameplay section that had three- or four-frame hangs. Compare
   their frequency and severity, and check input response and scene loads.
3. Save to virtual A2, fully power off after saving completes, reboot, and load.

Report the displayed build, FMV result, gameplay result, and save/reload result.
Use the same image variant for both builds. Keep the previous installer ZIP to
restore its module with the same profile and game selection.

## What changed

| Entry | Maximum work |
| --- | --- |
| REQUEST, CHECK, ordinary GD entry, reader interrupt | One delivered 512-byte SD block |
| EXEC | Eight delivered SD blocks, at most 4 KiB of source data |
| Any delivery visit | Ten loop iterations, including failed/retry iterations |
| Any entry using PIO fallback | One received SD block |
| EXEC DMA wait | 1,024 poll iterations shared across its blocks |

CHECK polls DMA once without a DMA wait loop. An unfinished DMA remains active
between entries, and SCI completion interrupts continue feeding the pipeline.
Repeated EXEC visits can make progress when interrupts are masked. The DMA
timeout accumulates pending polls across visits instead of treating a yielded
visit as a failed transfer. Existing CRC/retry and guest DMA ownership rules
remain enabled.

These are work-count limits, not a measured millisecond deadline. Card-command,
token-search, reset and VMU storage waits retain their existing timeouts.
Slicing those waits is separate work; the larger prototype did not fit the
unchanged resident boundary. The compiler's address-space workaround and the
original stack/guard limits remain in force. This is a native-game test, not a
new Windows CE or combined CDDA/VMU build.

The ZIP includes exact source history, checksums, linked checks and the host
regression evidence. Host tests establish bounds and correct output, not
Dreamcast frame timing.
