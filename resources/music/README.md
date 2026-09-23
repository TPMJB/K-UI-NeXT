# Original K-UI menu music

These are the five original K-UI compositions made for TPMJB with Codex
assistance. They use synthesized notes/percussion and no external recordings,
samples or sound banks. The maintainer authorized their reuse in independent
K-UI. The original [music permissions and composition record](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/applications/launch_app/music/README.md)
allows use, modification and redistribution of the musical material/recordings
without additional restrictions or attribution requirements to the extent
rights exist in that generated material.

`original_generator.py` is an exact copy of the independently authored
[`utils/generate_menu_music.py`](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/utils/generate_menu_music.py)
at revision `2a5309298dde8fb100da1e2e4e10517695c9780f`. The first soundtrack was
introduced in [`afa3767a`](https://github.com/TPMJB/K-UI_DS/commit/afa3767ac5a6c8f9e79cf61ca2ba894ee3d6fb23),
and the playlist expanded in [`7d5b4477`](https://github.com/TPMJB/K-UI_DS/commit/7d5b4477a9d4f8ea406c6ec58d590f6eaafa770a).
The selected generator code is reused under this project's GPL-3.0-only terms
at the maintainer's direction. No DreamShell player, module or binary is copied.

[manifest.json](manifest.json) records the exact generator Git blob/SHA-256 and
each generated WAV's SHA-256, format, length and frame count. The legacy repository
generates these WAVs at build time rather than storing WAV binaries.

| File | Title | Duration |
| --- | --- | --- |
| `menu.wav` | After Hours | 24.0 seconds |
| `neon-circuit.wav` | Neon Circuit | 19.2 seconds |
| `orbital-drift.wav` | Orbital Drift | 24.0 seconds |
| `midnight-vector.wav` | Midnight Vector | 17.45 seconds |
| `chrome-horizon.wav` | Chrome Horizon | 21.33 seconds |

Reproduce the recordings with Python's standard library:

```sh
python3 tools/generate_menu_music.py
```

Generated WAVs are ignored by Git. Packaging writes directly into its staging
directory with `python3 tools/generate_menu_music.py --directory STAGING/KUI/apps/music`.
The asset test regenerates all five in a temporary directory and compares them
with the committed manifest; no pre-existing WAVs are needed.

The five files total 4,674,286 bytes. They are mono PCM16 at 22,050 Hz and retain
the original arrangements. The largest supplied file is 1,058,444 bytes.
Package the WAVs into `/KUI/apps/music/` on SD.
They are not embedded in the runtime binary. The separate manifest/generator
need not be copied onto the card.

## Independent player

The shared background player accepts RIFF WAV files with mono/stereo PCM16 at
8–44.1 kHz. Each bundled menu file is limited to 2 MiB; a custom Music Player
selection is limited to **6 MiB for the whole file**. The combined cache budget
is **8 MiB**, including a temporary replacement allocation. Inactive tracks can
be evicted to stay inside that budget; the current song remains available until
its replacement has loaded and validated. A missing, invalid, oversized or
cancelled replacement keeps the previous song and reports the reason.

Only the existing I/O worker loads files, in cancellable 32-KiB reads. It
releases SD before selecting the new cache. Idle work attempts to preload all
five bundled songs, subject to the same budget. A missing or invalid file does
not cause repeated idle-loop reads.
Audio initialization, allocation and playback failures also latch until an
explicit Music setting or track change, avoiding repeated idle-loop retries.

An independent audio service thread feeds RAM-only callbacks; neither it nor
the callbacks accesses the card. Bundled songs and custom selections loop.
Off retains the cache. Navigation, preference saves, tests and capture no longer
unconditionally pause music. Selecting a song in Music Player returns control
to the shell after preload, so B/Start can leave the browser without stopping
playback. Music Player Y stops music; Ripper Y remains Verify.

The five cached menu files retain **4,674,286 bytes (4.46 MiB)**. A custom song
adds its own retained file until replaced or evicted; both are separate from
the runtime image, stacks and audio buffers. A rise while the playlist fills
can therefore be expected. It is not enough by itself to establish a leak.
Selecting one of the five bundled files manually now shares its existing menu
cache slot, including an ASCII case variation of its path, instead of creating
a second custom copy of the same file.

The music RAM report separates ready cache, in-flight loading bytes, peak
combined file allocations and cumulative allocation/free counts. Stop/mute
retain cached tracks for instant selection. The peak is historical and will
not fall when a replacement releases memory. Main RAM accounting already
counts heap free space as available; the heap's reserved arena size alone is
not a live-use measurement.

Home and Ripper L/R select the previous/next bundled song. Cached selections can
change during capture without touching SD. An uncached choice displays a queued
change and loads when the storage worker becomes idle; capture retains storage
ownership. A custom song is chosen through Music Player rather than the bundled
five-song trigger cycle.

The two callback buffers are **128 KiB each**, KOS uses a further **64-KiB
separation buffer**, and the audio thread has a **32-KiB stack**. These are
additional main-RAM costs beyond the file-cache budget. Audio RAM is separate.
Startup-cue handoff and exiting to the BIOS still drain and release the audio
streamer. Playback errors are reported rather than silently claiming continuity.

`Music/harbor-lights.wav` in the update package is a separate original 60-second
mono PCM16 test piece, 2,646,044 bytes, generated by
`tools/generate_music_demo.py`. It exercises the larger custom-song path.
The companion demo guide and manifest record its format and hash. Ogg/Vorbis
playback is not enabled; an optional generated Ogg is for PC-side comparison.

The backend follows the pinned upstream KallistiOS
[stream implementation](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/sound/snd_stream.c)
and [public API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/sound/stream.h).
In that revision callback lengths are bytes despite the header's sample wording;
stream destruction waits for outstanding DMA. Host tests cover these contracts,
buffer lifetime, cache bounds, malformed files, volume and cancellation.
The [cache audit](../../docs/evidence/m15-music-cache-host-2026-09-23.json)
additionally checks actual wrapped allocation/free calls through 1,000 cached
switches, 300 custom replacements plus 300 failed replacements, and sixteen
swaps reaching the exact 8 MiB staging limit. All file allocations are released
on shutdown in both host paths. These tests do not prove the absence of a leak
elsewhere in the console's audio stack; a report from the owner's ongoing run
is still needed to compare its live cache and heap values.
The audit also reproduced a separate use-after-free race: a Resume control
could run between releasing the previous custom song and selecting its
replacement. Custom replacement now publishes the new file and playable loop
under one audio lock. A deterministic control call at every mutex-release
boundary reproduced the failure under ASan before this fix and passes afterward
through sixteen custom replacements. That finding does not explain a rise in
retained cache size by itself.
Actual audio continuity during navigation/capture and controller responsiveness
still need the focused console checks in
[apps-round-three.md](../../docs/apps-round-three.md). Available RAM alone does
not establish zero CPU or throughput cost. No new boot disc is required.
