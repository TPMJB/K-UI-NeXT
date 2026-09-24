# Games: independent image loading

Planning baseline: 2026-09-24, app source `bc5c8133bf76`. This is a development
plan, not a claim that K-UI currently runs retail games from images.

**Implementation update:** the first G1/G2 increment now provides the portable
GDI/boot-metadata services and a read-only Games browser in the SD runtime.
Host validation and the focused console checklist are documented in
[games-test.md](games-test.md). The first G3 resident read probe has passed on
the console: build `7a8493ae825e`, ten checks passed, 84 post-handoff SD blocks.
This accepts the original-fixture storage/handoff foundation through K-UI probe
ABI v1. [Evidence](evidence/games-resident-probe-hardware-2026-09-24.json).
The next increment implements selected-image allocation mapping and a bounded
request service reached through the actual GD BIOS vector. Its own test client
compares sampled post-shutdown SD reads with pre-handoff CRC references. This
increment has **passed on hardware** with DEAD OR ALIVE 2: build `c4cfd4585ec5`,
all 11 checks passed and 93 post-handoff SD blocks read.
[Evidence](evidence/games-selected-image-hardware-2026-09-24.json) and
[accepted test guide](games-image-probe.md). No repeat is requested.
That accepted probe does not execute a retail boot file or provide hardware SD DMA.
The separate first retail experiment is now implemented; its console test is
pending. See [the DOA2 launch guide](games-retail-test.md).

Runtime `57d53841c1ea` has now passed repeated ARMADA metadata inspection on
the console, with a stopped earlier inspection followed by successful use.
[Hardware evidence](evidence/games-armada-inspection-2026-09-24.json) accepts
that demonstrated path. G3's original-fixture post-shutdown reads are now
accepted separately. No repeat of either successful test is needed.

## First goal

Select a GDI from one Games app, launch a small independently authored test
program, and keep servicing its disc requests from SD after the launcher has
relinquished the machine. Then use the same path to attempt one owned retail
game. A library listing or a jump to a game's boot executable alone does not
meet this goal: later disc requests must also work.

The portable, read-only GDI image service and bounded metadata reader are in
place, and the original-fixture post-handoff read proof passed. Selected game
tracks are connected to a limited GD-vector request service, and the selected
DOA2 read path has passed on hardware. A separate experimental retail boot path
is implemented, with boot state and compatibility awaiting the console test.
The accepted read probes do not establish those capabilities.
The existing bootstrap CD remains the entry point; deliver updates on SD.

## What we can reuse

| Existing component | Reuse and boundary |
| --- | --- |
| Original K-UI interface, input and branding | One Games library with Advanced > Browse; preserve the existing visual style |
| `/Games` naming and paged browser | Discover current named dumps and numbered duplicates; no rename or write needed |
| GDI parser and sector helpers | Independently authored starting points; current scan parser accepts raw 2352-byte tracks with zero file offset, not every external GDI variant |
| FAT32/exFAT and media interface | Launcher-side SD access exists; game-time storage must be made independent of the shell's KOS threads and globals |
| Runtime package validation and shutdown model | Useful validation/lifecycle patterns; current bootstrap `arch_exec` path is not a retail loader |
| Physical CD player and GD Play | Keep as separate working features; neither supplies image-backed game-time CDDA or disc requests |

Current code has bounded image ISO9660 traversal, a Games browser and separate
original-fixture and selected-image resident test payloads, plus an experimental
DOA2 executable launch path. There is no IDE/CF adapter. The default branch remains an empty initial commit;
work from the current app branch based on `milestone/experiments`.

## Stages and acceptance

| Stage | Deliverable | Gate |
| --- | --- | --- |
| G1: image access | Strict GDI track map, bounded sector reads and boot metadata/ISO9660 lookup | Synthetic fixtures prove addresses, lengths, gaps, data/audio distinction, truncated files and malformed metadata; no capture-engine changes |
| G2: Games library | `/Games` discovery, title/region/product metadata, Advanced > Browse, clear unsupported-image errors | Existing K-UI GDI dumps and GDI-only folders list without a manifest or full hash scan; browse/cancel leaves files unchanged |
| G3: resident read proof | Separate loader payload, versioned launch request and owned test executable | On console, the test program reads known SD-image bytes through the intended game-facing request interface after shell teardown; request completion and failures are visible |
| G4: first retail title | First narrowly scoped compatibility profile | Reaches gameplay, crosses loading transitions and loads/saves through the physical VMU; a title screen alone is insufficient |
| G5: compatibility | Additional titles and required command/audio/SDK behavior | Each title gets a reproducible test record with device, build, settings, working behavior and limitations |

G1 and the thin G2 screen are implemented. Both the original G3 handoff/storage
test and the selected-image GD-vector read test are accepted. G4 status:

1. **Accepted for the tested DOA2 image:** bounded, validated resident maps for
   the selected GDI's actual track files. Fragmentation and both filesystem
   types have host coverage; the photograph alone does not establish those
   particular allocation/filesystem cases on hardware.
2. **Implemented subset accepted on hardware:** the independently sourced GD BIOS
   request/response calling convention, statuses, buffer bounds and command
   lifecycle exercised by our own executable through the actual vector. Probe
   ABI v1 is separate. The new command-17 path still uses CPU-driven serial SD;
   retail interrupts/callbacks and streamed reads remain unimplemented.
3. **First gameplay confirmed:** build `7fd48f11be02` reaches DOA2 gameplay
   through the independent reader. Bootstrap2 entry, stack placement and the
   required version-query command now pass that title's startup path.
4. **First SD optimization confirmed:** `3ebbf8f9846b` starts the game in about
   15 seconds, then takes about 10 seconds until Start works, versus roughly
   three minutes previously on that initial screen. Gameplay has minimal lag.
   Stage-start loads, FMVs and speech still suffer. These are owner estimates;
   measured throughput and physical VMU save/load remain open. G4 is partial.
   Next is a two-sector EXEC latency comparison with persistent block caching.

See [the first gameplay baseline](evidence/games-doa2-gameplay-2026-09-24.md) and
[the improved baseline and latency comparison](evidence/games-doa2-fast-io-2026-09-24.md).

### Current implementation and next performance work

The selected-image photograph closes the read-path hardware gate. The first
DOA2 launch path is now implemented. Do not repeat either accepted read probe.

- Launcher preparation validates the native GD profile, reads IP/boot CRCs,
  and produces a bounded read-only allocation map before filesystem teardown.
- The temporary high stage installs the resident before the owner's Bootstrap2. Its entry
  trampoline restores the original executable bytes before game execution.
- The compact resident occupies lower retired IP RAM, with a guarded private
  stack. Firmware, metadata/TOC and conventional upper bootstrap/VBR remain.
- Hook entry masks interrupts before switching stacks; guest buffers use
  coherent cached/uncached aliases. The resident uses no floating-point code.
- Serial ownership is scoped to each request with no game timer borrowed.
  Polling, status/mode and MISC vector behavior are implemented. Command17
  uses CPU copies; DMA interrupts, streaming and CDDA remain unsupported.

[The next test](games-retail-test.md) is **Y — Launch (experimental)** on the
same DOA2 image, followed by **A — Launch**. Record the last diagnostic screen
or furthest game progress. The owner has confirmed gameplay on the baseline above; loading-transition
and physical VMU acceptance remain separate. Preserve that baseline while
optimizing the SD receive/burst path and reviewing interrupt hold times.

Do not spend this stage on cover art, large compatibility menus or broad
format support. Dead or Alive 2 is the first retail candidate because
the owner has used it over serial SD in DreamShell. That experience is not proof
of compatibility with our new loader. Resident Evil Code: Veronica is another
later candidate from the owner's collection.

## Image service and handoff design

Start with the uncompressed raw GDI layout produced by our ripper, including
multiple data/audio tracks. Resolve filenames relative to the selected image,
validate integer arithmetic and every file extent, and explicitly distinguish
LBA from FAD. Test track boundaries, gaps, multisession filesystem offsets and
raw-sector versus 2048-byte user-data reads. Audio tracks must not become
filesystem data. Later formats get separate tested adapters.

Opening the library reads only bounded metadata and validates required files;
it does not run Advanced CRC or reread whole dumps. A dump's existing verification
record can be shown with its provenance; it is not a fresh integrity check.

The boundaries are `src/apps/games*` for the app, portable image and GD service
modules under `src/core/`, and separately linked `src/loader/` payloads. Their SD
packages live under `/KUI/apps/games/`. The selected-image package uses a
CRC-protected, pointer-free 64 KiB manifest with at most 99 tracks, 4,096 file
extents and 16 reference samples. These are versioned probe contracts, not a
general app plugin ABI or a finished retail loader interface.

Before handoff, stop music and CD playback, park/join workers, finish storage
operations and explicitly transfer device ownership. Validate payload sizes,
destinations and resident memory regions before overwriting any shell memory.
The resident test code owns bounded stacks/buffers and uses its independent
read-only SCIF backend. No FatFs or KOS state survives into the resident reader;
the validated allocation map connects selected-image reads to physical blocks.

Document the supported BIOS/GD request surface and retail boot state from
independently licensed sources. The proof program must exercise TOC/status,
read submission and completion, sequential and random reads, track boundaries,
and unsupported/cancel/error behavior. It checks returned bytes against
pre-handoff sample CRCs after the main runtime is gone. Host tests prove arithmetic
and state handling; only the console test proves this new vector handoff. Initially rebooting
to return to K-UI is acceptable; in-game return is separate compatibility work.

Design a read-only storage backend boundary for SD and later IDE/CF. Upstream
KOS exposes ATA block-device and DMA interfaces, but that does not constitute
an implemented or hardware-tested K-UI game backend. The current FatFs adapter
has one active volume; do not add concurrent mounts by reusing its globals.

## Independence and constraints

- Preserve source provenance and dependency notices. Reuse independently
  authored earlier K-UI additions only after the existing
  [reuse review](prior-work-reuse.md); do not import or mechanically translate
  DreamShell's ISO-loader implementation.
- Keep `src/core/capture.c`, `src/dreamcast/disc.c` and `src/core/command.c`
  unchanged. The [accepted reader](HANDOFF-disc-reader.md), its defaults and
  existing evidence are the baseline. No new full rip or speed census is needed.
- Respect the established SCIF serial-SD limits. Fast optical DMA capture does
  not establish fast game-time SD reads. Do not revive SCI on a stock rear port
  or promise that software makes streaming-heavy games perform like hardware DMA.
- Add image-backed CDDA, Windows CE and other SDK compatibility as explicit
  later capabilities. The physical Audio CD app is not that implementation.
- Defer ISO/CDI/compressed formats, VMU emulation, in-game return and automatic
  game patching until a basic loader works. Keep physical VMUs available.
- Do not commit game dumps or patch files. Test fixtures must be generated
  synthetic data and our own executable, not distributed retail content.
- Network hardware and salvage remain deferred at the owner's request.
  Boot-disc redesign follows Games; retain the current SD-update/fallback path.

## Sources and checkpoints

Repo audit: `src/core/recovery_manifest.c`, `src/core/data.c`,
`src/core/destination_file.c`, `src/core/diskio.c`,
`src/dreamcast/bootstrap.c`, `src/apps/gd_play.c`,
[app architecture](app-architecture.md), [reader evidence](hardware-evidence.md)
and [runtime package contract](sd-bootstrap.md).

External primary sources checked on 2026-09-24:

- [KallistiOS](https://github.com/KallistiOS/KallistiOS): independent platform
  foundation. Implementation must use this repo's pinned dependency revision,
  not silently substitute the latest upstream code.
- [KOS G1 ATA API](https://kos-docs.dreamcast.wiki/group__g1ata.html): future
  IDE/CF driver foundation; API availability alone does not prove game support.
- [KOS firmware syscall interface](https://kos-docs.dreamcast.wiki/syscalls_8h.html):
  one interface reference for the new request contract.
- [DreamShell architecture overview](https://github.com/DC-SWAT/DreamShell):
  documents why retail loading includes syscall emulation, CDDA and SDK-specific
  interrupt work. This is a scope reference, not an approved code dependency.

Store future implementation/test results in `docs/evidence/`, distinguishing
host tests, owner-reported results and console logs. This plan contains no new
game compatibility or performance measurement.
