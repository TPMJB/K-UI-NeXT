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
| Network inspection | `src/apps/network_test.c`, `network_status.c` | none |
| Music | `src/apps/music.c`, `wav.c` | five original loops in `/KUI/apps/music/` |

A single worker owns app hardware and SD operations. UI input queues work; it
does not mount storage. Music preloads one bounded WAV file, releases the card,
and plays from RAM. Before any worker operation its stream is stopped and
outstanding audio DMA is drained. Idle playback resumes from cached data.

System settings contain video timing, memory display and music options. Ripper
hash/readback settings live under Ripper > Advanced > Capture settings. Existing
`bench.cfg` overrides still apply to captures. The separate system record does
not alter them. New settings use alternating CRC-protected records with exact
readback; only successful saves become active UI preferences.

The canvas remains 640x480. Video choices select default cable timing, NTSC60 or
PAL50 on TV; VGA always uses its supported progressive timing. This is the first
system video control, not arbitrary resolution scaling. Changes preview for ten
seconds and roll back without confirmation. Holding Y when the SD runtime starts
bypasses a saved video choice without rewriting it; preview cancellation also
restores that safe override.

## Separate executable apps: next architecture step

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
