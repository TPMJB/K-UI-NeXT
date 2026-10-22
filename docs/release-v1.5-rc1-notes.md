# K-UI V1.5 "Dáinsleif" — RC1 release notes

Version `1.5.0-rc1` prepares the independent K-UI launcher for broader console
use. The package adds the Dáinsleif SD splash and presents Games launch as the
primary action after image inspection. It retains the working bounded SD
reader and the original K-UI badge on the optional boot CD. Start with the
[installation guide](release-v1.5-rc1.md).

## Games compatibility

The launcher now permits suitable **native Katana GD images** beyond DOA2.
Passing preparation checks means an image fits the current reader; it does
not establish that the title will play correctly.

| Requirement or limitation | RC1 behavior |
| --- | --- |
| Image format | GDI with raw 2352-byte track files and zero file offsets |
| Filesystem | Existing FAT32 or exFAT SD card |
| Boot program | Native GD executable in a contiguous root ISO9660 file extent, 128 bytes to 12 MiB; filename taken from IP metadata |
| Image map | At most 16 tracks and 128 physical file extents across the image |
| Windows CE / Mode 2 boot data | Unsupported |
| Audio tracks | The usual low-density track 2 is normal; high-density CDDA playback is unsupported and may cause missing music or a failed game request |
| Returning from a game | Power cycle the console |

Game reads are SD read-only. Normal game VMU save operations can still write
to the VMU. An incompatible layout or request may stop launch or gameplay;
broader title, streaming-audio and save/load compatibility remains unproven.
Ext4 is outside RC1; no filesystem conversion is part of this update.
GDEmu and IDE/CF support are outside this release. The tested optical capture
engine and bounded CMD18 retail SD reader remain unchanged.

## What the hardware evidence supports

Only **Dead or Alive 2** has confirmed gameplay. The owner called the pinned
CMD18 build substantially better and really playable, while still observing
initial fight and FMV slowdown. RC1 does not claim broad compatibility,
full-speed loading, or confirmed VMU save/load across titles. The next check
is one other owned native GD title through ordinary boot, gameplay and save/load.
In the confirmed DOA2 run, character selection to the first stage took about
32 seconds; slowdown lasted roughly eight seconds at the start of a fight and
ten seconds at the start of an FMV. These are owner observations, not measured
frame timings. RC1 itself has not yet passed physical-console acceptance.

The preserved rollback branch is `baseline/doa2-cmd18-ed31d522c847`, packaged
commit `ed31d522c8475b88bd40afa366fe3f7bdf143985`. Its archived SD ZIP is
8,134,517 bytes with SHA-256:

`a3c64bd3a6d70cc0369a32aaa04f563af9bea72ab8603e20307d3c5cb5b18bc4`

That branch and ZIP, and the older `baseline/doa2-sd-6c02bd8b22f4`, remain
unchanged. Restore the archived `KUI` files to roll back the SD runtime and
Games payload together; keep the existing boot CD and game dumps.

## Package and release status

The `release-candidate` download contains the normal runtime, Games payloads,
menu music, catalogues, optional boot CDI, splash preview and source/license
records. It excludes scan fixtures, demonstration Music files and SD
preferences. Merge its supplied files without replacing the existing SD
directory wholesale.

The original boot badge is checked byte for byte and read back from the
generated CDI during packaging. The new splash is embedded in the runtime;
`splash-preview.png` shows its artwork. Your current working CD remains usable.
This is **RC1 for review and console checks**, not a final release. Full source,
exact dependency pins and diagnostics remain available in the separate
`diagnostic` artifact from the same workflow run.
