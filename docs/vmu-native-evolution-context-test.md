# Evolution first-transfer context diagnostic

Grandia II was reported as "just fine" on build `7753391a62e4`. Evolution 1
stopped on its first four-port controller query with phase `10`, detail `43`,
one submission and zero completions. Its SD reads, write phases and commits
were zero. This records a console refusal, not a save-file failure.

This package improves the failing check's diagnostic. It does **not** claim
to fix Evolution yet, and it retains every existing refusal condition.
Previously `AT/VALUE/EXPECT` still described the last successful chain read;
the context guard now reports the actual failed comparison. A new frame row
shows the original frame address, entry PR/SR and copied PR/SR. Successful
context checks use the same hardware/RAM read sequence; frame capture adds
three small state writes per completion.

## Install and retry Evolution 1

Extract this ZIP into a fresh folder. From that folder run:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1
```

This installs only the personalized `KUI/apps/games/retail-boot.kui` and keeps
the existing prepared card and its saves. Use normal mode: omit `--control`
and `--profile-runtime`.

Keep the same Evolution GDI, SCI/Standard, Auto encoding and controller setup
as the failed test. Keep CDDA, background loading and RAM patches off.

Photograph the entire next diagnostic, including `DETAIL`, `AT VALUE EXPECT
CHECK LEASE` and `FRAME ADDRESS PR SR COPY PR SR`. There is no need to retest
Grandia or SA2 for this diagnostic batch. If Evolution reaches its menu,
report that result before treating this as a completed compatibility fix.

## What has been checked

The actual Evolution executable and pinned mapping produce a valid host
replay of its first four physical DEVINFO packets. The replay covers both
TX banks, three allocator aliases, three manager aliases, three completion
PR aliases and inherited interrupt masks 6/15 (108 cases). The last first-bank descriptor matches the photo:
`0c248444 = 80030000`.

The target worker's frame copy, guard comparisons, relocation records and
cached constant publication were inspected. No justified correction to
those checks was found. The unresolved difference is on the console between
the detailed transfer check and the short final context check: stack/frame,
restored DMA source or another live control word. Those remain hypotheses
until the new witness identifies one.

Host fault injection changes witnesses only after the detailed transfer
check passes. Each case must still stop with detail `43`, report its actual
failed witness and publish no virtual response or save I/O.

## CHECK field reference

All values are hexadecimal. `CHECK` values beginning `0002` identify the new
context diagnostics. `AT`, `VALUE` and `EXPECT` contain the address, observed
value and expected value where applicable; `AT=0` denotes a register/scalar.

| CHECK suffix | Refused witness |
| --- | --- |
| `01` through `05` | Worker interrupt mask, stack bounds or stack canary |
| `06` through `09` | Bank, phase, inherited interrupt mask or source bank |
| `0a` through `0e` | Saved registers, completion return or hook identities |
| `20` through `23` | Maple DMA source, trigger, enable or active state |
| `30` through `32` | Manager pointer, lock pointer or held lock byte |
| `40` through `45` | Manager pending/source/bank/trigger/register selectors |
| `50` through `52` | Worker armed/pending/submission counters |

A failed RAM/MMIO read sets bit `80` in the corresponding `20`/`30`/`40`
suffix and reports `ffffffff` as the unavailable observed value. No failed
check authorizes DMA, virtual response publication or storage access.

The earlier wider-compatibility guide remains in
`docs/vmu-native-broader-test.md`. The current generic adapter still exposes
virtual A1 and uses SCI backing; this batch adds no A2, SCIF or CDDA feature.
