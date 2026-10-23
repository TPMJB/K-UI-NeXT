# Native VMU latency comparison

The polling build `26b6d5d10fa6` produced only a marginal improvement on the
owner's SA2 test. SA2 still has substantial lag and Skies has a slight gummy
feel; neither has an established matched no-VMU baseline. This package adds a
comparison and a gameplay timing capture using the same loader module.

Use the original GDI, SCI / Standard, Auto encoding, CD music off, background
loading off and RAM patches off for both tests. Keep the same controller,
display settings and game scene. One controller with no physical VMU is the
simplest comparison. The commands preserve the existing prepared card files.

## 1. Compare without virtual VMU

Extract the ZIP into a new folder and run there:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game sa2 --control
```

Safely eject and launch SA2. Startup shows `NATIVE VMU CONTROL / NO VIRTUAL
CARD`. Continue without saving, start a new game and compare the first stage's
movement and camera with the previous VMU-enabled result. This mode provides no
virtual card, so it cannot load your virtual save. Your existing save stays on
the SD card for the next VMU-enabled launch.

The control keeps the same GD reader, bootstrap checks and owner CPU/cache
restoration. It skips VMU preparation, native scans, heap reservation and
Maple/free/initializer hooks. If it is also choppy, the VMU hooks cannot explain
the entire problem. A faster control attributes a difference to the VMU path
as a whole; that includes its 192 KiB reservation as well as hook execution.

## 2. Capture the VMU path during gameplay

With the card mounted again:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game sa2 --profile-runtime
```

Launch with the same settings. The virtual card works normally, including
loading an existing save. Reach gameplay, then **hold both triggers fully and
B for about a second**. Release them and play normally for about 20 seconds.
The diagnostic screen then stops the game. Photograph that screen and power
off. The gesture starts a fresh window and discards loading/menu measurements.
It requires 30 consecutive controller responses. An unsupported timer uses a
1,200-poll fallback, so the capture duration can differ from 20 seconds.

The screen reports calls, valid/invalid samples, total and maximum timer ticks
for submission, completion, heap frees and storage loans. One tick is 1.28
microseconds when the exact owner TMU0 profile is admitted. Completion includes
storage-loan time: those two totals overlap. The measurement covers the C body
and includes sampling overhead; it excludes the assembly entry/exit saves.
It also records owner CCR values and instruction-cache-off samples. No timer
or cache register is written by the profiler. Invalid timing stays explicit.

Timing mode adds sampling overhead. Use ordinary mode or the previous build
for subjective VMU-enabled performance comparisons, and the timing mode to
locate long masked scopes. The screen appears only after the current transfer
has completed and no storage transaction is in flight. Normal save journal,
readback and bus-restoration rules remain active throughout.

## Return to ordinary VMU mode

Run the command without either diagnostic flag:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game sa2
```

Use `--game skies` for Skies or `--game ps2` for Power Stone 2. SA2's initial
stage is the shortest matched comparison. The Skies ship-control scene is
useful in timing mode; a no-VMU control cannot load its virtual save, so do not
spend hours reaching that scene merely for this test.

## Validation boundary

The startup mode is a canonical typed 32-byte immutable object with three
values: ordinary, no-VMU control and runtime profile. Personalization changes
only its audited span, the prepared profile, mapping seed and envelope
checksums. Exact linked instruction models cover the ordinary/profile relay,
the no-VMU early return, and storage initialization that bypasses every VMU
recovery/write callback in control mode. Malformed modes stop before storage.

Host tests exercise valid and incompatible clocks, counter wrap, saturation,
fresh gameplay-window reset, gesture debounce, timed and fallback capture
termination, no normal-mode timer sampling, and safe idle state at the report.
These establish the instrumentation's contract; they do not establish the
cause of the owner's console lag before the comparison and capture are run.
