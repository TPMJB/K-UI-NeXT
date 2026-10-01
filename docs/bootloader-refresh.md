# Boot CD refresh: standalone storage and recovery images

The bootstrap artifact contains the boot CD. For standalone SCI microSD or
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

## Install and use

Burn `kui-bootstrap.cdi` as a disc image onto a new boot CD-R. Normal startup
shows the original K-UI badge, including `github.com/TPMJB`, under the Sega
logo, then searches SCIF, SCI and IDE/CF. On each device it tries
`/KUI/runtime.kui`, then `/KUI/recovery.kui` if the first image cannot load.
It accepts the existing FatFs layouts or the clean ext4 profile documented
above. Compatible runtime updates can reuse this CD; a bootstrap bug fix or
unsupported format change may still require a replacement.

To stay in the CD tools, hold **B from power-on** until the built-in CD
diagnostics screen appears. Release B, then press the **right trigger (R)**
to start the benchmark runner. B is checked during the startup grace period,
through SD runtime loading, and before handoff; once seen it stays latched
for this boot. If neither image can load, startup returns to the CD tools.

Hold **X from power-on through the startup prompt** to skip the normal image
and load only `/KUI/recovery.kui`. This also bypasses a checksum-valid runtime
that hangs after launch. Keep a known-working recovery image when updating the
normal runtime. X below describes its different function once CD tools appear.

| Control in CD tools | Action |
| --- | --- |
| R trigger | Run benchmarks configured by `/KUI/bench.cfg` |
| B | Stop the current operation safely |
| A | Probe the inserted optical disc |
| X | Run the SD write/read check |
| Y | Save the diagnostic log to SD |
| Up / Down | Scroll the log |
| Start | Jump to the latest log lines |

Benchmarks do not start automatically when B selects recovery. They use the
existing runner and options from SD, including saved settings followed by
explicit `bench.cfg` overrides. Missing configuration uses defaults; an
unavailable SD card or rejected configuration prevents the benchmark run.
The default sections are optical, hash and SD. Selected SD/capture benchmarks
write temporary test files; reports save automatically when a run finishes or
stops. Existing game files are preserved. Use the existing benchmark guide to
choose longer sweeps; this boot refresh does not request a new sweep.
These CD tools continue using FatFs. Ext4 bootstrap loading does not enable
their log writes, settings or storage benchmarks on ext4.

For optical measurements, wait for the CD tools, replace the boot CD with a
known-good retail GD-ROM, and close the lid before pressing R. The tools and
fonts are already in RAM. The CD menu does not offer a full-disc capture.

## Next console check

One normal boot should show the badge and enter the matching launcher from the
selected device; confirm its storage transport in Diagnostics.
One boot holding B should stay in the CD tools with `R: Bench` visible.
One boot holding X should load the retained recovery image (or stay in CD tools
if it is absent). That checks recovery controls. The new SCI and IDE/CF paths
additionally need
the short integrity/game check described in the storage guide.

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
