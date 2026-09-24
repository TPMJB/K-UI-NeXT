# Owner CMD17/CMD18 result — passed

The owner supplied a photograph of build `93b1e6022592` running the separate
read-only DOA2 SD comparison. The screen explicitly says **DATA AND STOP
CHECKS PASSED**. Stopping at that screen was intentional, not a launch crash.

Window LBA: `03615900`; reference CRC32: `29C9833C`.

| Physical blocks per call | CMD17 KiB/s | CMD18 KiB/s | CMD17 maximum us | CMD18 maximum us |
| --- | ---: | ---: | ---: | ---: |
| 2 | 395 | 392 | 2561 | 2598 |
| 8 | 395 | 486 | 10119 | 8258 |
| 10 | 395 | 490 | 12678 | 10246 |

CMD18 improved measured payload throughput by approximately 23% at eight
blocks and 24% at ten blocks; two blocks offered no benefit. The corresponding
longest larger calls were shorter with CMD18. This small same-window test
does not measure game frame time, cold-card behavior or end-to-end loading.

Exact tested source: `e0f6f5d217c46588a0f27f98c410b9d2bf4e536b`;
packaged PR merge: `93b1e6022592935ba180f581c96009fba3a4f567`;
CI run `36039191044`, SD benchmark artifact `10825791514`.
ZIP SHA256: `f3d16cff42eb54d7cc2a3d2aa34a0181047545b6c149d80acd1c92dd6bfc4f20`.

## Gameplay integration — owner confirmed

Use CMD18 only when the current request and physical extent provide at least
eight blocks, and read at most ten blocks per stream. Retain the existing
two-game-sector EXEC chunk, one-block cache, per-block CRC checks, destination
cache handling, caller interrupt-state restoration and guarded resident area.
Consume blocks directly through the existing cache; no 5 KiB read-ahead buffer
and no speculative file reads. Stop before an extent/discontinuity, at a stream
limit, or when the image request returns, including mode/IO/CRC errors. No
open stream survives pin release or a return to the game.

The stage already validates the manifest and initializes the physical card
before loading IP/executable data. The candidate copies that validated map
and adopts the idle card state in the resident instead of repeating decode
and SD initialization there. Adoption replaces every bus callback with the
resident's own code; no temporary-stage pointers survive. The removed cold
code makes room for streaming without enlarging the reserved resident area.
Stage loading uses the same bounded transport. The owner has now run the
integrated build and reports it substantially better and really playable.

Owner observations: about 32 seconds from character selection to the first
stage; slower character motion during the first approximately eight seconds
of a fight; slowdown during the first approximately ten seconds of an FMV.
These are approximate owner observations, not instrumented frame timings.

Pin: `baseline/doa2-cmd18-ed31d522c847`, exact packaged merge
`ed31d522c8475b88bd40afa366fe3f7bdf143985`, source
`750982e55efbec666922cbf52e6a8a8d5ce72557`.
CI run `36041393177`, SD update artifact `10826433905`.
ZIP bytes: `8134517`; SHA256:
`a3c64bd3a6d70cc0369a32aaa04f563af9bea72ab8603e20307d3c5cb5b18bc4`.
The baseline ZIP has been copied without alteration and its hash checked.
Keep `baseline/doa2-sd-6c02bd8b22f4` and its archived ZIP unchanged too.
Games work is paused at the owner's request while the boot CD is refreshed;
no repeated gameplay or transport test is requested.

Focused local validation: `ASAN_OPTIONS=detect_leaks=0 make test-retail-streams`
passed for protocol stream lifecycle/error cleanup, resident-local SD state
adoption and pin ownership, bounded run selection, and image request/extent
spans with cache/data checks. Native size, stack and instruction limits remain
mandatory. Only `kui_retail_sd_adopt`, called by resident initialization on the
high stack, joins the existing init-only stack accounting list; all game-time
stream functions remain charged to the resident stack budget.
