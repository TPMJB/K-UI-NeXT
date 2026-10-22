# K-UI V1.5 "Dáinsleif" — release notes

Version **1.5.0** promotes the tested independent K-UI launcher to a full
release. It adds broader native-GD game launching and the crimson Dáinsleif
startup splash while preserving the working boot CD and optical capture
engine. Follow the [installation guide](release-v1.5.md) to update an existing
FAT32/exFAT SD card.

## Changes in 1.5

- Games launches suitable native GD images without the former DOA2 title or
  `1ST_READ.BIN` filename allowlists. The boot filename comes from IP metadata;
  image, memory and allocation checks still apply.
- Game inspection leads directly to launch confirmation. Read probes remain
  available under Advanced diagnostics.
- GETSCD command `0x22` now provides bounded raw Q position, formatted Q
  position and unavailable media-catalog responses. This fixes the request
  rejection that prevented the tested Evolution 2 image from booting.
- The SD runtime includes the new Dáinsleif splash. The original K-UI badge
  beneath the SEGA logo and startup sound remain unchanged.
- The release bundles the nine-app shell, menu music, reference catalogues,
  normal Games payloads and an optional boot CDI. Existing game dumps and
  settings are preserved when the supplied files are merged onto the card.

## Games compatibility

Passing preparation checks means an image fits the current reader; it does
not establish that the title will play correctly.

| Requirement or limitation | 1.5 behavior |
| --- | --- |
| Image format | GDI with raw 2352-byte track files and zero file offsets |
| Filesystem | Existing FAT32 or exFAT SD card |
| Boot program | Native GD executable in a contiguous root ISO9660 file extent, 128 bytes to 12 MiB; filename taken from IP metadata |
| Image map | At most 16 tracks and 128 physical file extents across the image |
| Windows CE / Mode 2 boot data | Unsupported |
| Audio tracks | The usual low-density track 2 is normal; high-density CDDA playback is unsupported and may cause missing music or a failed game request |
| Subchannel responses | Synthesized index-1 Q position; no captured subchannels, pregap reconstruction or format-3 GETSCD response |
| Returning from a game | Power cycle the console |

Game reads are SD read-only. Normal game VMU save operations can still write
to the VMU. An incompatible layout or request may stop launch or gameplay;
broader title, streaming-audio and save/load compatibility remains unproven.
Ext4, GDEmu and IDE/CF support are outside this release. The tested optical
capture engine and bounded CMD18 retail SD reader remain unchanged.

## Hardware evidence and known performance limits

| Area | Confirmed observation |
| --- | --- |
| 1.5 runtime | Owner reports successful boot of the release-candidate runtime |
| Dead or Alive 2 | Playable on the pinned CMD18 baseline; about 32 seconds from character selection to the first stage, with initial fight and FMV slowdown |
| Evolution 2 | Owner reports successful boot after the GETSCD fix, but describes performance as horribly slow |

These are owner observations, not measured frame timings. Evolution 2's
successful boot does not establish full-game completion or VMU save/load
compatibility. Broader game testing continues separately. Serial SD transport
remains a performance constraint, and speed optimization is deferred beyond
1.5. The final release does not claim universal game compatibility or
full-speed loading. Other app paths retain their documented acceptance limits;
see [hardware evidence](https://github.com/TPMJB/K-UI-NeXT/blob/v1.5.0/docs/hardware-evidence.md) and the
[app acceptance guide](https://github.com/TPMJB/K-UI-NeXT/blob/v1.5.0/docs/apps-round-five.md).

The preserved rollback branch is `baseline/doa2-cmd18-ed31d522c847`, packaged
commit `ed31d522c8475b88bd40afa366fe3f7bdf143985`. Its archived SD ZIP is
8,134,517 bytes with SHA-256:

`a3c64bd3a6d70cc0369a32aaa04f563af9bea72ab8603e20307d3c5cb5b18bc4`

That branch and ZIP, and the older `baseline/doa2-sd-6c02bd8b22f4`, remain
unchanged. Restore the archived `KUI` files to roll back the SD runtime and
Games payload together; keep the existing boot CD and game dumps.

## Package and source

`kui-1.5.0-dainsleif-release.zip` contains the normal runtime, Games payloads,
menu music, catalogues, optional boot CDI, splash preview and source/license
records. It excludes scan fixtures, demonstration Music files and SD
preferences. Merge its supplied files without replacing the existing SD
directory wholesale.

The original boot badge is checked byte for byte and read back from the
generated CDI during packaging. The new splash is embedded in the runtime;
`splash-preview.png` shows its artwork. Your current working CD remains usable.
Full corresponding source, dependency pins and license records accompany the
release in `kui-1.5.0-dainsleif-source.zip`, as identified by `SOURCE.txt`. Historical RC1 documentation remains in
the repository for reference.
