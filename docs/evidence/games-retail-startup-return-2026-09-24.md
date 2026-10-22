# DOA2 returns before recorded GD dispatch, 2026-09-24

The user's photograph of build `f26d1a883109` still shows BIOS-menu return.
Caller PR is `0x8c012450`, caller stack `0x8c00f3d8`. BC, C0, direct-1000,
direct-10f0 and miscellaneous setup counts are all zero. Last route R6/R7,
route result, GD command/LBA and resident SD blocks are also zero.

The entry-routing correction did not resolve this startup failure. The earlier
setup-call/bypass hypothesis is unsupported by this result. Zero counters mean
no recorded resident dispatch; they do not by themselves identify the rejected
startup prerequisite or exclude memory corruption.

The handoff review found no concrete explanation in integer register-frame
ordering, SR/SSR bank handling or cache publication. Do not make another
speculative console change from this photograph alone.

At the time of that photograph, the exact executable was not available. At its load
base `0x8c010000`, caller PR maps to file offset `0x2450`. For a normal SH call,
the call instruction is at `0x244c` followed by its delay slot at `0x244e`.
The actual file is needed to identify the caller and trace its failure branch.

`tools/kui-startup-bundle.py` extracts the owner's existing raw-GDI IP.BIN and
1ST_READ.BIN into a small ZIP, with version/region and checksum metadata. It
requires only Python 3, reads source files with `rb`, refuses output overwrite,
and does not execute the game code. A single existing synthetic GDI fixture
confirmed exact extracted IP/executable bytes and unchanged source hashes.
No proprietary game bytes are committed or bundled with this helper.

That update changed only the extraction helper and this evidence record;
it requested no new console build. The owner subsequently supplied the ZIP.


## Exact startup files establish the collision

The supplied bundle identifies `DEAD OR ALIVE 2`, `T3601N`, `V1.100`, region
`U`, boot length 1,815,864 bytes, boot CRC32 `349d3ae0`, IP CRC32 `987b8ea5`.
SHA-256:

- IP.BIN: `234e888892ed916197633c2d12a16d7ad040d2fe93ca907798ef12983342141c`
- 1ST_READ.BIN: `105056d9ffef86bf5e470fa28dc5907443156f9ab1c6bc009f73661d9c047a85`

Targeted disassembly of those owner-provided bytes establishes:

1. Game startup at `0x8c1132d8..0x8c1132e4` fills the half-open range
   `[0x8c00c000, 0x8c00f400)` with word `0x41474553` (the SEGA stack marker).
   This necessarily overwrites the old guard at `0x8c00d000`.
2. Our assembly hook compares that guard against `0x4b554947`. On mismatch
   it sets `kui_retail_hook_fault=1` and returns `-1` before publishing the
   route source or calling C dispatch. None of the previous screen's route
   counters count this rejection.
3. The game's initialization follows `0x8c11587e` through `0x8c116e64` and
   `0x8c115884`. Driver table `0x8c14ed08` calls GD init through
   `0x8c1224bc -> 0x8c129bb4 -> *(0x8c0000bc)` (R6=0, R7=3), ignores that
   result, then requests command 24 through the same vector. There is no
   alternate GD entry needed to explain this failure.
4. Initialization failure is retried up to eight times. Its final failure
   branch reaches the call at `0x8c01244c`, whose PR is `0x8c012450`, exactly
   matching both photographed menu-return screens.

This is an identified memory collision with a complete static explanation
for the hardware result. It is not yet hardware confirmation of its repair.
Proprietary game bytes and disassembly remain outside the repository.

## Correction and bounded validation

- Restrict resident code/BSS to `[0x8c008300, 0x8c00bb00)` and move the guarded
  stack to `[0x8c00bb00, 0x8c00c000)`. The linker checks code/BSS placement;
  the existing native validator checks every retained runtime C frame plus
  its 256-byte assembly allowance against 1,232 available bytes.
- Replace two menu diagnostic rows with the hook fault flag and guard word,
  retaining all four route counts without overflowing the display.
- Preserve the actual Bootstrap2 executable alias `0xac010000` on relay
  resume. Actual Bootstrap2 calls that alias with caches disabled; the
  previous P1 alias was a contract mismatch, but is not claimed as the cause
  of this menu return.
- Keep executable bytes unchanged and retain CRC checks. No stored game
  files, accepted probe or optical capture/drive/command code are changed.
- The existing focused retail-package/layout module passed: 11 cases in
  0.096 seconds. Request one native CI build with its linked layout, stack
  and instruction audit. The workflow
  scope adds only the package constants and their existing tests so this
  correction does not repeat the broad host suite.

Next hardware check: replace the SD update, use the same boot CD and dump,
then Games -> DOA2 -> Y -> A. Record the furthest game screen or final
reader diagnostic. No repeated accepted probe, rerip or full-image verify.
