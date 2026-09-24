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

The exact executable is not present in the available workspace. At its load
base `0x8c010000`, caller PR maps to file offset `0x2450`. For a normal SH call,
the call instruction is at `0x244c` followed by its delay slot at `0x244e`.
The actual file is needed to identify the caller and trace its failure branch.

`tools/kui-startup-bundle.py` extracts the owner's existing raw-GDI IP.BIN and
1ST_READ.BIN into a small ZIP, with version/region and checksum metadata. It
requires only Python 3, reads source files with `rb`, refuses output overwrite,
and does not execute the game code. A single existing synthetic GDI fixture
confirmed exact extracted IP/executable bytes and unchanged source hashes.
No proprietary game bytes are committed or bundled with this helper.

This update changes only the extraction helper and this evidence record. No
new console build or broad test suite is requested. Next input needed from
the owner: the generated `DOA2-startup.zip` from the same tested GDI.
