# Music cache, custom cycling, and Ogg playback

The round-four hardware logs show retained custom-song bytes and stable cache
accounting. Harbor Lights was unreachable from the triggers because that code
cycled five bundled entries, while the browser song lived in a sixth slot. The
new cycle includes that custom slot whenever it is cached. Cache retention is
intentional; Stop/mute leaves songs ready for RAM-only selection.

**Music → X → Clear cache** stops playback, drains its stream and releases the
cached song allocations. The I/O worker performs it when a preload cannot be in
flight. Home/Ripper triggers and ordinary playback never access SD. Selecting a
file in the browser still replaces the single custom entry, preserving the old
song if loading fails or is cancelled.

The Music browser accepts PCM16 WAV and Ogg Vorbis. Both support mono/stereo,
8–44.1 kHz and files up to 6 MiB. Vorbis clips must contain at least 1024 frames;
chained/multiplexed streams are rejected. The selected Ogg remains compressed in
RAM and is decoded in the existing audio service, using the same double output
buffers as WAV. No whole-song PCM copy is allocated.

The **8 MiB cache budget includes staging, compressed bytes, alignment padding
and one 384 KiB decoder arena shared by every cached Ogg**. Only the selected Ogg
is decoded; selecting another rebuilds its decoder in that arena from RAM and
restarts it. An Ogg load temporarily needs a second, staging arena to validate
the new file while the previous song continues. All codec allocations,
including its temporary frame memory, are confined to those arenas. A valid but
unusually complex Vorbis setup that exceeds the arena is rejected, retaining the
old song. Fixed callback buffers, the audio thread stack and KOS stream state are
separate from that song-cache total. The system allocator may retain freed heap
pages; `heap in use` and music cache counters distinguish that from a live leak.

The independently licensed decoder and exact revision are recorded in
`third_party/stb/README.md`. Page checksums, sequence/serial continuity and final
page presence are checked before selecting a song. Playback errors stop the
stream; they do not cause reads from SD. Host tests cover exact looping,
mono/stereo output, malformed/truncated input, allocation accounting, failed and
cancelled replacement, custom trigger selection and full cache release.

Hardware acceptance remains open for Ogg. First listen to Harbor Lights through
one full loop, visit menus, switch to a bundled track and back, then clear cache.
Only then try it during an ordinary rip. This is an audio compatibility check;
no accepted disc-reader benchmark needs repeating. Ogg saves cache memory at the
cost of ongoing CPU decoding, and its effect on capture speed is not yet measured.
The owner has since tested an Ogg on the console and reports the decoding cost
as minuscule; no timing was logged.

## Bundled menu songs as Ogg

After 1.5 the five menu songs ship as Ogg Vorbis (`KUI/apps/music/*.ogg`). With
all five cached, music holds **913,765 bytes (0.87 MiB)** instead of the WAVs'
4,674,286 bytes (4.46 MiB): 520,534 compressed bytes plus the one shared
decoder arena. Startup also reads about 0.5 MiB from SD instead of 4.5 MiB.
Each Ogg was encoded from the reproducible WAV and decodes to its exact length;
see the [music record](../resources/music/README.md). A runtime whose card has
only the 1.5 WAVs plays those, so updating `runtime.kui` alone keeps menu music.

Console check, with the new `KUI/apps/music` and runtime and music enabled:
confirm a song starts after the splash, then step through all five with Home
L/R; each should restart from its beginning without clicks at its loop point.
After the playlist fills, the Music page should read `Cached 0.8 MiB / 8 MiB`
(the WAVs showed 4.4 MiB), and a log saved with Diagnostics Y should contain
`cached=913765 (decoder 393231)`. Finally, test once with only the old WAVs in
that folder.

## Audio CDs

Music → **L: Audio CD** opens an audio-track list. **A** plays the selected track,
**Y** pauses/resumes, **X** stops, **R** refreshes, **B** returns to the SD browser,
and **Start** returns Home. The drive plays the audio directly; SD is not used.
Ordinary audio CDs and audio tracks on enhanced/mixed CDs are accepted. The
list omits every TOC entry with the data bit set; the original audio track
numbers are retained, even when there are gaps. A requested track must appear
in that audio-only list and the complete TOC must still match before PLAY.
Data-only discs, data-track selections and GD-ROMs are refused.

The September 24 hardware log explains the first rejected album: its TOC has
16 audio tracks and a seventeenth data track, and firmware identifies it as
CD-ROM XA. The initial player deliberately accepted only pure audio CDs. The
second album has 13 audio tracks and successfully played tracks 1, 2 and 3.
These observations establish playback on that second disc; they do not yet
prove the new enhanced-CD filtering on hardware.

The same log records two `Drive not ready` failures between selected tracks,
followed by BUSY-to-STANDBY transitions. The adapter previously checked once
immediately after STOP and INIT. It now waits up to two seconds for each
readiness transition, with cancellation, before submitting the next command.
Commands are not retried. Host tests reproduce delayed readiness after STOP
and INIT, a permanently busy drive and cancellation before INIT. Idle playback
polling uses one status snapshot and keeps a transient BUSY/SEEKING observation
from replacing the playing message. The log does not expose the exact failed
UI action, so the hardware cause remains a supported inference to confirm.

The adapter runs exclusively on the existing optical worker. It uses documented
firmware commands with the existing portable deadline/abort loop, static command
parameters and a guarded static TOC. A selected track is rechecked against the
listed TOC before playback. The shell stops CD audio before another optical
operation and refuses that handoff if Stop fails. A failed command abort leaves
its firmware buffers untouched and requires a console reset. No accepted GD-ROM
reader file was changed.

`spu_cdda_volume` and `spu_cdda_pan` route the CD's audio through AICA; `snd_init`
is idempotent. This does not reset sound RAM or reinitialize the global mixer
while a song/effect is active. The shell selects one source at a time: starting
CD playback pauses cached SD music; selecting an SD song stops CD playback.

The implementation was checked against upstream KOS at
`fcfa7d869471591ca1c777543261a7bfea7cb726`: `dc/syscalls.h`, the documented
`dc/spu.h` mixer API, `hardware/cdrom.c` command parameters and the original
`examples/dreamcast/sound/cdda/basic_cdda` example. No standard blocking CD-ROM
wrapper is called. Command deadlines bound the polling loop; they cannot preempt
a firmware syscall that itself stops returning.

Host firmware tests cover pure/enhanced media, data-only rejection, hidden data
tracks, a changed TOC, bounded busy transitions, nonzero successful
status returns, selected-track commands, pause/resume, track completion, held-B
Stop, stop failure, guarded buffers and a failed abort retaining static firmware
storage. Physical audio playback was reported with the second album above.
The focused remaining check is the previously rejected enhanced CD: confirm
that only tracks 1–16 appear, play one, pause/resume, then change tracks without
needing Refresh. Stop and return to the SD song. No rip or new boot disc is
needed for this test.
