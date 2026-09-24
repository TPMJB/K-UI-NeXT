# Menu sounds

Navigation and confirmation use two original synthesized tones. Settings can
mute them independently of music; the existing volume setting scales playback.
The tones last about 28 ms and 80 ms, with an attack/release envelope and a lower
maximum amplitude than full-scale PCM. They use one reserved AICA channel and
4,736 bytes of sound RAM. Playing a tone does not access SD, allocate memory or
replace a music stream. A second effect reuses the same channel.

`kui_menu_sound_init()` runs once after startup audio and before the interactive
shell, loads the samples, and releases its temporary main-RAM synthesis buffer.
Partial initialization failure frees the channel and any loaded effect. The UI
serializes configuration/play calls; shutdown releases only this module's
channel and effects before global audio or a BIOS handoff. It does not shut down
the music service. A failed initialization leaves silent controls functional.

Host tests exercise 10,000 repeated plays without another allocation, mute,
zero/clamped volume, invalid effect IDs, initialization failures, cleanup and
reinitialization. The simulated music channels are excluded from effect use.
Actual volume and coexistence with SD music remain a console acceptance check.

The API and allocation behavior were checked against upstream KallistiOS
`fcfa7d869471591ca1c777543261a7bfea7cb726`:

- `kernel/arch/dreamcast/include/dc/sound/sfxmgr.h`
- `kernel/arch/dreamcast/sound/snd_sfxmgr.c`
- `kernel/arch/dreamcast/sound/snd_stream.c`
- `kernel/arch/dreamcast/sound/snd_iface.c`

The sound module generates its own samples and imports no DreamShell code or
recorded assets.
