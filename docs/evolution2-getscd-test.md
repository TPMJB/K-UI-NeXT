# Evolution 2 — GETSCD compatibility test

The RC1 console photo (build `8409223c28ba`) stopped on command `0x22`
(GETSCD). The previous reader rejected that command before SD access. Its
displayed LBA/count/destination were left over from an earlier read.

This test adds bounded GETSCD `{format, capacity, destination}` responses:
raw index-1 Q (format 0), formatted Q position (1), and unavailable media
catalog (2). Position follows the last completed read/seek and the GDI track
map. Raw Q includes its complemented CRC; formatted Q uses binary sector
counts. Short buffers receive only a prefix, and completion reports the bytes
actually written. Format 3 remains unsupported. No SD read or write is needed.
These are synthesized normal-disc responses, not captured subchannels,
pregap/index reconstruction, CDDA playback or proof of game compatibility.

The independent response encoder uses the interfaces documented by
[KOS syscalls.h](https://kos-docs.dreamcast.wiki/syscalls_8h_source.html) and
cross-checked against Flycast's
[GD-ROM response formats](https://github.com/flyinghead/flycast/blob/master/core/hw/gdrom/gdromv3.cpp)
and [BIOS command handler](https://github.com/flyinghead/flycast/blob/master/core/reios/gdrom_hle.cpp).
No emulator implementation is copied.

Obsolete startup route counters were removed from BIOS-menu-return diagnostics
to make room in the existing resident region. The fault screen retains command,
destination, SD result and last route details; GETSCD labels its format/capacity
correctly. Resident addresses, guarded stack bounds, CMD18 reads and the
accepted optical capture code remain unchanged.

## Console result — 2026-09-24

After installing the GETSCD update, the owner reported that Evolution 2
worked, but was "horribly slow." This confirms that the reported command
rejection no longer prevents the tested image from booting. Full gameplay,
game completion and VMU save/load were not established by that report.
The successful build is recorded in
[workflow run 36057635443](https://github.com/TPMJB/K-UI-NeXT/actions/runs/36057635443),
from source commit `420569e541014a7ed0be619967f6321886914353`.

This fix is included in the final [1.5 release](release-v1.5-notes.md).
Further game compatibility testing continues independently; speed
optimization is deferred beyond 1.5.

## Original console check

Merge the new package's `KUI` folder onto the card, replacing matching supplied
files while preserving games and settings. Keep the current boot CD. Launch
the same Evolution 2 GDI, try to reach the title screen and start gameplay.
Report how far it gets; photograph a new stop screen if one appears. No
benchmark, rerip, reformat or broader title sweep is needed. The earlier RC1
package remains available for rollback.
