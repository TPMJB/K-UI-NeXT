# Boot CD refresh: original badge and hold-B recovery

This package updates the boot CD only. Keep the playable SD files already
installed from build `ed31d522c847`; there is no runtime or Games update here.

## Install and use

Burn `kui-bootstrap.cdi` as a disc image onto a new boot CD-R. Normal startup
shows the original K-UI badge, including `github.com/TPMJB`, under the Sega
logo, then loads `/KUI/runtime.kui` from SD as before.

To stay in the CD tools, hold **B from power-on** until the built-in CD
diagnostics screen appears. Release B, then press the **right trigger (R)**
to start the benchmark runner. B is checked during the startup grace period,
through SD runtime loading, and before handoff; once seen it stays latched
for this boot. A missing or invalid SD runtime also returns to the CD tools.

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

For optical measurements, wait for the CD tools, replace the boot CD with a
known-good retail GD-ROM, and close the lid before pressing R. The tools and
fonts are already in RAM. The CD menu does not offer a full-disc capture.

## Next console check

One normal boot should show the badge and enter your existing SD launcher.
One boot holding B should stay in the CD tools with `R: Bench` visible.
That is enough to check the refreshed boot path; another Games comparison or
storage sweep is not required. The benchmark runner is available whenever
you choose to use it.

## Preserved playable Games build

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
