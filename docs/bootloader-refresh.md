# Boot CD refresh: graphical menu, storage and recovery

The bootstrap artifact contains the boot CD and an optional card utility for
testing the same bootstrap without burning another disc. For standalone SCI microSD or
IDE/CF, also install the runtime and Games files from this same run's SD-update
artifact. See [storage-transports.md](storage-transports.md). Older accepted
SCIF runtimes can still use this CD, but do not contain SCI/IDE game readers.

This CD also has a [read-only ext4 runtime loader](ext4-bootstrap.md), so a future
compatible ext4-capable runtime can be installed through card updates. The
current runtime's apps still require exFAT/FAT32: keep the working card format
for this refresh. The CD reader does not add ext4 writes, filesystem repair or
app support.
It also supports a retained `/KUI/recovery.kui` image and the future same-card
FAT32-boot/ext4-data layout described in [boot-recovery.md](boot-recovery.md).
No ext4 repair program is bundled; current apps intentionally reject that split
layout until the runtime gains ext4 support.

## Check the final artwork without reburning

The owner confirmed normal SCIF boot with the `82984` CD, then confirmed a
substantially faster launch after entering Card tools and checking the new
`18dd87d457fc` bootstrap build. The photographed measurement of the same
1,546,484-byte runtime reported 60 ms initialization and 2,771 ms load/check
(544.8 KiB/s), with three redraws totaling 818 ms. Drawing overlaps loading;
these durations must not be added. The old CD still loaded Card tools slowly,
as expected. This validates the redraw correction, not a timed comparison or
the newer optional override/artwork changes.

Use the **bootstrap-cd** download, but copy **only `KUI/tools.kui`** onto the
card. Keep `KUI/runtime.kui` and `KUI/recovery.kui` unchanged. This utility is
the exact new CD-bootstrap executable in the existing version-1 envelope;
it is not an ext4 repair program.

1. Boot the existing compatible graphical CD (the `82984` build has Card tools),
   pause its automatic startup and choose **Card tools**.
2. Confirm the red artwork, then press **B** to open the menu. Its header should
   say **Card boot / recovery** and show the new package's build ID.
3. Choose **Start K-UI** and confirm the normal runtime opens. The prior loading
   correction is already confirmed; no repeat timing measurement is requested.

Diagnostics → Measure load time remains available for later troubleshooting.
It reads and verifies the runtime without executing it or writing the card.

The log separates device initialization from loading/checking and reports the
loaded build and effective KiB/s. Loading/checking includes partition and
filesystem reads, checksums, transport handoff validation and UI callbacks; it
excludes disconnect, final report drawing, the handoff delay and runtime
startup. Measurement uses the normal filename policy: if `runtime.kui` fails,
`recovery.kui` may be measured instead, with the fallback/build shown in logs.

The existing CD still performs the initial `tools.kui` load at its old speed.
This card utility does not permanently update the burned CD. The short check
previews the final artwork and boot menu before using another CD-R; it does not
exercise CD-origin automatic selection of `boot.kui`. No card formatting or new
runtime installation is needed. A CD without a Card tools entry cannot use this
particular shortcut.

## Install and use

Burn `kui-bootstrap.cdi` as a disc image onto a new boot CD-R. Normal startup
shows the original K-UI badge, including `github.com/TPMJB`, under the Sega
logo, then the red Dáinsleif artwork and startup countdown. Any input opens the
graphical menu. After three seconds without input,
unattended CD startup searches SCIF, SCI and IDE/CF. On each device it tries
optional `/KUI/boot.kui`, then `/KUI/runtime.kui` and `/KUI/recovery.kui` when an
image cannot load. The package deliberately leaves `boot.kui` absent, so normal
startup still loads the runtime directly. A bootstrap already loaded from the
card skips the optional override to avoid recursion.
It accepts the existing FatFs layouts or the clean ext4 profile documented
above. Compatible runtime updates can reuse this CD; a bootstrap bug fix or
unsupported format change may still require a replacement.

Any input pauses automatic startup. Select Start K-UI, Recovery, Card tools,
Diagnostics or Help with Up/Down and A. Left/Right on Home chooses Auto, SCIF,
SCI or IDE/CF for this session; an explicit source does not fall back to another
device. Manual **Start K-UI** always bypasses `boot.kui`; Recovery and Card tools
retain their separate fixed paths. Hold B during startup to keep those built-in
CD paths available if an optional override launches but hangs. B returns or
stops the current operation, and a later attempt can still
run. X on Home loads only `/KUI/recovery.kui`, useful when a checksum-valid normal
runtime hangs after launch. Card tools loads only `/KUI/tools.kui`; this
bootstrap package supplies the measurement utility as an optional manual copy.

If loading fails, return to Home and retry with A after checking or inserting
the SD card while idle. Do not change cards during loading or diagnostics.
Only use live card insertion with a suitable SD socket; power off to change
adapters, wiring, boards or IDE/CF hardware. There is no automatic insertion
polling. Preserve a known-working recovery image during ordinary updates.

| Menu/control | Action |
| --- | --- |
| Diagnostics → Optical probe | Probe the inserted optical disc |
| Diagnostics → Write/read test | Confirm with A before creating temporary test data |
| Diagnostics → Save log | Confirm with A before saving a report |
| Diagnostics → Benchmarks | Confirm with A; run settings from `/KUI/bench.cfg` |
| Diagnostics → Measure load time | Read/verify using normal boot selection and show phase timings; no execution or writes |
| Y | Open the log viewer |
| Up/Down in log | Scroll |
| Left/Right in log | Pan long lines |
| Start in log | Jump to newest lines |
| B | Go back or stop safely |

Benchmarks never start automatically or from a trigger shortcut. They use the
existing runner and options from SD, including saved settings followed by
explicit `bench.cfg` overrides. Missing configuration uses defaults; an
unavailable SD card or rejected configuration prevents the benchmark run.
The default sections are optical, hash and SD. Selected SD/capture benchmarks
write temporary test files; reports save automatically when a run finishes or
stops. Existing game files are preserved. Use the existing benchmark guide to
choose longer sweeps; this boot refresh does not request a new sweep. Ordinary
storage checks follow the Home source; benchmark transport follows `bench.cfg`.
These CD tools continue using FatFs. Ext4 bootstrap loading does not enable
their log writes, settings or storage benchmarks on ext4.

For optical measurements, wait for the CD tools, replace the boot CD with a
known-good retail GD-ROM, and close the lid before selecting the probe or
confirming a benchmark. The tools and
fonts are already in RAM. The CD menu does not offer a full-disc capture.

## Next console check

One normal boot should show the badge and enter the matching launcher from the
selected device; confirm its storage transport in Diagnostics.
Pause automatic startup, check Home/Help/log navigation, and verify B can stop
an attempt without preventing a later retry. Recovery should load the retained
image, or return to Home if absent; Card tools should report its missing file
when none is installed, or open the new menu when this utility is copied.
Measure load time must return after validation without starting an image.
Confirm that write/read, save-log and benchmark actions
show confirmation first. Normal SCIF boot from `18dd87d457fc` and its redraw
correction are confirmed. The newer optional override and red artwork still
await console validation. SCI and IDE/CF paths retain
the hardware status and short
integrity/game check described in the storage guide.

## Later boot-menu updates without another CD

The optional `/KUI/boot.kui` hook lets a compatible bootstrap utility replace
the menu on unattended CD startup. Future releases may provide an explicit
update for that filename; for this release, leave it absent for faster startup.
See [the override and recovery contract](boot-recovery.md#optional-card-update-for-the-boot-menu).
It uses the existing version-1 envelope and validated transport marker, with no
new on-disk ABI. Updates to code that runs after the CD reads that image can
then arrive on the card. New unsupported hardware, filesystem or envelope
requirements that prevent the CD reading it may still require a new disc.

## Historical playable Games baseline

The owner reported the CMD18 build substantially better and really playable:
about 32 seconds from character selection to the first stage, slow character
motion during the first roughly eight seconds of a fight, and slowdown during
the first roughly ten seconds of an FMV. Further Games optimization is paused.

Rollback branch: `baseline/doa2-cmd18-ed31d522c847`.
Packaged merge: `ed31d522c8475b88bd40afa366fe3f7bdf143985`.
Source: `750982e55efbec666922cbf52e6a8a8d5ce72557`.
SD ZIP SHA256: `a3c64bd3a6d70cc0369a32aaa04f563af9bea72ab8603e20307d3c5cb5b18bc4`.

The previous `baseline/doa2-sd-6c02bd8b22f4` remains available too.

## Build verification

Packaging checks the exact original badge SHA256 and its MR geometry, then
reads the badge back from the generated CDI's bootstrap at offset `0x3820`.
`build.json` records the source, dependencies, compiler and CDI hash.
`SHA256SUMS` covers the files in this package. The native build checks both
CD and SD link targets. Console confirmation of the refreshed CD is pending.
