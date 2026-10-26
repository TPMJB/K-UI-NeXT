<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Evolution New demand-reader control @BUILD@

The owner reported that reader-only failed on 9956562f91d6, while VMU-only
with Background Off passed. Both used the same 384 KiB bottom reservation.
Virtual-card replies/storage are therefore not necessary to reproduce the
failure. The remaining lead is New's reader integration and support schedule.
The extent of VMU save/load testing in that latest report was not specified.

This next control removes only the controller-driven read-ahead heartbeat
from the failed reader-only configuration. New's demand reads, SDK SCI
callbacks, auxiliary code, ownership checks and protected memory geometry
remain. The virtual VMU stays disabled, matching the failed reader-only test.
The 192 KiB cache arena remains reserved; cold initialization plus the omitted
heartbeat prevents speculative production. This is a diagnostic comparison,
not an established fix or a change to the normal combined reader.

## Test once

From this extracted archive's root:

```sh
python3 install.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --variant reader-only
```

The installer accepts the verified previous 995 isolation session and retains
its original loader backups. No manual backup deletion, card preparation or
save reset is needed. The installed Games GUI remains 8adf1a039035.

Safely eject and cold boot. Select the same Evolution image with **SCI /
Background reader New (X) / Auto encoding / CD music Off / RAM patches Off**.
Keep the physical controller/VMU setup unchanged, with physical A2 empty.
Check the startup banner says:

```text
NEW DEMAND / PREFETCH OFF
VIRTUAL VMU OFF
READER SUPPORT HOOKS RETAINED
```

A missing-memory-card warning is expected. Report whether it passes the old
black-screen transition and plays the opening FMV. If it fails, report the
last working point or photograph any automatic error page. No long PVR
diagnostic recording is requested.

The same archive also includes the Standard/A2 control under `--variant
vmu-only`, with **Background Off**. Its runtime code is unchanged from the
passing 995 Standard control apart from build labels. A repeat of that
already reported pass is unnecessary for the initial demand-reader check.

## Restore

```sh
python3 install.py /run/media/apollonius/Dreamcast --variant restore
```

This restores both exact original pre-isolation loader files, including an
absent file, using the retained initial backup. Select **New (X)** again for
ordinary combined-reader testing. The installer leaves the GUI, profile,
card, journal, identity, game images and launch settings unchanged. Normal
on-console prepared-card validation/recovery still runs before gameplay.

## Interpretation

A pass would implicate the optional heartbeat work or state it produces.
A failure would leave New's demand/SCI path and shared support hooks as the
next focus. Neither result alone identifies a particular instruction fault.

Earlier timing shows the original video code waiting for exact scanline zero.
It does not prove that speculative PIO masks that scanline continuously: the
captured cache had filled, and hundreds of callbacks skipped production.
The old capture did not record the loop's getter/mask operands. Existing
linked ABI checks found no register-preservation violation.

The complete source history, exact build bindings, original-instruction
checks and reversible-installation checks are included. Console acceptance
remains pending.
