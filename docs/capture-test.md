# First complete GDI capture test

This SD update implements M1.3 capture and M1.4 saved-file verification and
controlled resume. It has host-image tests; full capture on a physical Dreamcast
is still pending. Keep the reusable bootstrap disc that already loads SD.

## Install the update

1. Download **sd-update** from the successful Diagnostic build run. Keep a copy
   of your currently working `runtime.kui` on the PC.
2. With the console off, copy the download's `KUI/runtime.kui` to the card's
   `/KUI/runtime.kui`. Safely eject the card. Leave existing files in place.
3. Boot the **same existing CD**, without holding B. Confirm **SD runtime** and
   the new build ID. The capture page has **A New dump / X Resume / Y Verify**.
4. Replace the boot CD with a clean, known-good retail GD-ROM and close the lid.
   Start with an original well-kept game such as Evolution 2. Use the accessible
   exFAT card, with at least 1.5 GB free. No new CD burn is needed.

## Controls

| Control | Capture page | Diagnostics page |
| --- | --- | --- |
| A | New dump in a new directory | Disc sample probe |
| X | Resume newest job matching this disc | SD write/reread probe |
| Y | Verify newest completed job matching this disc | Save current log |
| B | Stop; finish/abort current call and checkpoint written data | Stop current probe |
| Left / Right | Switch pages while idle | Switch pages while idle |
| Up / Down | Scroll log | Scroll log |
| Start | Latest log lines | Latest log lines |
| Left trigger | mstats snapshot, also during capture | mstats snapshot |

The stick can also navigate. Input does not start another operation while one
is running. Wait for the operation to end and **READY** before removing the
card or powering off. A storage call may take time to return after Stop.

The RAM line updates once per second. Pull the left trigger at idle, during
track 3 and during verification to add detailed **mstats** snapshots to the log.
See [counter definitions and DreamShell comparison notes](memory-stats.md).

## One combined capture/resume test

1. Press A on the capture page. It reads both TOCs and content samples, checks
   available space, and creates `/KUI/dumps/d<identity-prefix>-0001/` (or the next
   unused number). It displays the title, profile and selected track ranges.
2. Once writing has progressed into track 3, press B. Wait for **STOPPED** and
   **READY**. Note the last committed byte count and job directory.
3. For the first resume check, power off normally. Reinsert the same bootstrap
   CD, boot the SD runtime, then swap back to the **same game disc**. Press X.
   It should select the same job, validate both checkpoints, and reread all
   committed bytes before continuing. This prefix check is expected to take time.
4. Let it finish. Capture is followed by a complete SD reread. The success line
   must say **SAVED DATA VERIFIED** and give the output `disc.gdi` path. A failure,
   Stop, or **Capture written** alone is not completion.
5. Switch to diagnostics with Left/Right and press Y. Save the log, then power
   off and retrieve the job's `manifest.json`, `disc.gdi`, and the diagnostic log.

Do not press A to resume: A deliberately creates a separate new job. X chooses
the newest directory for the current content fingerprint; it does not silently
fall back to an older job if that newest job's checkpoints are invalid. A new
complete rip can later provide the uninterrupted-versus-resumed comparison.

If the disc errors, the job stops after its retry budget and retains committed
data. The screen gives the track/FAD and drive sense information. After cleaning
or reinserting the same disc, X re-identifies it before writing. If an abort
cannot be confirmed or a buffer guard fails, reset the console first. There is
no zero-fill completion path and no claimed damaged-disc recovery rate.

## Check on the computer

The download includes a Python standard-library verifier. Point it at the
directory containing the completed tracks, `disc.gdi` and `manifest.json`:

```sh
python3 verify_dump.py /path/to/card/KUI/dumps/d0123456789abcdef-0001
```

It independently checks every track's length, CRC32, SHA-256 and GDI layout.
No reference comparison is claimed by default. To compare with an independently
verified reference manifest using the same track/gap convention:

```sh
python3 verify_dump.py /path/to/dump --reference /path/to/reference.json
```

A reference needs `profile: "gdi-raw2352-typegap150-v1"` and a `tracks` array
containing each required `file`, `bytes`, and `crc32` and/or `sha256`. A missing
track produces **PARTIAL MATCH ONLY** (exit 2); a mismatch produces exit 1.
All provided hashes must agree. Do not label an old dump as this profile until
its track sizes and gap convention have been checked. No catalog is bundled.

Send the build ID, disc title/region, manifest, GDI, diagnostic log and verifier
output. The full raw track files need not be uploaded. Note capture speed and
whether Stop, reboot, resume and final verification worked.

## Limits of this first capture update

- Physical full-track reading and throughput are untested. Each raw request is
  read twice with different initial fills and compared. This favors detecting
  underfilled/unstable buffers at a potential speed cost.
- Data sectors require a supported Mode 1/Mode 2 Form 1 layout and valid EDC.
  ECC correction, subchannels and audio-offset correction are not implemented.
- The [documented GDI profile](capture-format.md) excludes 150 sectors before
  an intra-session audio/data type change. TOCs do not measure arbitrary INDEX
  00 boundaries. A reference match still needs an independent compatible dump.
- Checkpoints support controlled Stop/Resume and validate saved data before
  appending. FAT/exFAT and card firmware do not guarantee arbitrary power-loss
  recovery. Use the controlled Stop test first.
- Resume is limited to the newest job for the current disc fingerprint.
  Existing completed jobs remain untouched when starting another dump.

Holding B at boot still uses your original CD fallback. Ordinary capture fixes
can be distributed as another `runtime.kui` using that same disc.
