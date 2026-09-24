# Apps and the SD runtime

`/KUI/runtime.kui` remains the entry point understood by the existing bootstrap
CD. It currently contains the launcher and statically linked app code. App data
and music are separate SD files; this is not yet an executable plugin loader.
No new boot disc is needed for this round.

| Component | Source boundary | SD data |
| --- | --- | --- |
| Launcher and display | `src/core/shell.c`, `src/dreamcast/shell_draw.c` | embedded original UI assets |
| System preferences | `src/apps/system_settings*.c` | `/KUI/apps/system/settings-a.bin`, `settings-b.bin` |
| Disc identity | `src/apps/disc_identity.c` | none; insertion title is separate from previous job results |
| Disc Ripper | existing capture adapter and frozen reader | chosen destination, default `/Games`; existing ripper preference records |
| Memory Test | `src/apps/memory_test.c`, `memory_pattern.c` | none |
| VMU Manager | `src/apps/vmu.c` | new verified backup folders in `/KUI/backups/vmu/` |
| Network inspection/connection test | `src/apps/network_test.c`, `network_status.c`, `network_probe.c`, `network_connect.c` | none; temporary session |
| Music | `src/apps/music.c`, `wav.c`, `music_ogg.c`, `cd_audio.c` | original loops in `/KUI/apps/music/`, selected WAV/Ogg files |
| Advanced CRC / Salvage | `src/core/recovery_scan.c`, `salvage.c` | read-only existing dumps; isolated `/KUI/salvage/` jobs |
| System backups | `src/apps/maintenance*.c` | `/KUI/backups/system/`; no console flash writes |
| Menu feedback | `src/apps/menu_sound.c` | synthesized samples held in sound RAM |
| Games foundation | `src/apps/games.c`, `src/core/game_image.c`, `game_metadata.c` | read-only `/Games` or selected SD GDI; metadata inspection only |

A single worker owns app hardware and SD operations. UI input queues work; it
does not mount storage. Music preloads bounded WAV/Ogg files, releases the card,
and plays from RAM through a separate audio service thread. Menu actions and
capture do not stop playback. Cached song switches need no card access; an
uncached request waits for the I/O worker to become idle. Stream teardown drains
audio DMA before cached bytes are replaced or playback exits. See
[the current acceptance round](apps-round-five.md).

System settings contain video timing, memory display and music options. Ripper
hash/readback settings live under Ripper > Advanced > Capture settings. Existing
`bench.cfg` overrides still apply to captures. The separate system record does
not alter them. New settings use alternating CRC-protected records with exact
readback; only successful saves become active UI preferences.

Audio-CD commands share the worker and must stop before an optical or SD-song
handoff. Playback can continue while browsing menus; idle disc identification is
excluded while the CD player owns the drive. Failed stop refuses the handoff;
explicit System Tools Restart remains available after a failed abort.

The canvas remains 640x480. Video choices select default cable timing, NTSC60 or
PAL50 on TV; VGA always uses its supported progressive timing. This is the first
system video control, not arbitrary resolution scaling. A horizontal 0/16/32-pixel safe-area inset
compresses the existing canvas without changing the video timing. Changes preview for ten
seconds and roll back without confirmation. Holding Y when the SD runtime starts
bypasses a saved video choice without rewriting it; preview cancellation also
restores that safe override.

## Separate executable apps: next architecture step

The [Games foundation](games-test.md) remains statically linked for GDI browsing
and inspection. Its planned resident loader has a separate lifetime and memory
contract; see [the staged plan](games-milestone-plan.md). No game launch or
executable package interface is implemented by the browsing update.

Independent app executables should come after these app lifecycles are proven.
They need a versioned launch/return contract, bounded image validation and memory
layout, controller/display ownership, failure recovery, and explicit teardown of
threads, audio DMA, network and storage. Merely moving binaries into folders
would not implement those requirements. The module boundaries above make that
work possible without rewriting the finished reader.

A later `/KUI/apps/<id>/` package can contain a manifest, icon, resources and
validated executable. The shell must remain usable if one app package is
missing/corrupt, and the present runtime/CD recovery path must remain available.
No such executable package format is claimed to exist in this build.

## Later boot disc

After app acceptance: rebuild the launch CD with the original K-UI appearance,
SD app loading and a visible benchmark/diagnostics choice, retaining the B-key
fallback. The existing disc remains the supported launcher during app work.
Do not spend another blank disc on this round.
