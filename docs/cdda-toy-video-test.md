# Toy Commander three-sector video-streaming test

This is the first data-streaming experiment after the clean CDDA result on
`7b55156aafa2`. The audio worker is byte-for-byte identical to that build.
The only runtime policy change is the maximum synchronous GD data batch:
**three logical sectors per EXEC instead of two**. The final partial batch
still clips to the requested count.

The eight-block audio ring, cursor evidence, STOP fence, service/read limits,
original PCM, sound allocation, native driver, SCI receive pacing and CRC
checks remain the same. Card ownership, stream closure and bus release still
finish inside every read callback. No cache prototype or background reader
is linked into this test.

## Why try this

The successful console run reports zero audio recoveries and handoff gaps,
but the user still sees visual stalls. GD delivered 5,261 sectors through
2,670 measured data steps, with a mean completed step of 8.2502 ms and a
maximum of 10.4474 ms. See the
[console evidence](evidence/cdda-eight-block-console-clean-2026-10-08.md).

When a game calls EXEC once per frame, a two-sector cap can limit delivery
even when the card could supply more. Three sectors increase admitted payload
per call by 50% and can reduce callback/stream setup counts for larger requests.
The synchronous callback also lasts longer. Extrapolating the old mean suggests
approximately 12 ms; that is a planning estimate, not a measured duration or
deadline. Delayed interrupts, card tails, decoding cost and other bottlenecks
can still affect video.

This candidate has not run on the console. Neither its build checks nor its
synthetic tests establish smooth video or continued audible CDDA on hardware.

## Install one file

With the Dreamcast powered off:

1. Keep `K-UI-CDDA-Eight-Block-Test.zip` and its `7b55156aafa2` runtime as the
   clean-audio fallback. Keep the ordinary 1.8.5 reader backup as well.
2. Keep the working runtime at `/KUI/runtime.kui`, the same original GDI,
   all 15 original track files and the existing card-path configuration.
3. Copy only `pilot/15-toy-gd-three-sector.kui` from this ZIP over
   `/KUI/apps/games/retail-boot.kui`.
4. Safely eject, cold boot with **SCI**, and launch Toy Commander with **A**.

The bundle contains one installable runtime and no game files. Extracting it
installs nothing. The same original raw image and standard reader apply.

## Compare the same intro

Let the intro run without skipping. Record whether visual holds shorten,
whether moving action becomes smoother, and whether music stays continuous.
At the first menu, hold **A+B+X+Y+Start** and photograph the build ID plus
pages **0 through 7**, as before. Include a short video if possible.

The snapshot remains API 8, 448 bytes. The
[eight-block report legend](cdda-toy-eight-block-test.md) still describes the
fields, except page 5 row 1 column 2 now reads `00000003`. Page 5 row 2
column 3 must be at most `00000003`. Linked addresses and stack telemetry
must be interpreted against this candidate's `build.json`.

A successful result needs both better visual behavior and retained continuous
audio. A lower read-step count alone does not prove improved frame pacing.
For any regression, restore the preserved eight-block runtime.

## Review and reproduction

The full source snapshot, host tests, hardware-run transcription, actual linked
ELF/map/stack evidence and member checksums are included. The fixed-three
instruction audit checks the emitted cap store, separate GD_EXEC argument,
dispatch target and delay slot. All other layout/stack/instruction gates remain.
The worker hash must exactly equal the successful fixed-two worker.

All 22 host regression suites pass with ASan/UBSan, including eighteen mixed
GD2/GD3 scenarios using the actual GD core and full audio worker. The synthetic
consumer checks exact PCM, data payloads, progress, request handles and physical
write destinations. Short commands remain continuous across ring wraps; a
deliberately long 129-sector command still requires conservative audio recovery
with both batch sizes. See the
[mixed model limits](evidence/cdda-toy-video-gd-model-2026-10-08.md).

```sh
python3 tools/test_toy_pilot.py
make -f Makefile.toy_pilot BUILD=build/toy-video GD_FIXED_STEP=3 all
```

The cross-build needs the SH-4 toolchain. The package records one exact local
source checkpoint without claiming a public repository push. Historical
eight-block cadence evidence and the separate source-only SCI cache prototype
are retained for review; neither is a hardware video result for this candidate.
