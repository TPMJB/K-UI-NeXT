# Single-session CD capture and acquisition checks

The ripper offers the separate `cd-toc-index01-raw2352-v1` profile for a
**user-declared single-session** CD, CD-R or audio CD. It is an explicit opt-in:
open **Disc Ripper > Start > Single-session CD source** and press A. It defaults
off and resets when the launcher restarts. Enable it only when the inserted
media is independently known to have one session. With this toggle off, retail
GD-ROM capture retains its original two-area preparation and does not require
this choice. When enabled, the new media-aware
preparation still accepts GD-ROM as well as eligible CD media.

Native preparation checks that the firmware identifies a ready CD-type disc
and reads its low-area TOC. It does **not** query or prove the number of
sessions. KOS names BIOS GETSES command 35 but does not establish its parameter
and result ABI; the implementation does not guess that ABI from the different
hardware SPI REQ_SES packet. The manifest records the separate declared TOC
INDEX01 profile; the UI and log explicitly state that the session count is
unqueried. Selecting the profile declares the intended scope; it is not a firmware session-count measurement.

The planner does not reinterpret a CD TOC as two GD density areas. Multisession
CD-R, including typical two-session self-boot media, is outside this profile.
No full-disc or multisession compatibility is implied by a successful capture.

Each numbered track stores its complete half-open TOC interval
`[INDEX01, next INDEX01 or session leadout)` as raw 2352-byte sectors. Track
numbers begin at 1, controls are data or audio, intervals are contiguous and
bounded, and the planner never removes the GD profile's conventional 150-sector
type-change tail. The final track includes every sector up to leadout. The
accepted retail GD profile and its default track bytes remain unchanged.

CD output is **BIN/CUE only**. The native adapter forces this format for an
admitted CD, including when the saved New preference is GDI or a compressed
format. The portable API refuses a non-BIN/CUE New request before optical or
destination I/O. Resume and Verify retain the saved BIN/CUE format even when
the current output preference differs. The raw tracks, hidden `.capture.gdi`,
manifest and two checkpoints remain together for Resume and Verify. The visible
CUE declares Mode 1/2352, Mode 2/2352 or AUDIO and gives each separate file an
`INDEX 01 00:00:00`. It has no GD density markers or invented intertrack pregaps.
A first TOC INDEX01 after FAD 150 produces an initial virtual `PREGAP` so the
CUE preserves that actual address; these missing initial sectors are not read
or claimed as captured audio.

A TOC does not locate INDEX00, pre-emphasis flags, subchannels, hidden audio
before track 1, or sample offsets. The CUE and profile explicitly state that
INDEX00 and subchannels were not captured. They do not claim a forensic image
or exact preservation of unmeasured pregap indexes. In particular, a next
track's unlocated INDEX00 may change sector type before its TOC INDEX01. A data
window containing audio or mixed data modes fails the existing data framing,
EDC or consistent-mode check, preserving the partial job. Supporting that
layout needs additional measured index/subchannel information.

General CDs do not need a Dreamcast boot signature. The fallback optical title
is `Data CD`, `Audio CD` or `Mixed Mode CD`; a real Sega signature at the start
of a data track can supply its actual title. A custom folder label changes the
new folder only. Disc identity still hashes the distinct profile, every actual
TOC interval and three raw content samples per track. It is independent of the
label and does not alias the GD profile. Audio bytes retain the drive's original
order, without byte swapping, jitter correction or an inferred offset.

## Retry budget

**Start > Capture settings > Read retries** selects 1, 5, 10, 20 or 50
additional attempts after an unsuccessful request. The underlying
`kui_capture_options.retry_limit` admits those same values. Zero preserves the historical default of ten.
A failed batch shrinks to one sector at the same FAD; shrinking does not reset
the budget. The initial failed request plus the chosen additional attempts
bounds the synchronous retry loop. Fatal media changes and Stop still exit
immediately. An overlapped DMA failure can first fall back to that synchronous
path, as before. No failed read is committed or zero-filled.

The retry choice applies to new optical reads during this run, including
identity sampling. A resumed job retains its own output/hash mode and recorded
cumulative retry count. Its saved prefix is still checked according to the
existing Resume policy. Invalid retry choices fail before I/O.

## Strict acquisition

**Start > Capture settings > Strict sector checks** defaults off, preserving
normal EDC-checked capture. Enabling it sets
`kui_capture_options.strict_sectors` and adds the project's existing Mode 1
validator to every newly acquired data sector on both synchronous and overlapped paths:

- exact sync and expected absolute FAD address;
- Mode 1 EDC and reserved zeros;
- both P and Q Reed–Solomon parity sets.

A failed check follows the bounded retry policy and does not commit the bad
sector. Strict Mode 2 is explicitly refused; it does not silently claim that a
Mode 1 parity check validates a different code layout. Ordinary Mode 2 Form 1
capture remains available when strict is off. Audio has no equivalent EDC/P/Q
check and follows the existing read policy with either choice.

Strict acquisition detects invalid sectors, without repairing or changing their
bytes. It does not retroactively apply parity checks to a previously saved
prefix on Resume, and it does not establish audio reference equality. Advanced
CRC and full saved-file Verify remain separate checks. Reference comparison is
still reported separately from saved-file consistency.

## Validation

`test_capture_core.c` checks exact CD geometry, invalid-plan rejection without
changing the output plan, audio/single/mixed layouts, CUE round trips through the
independent game-image parser and all admitted retry limits.
`test_capture_cd_images.py` drives the actual capture/checkpoint/FatFs code on
FAT32 and exFAT using independently generated Mode 1 sectors. It covers all CD
layouts, offset CUEs, full raw-byte equality, interrupted/resumed equality,
damaged newest checkpoints, wrong-disc exact selection, read-only Verify,
bounded retries, early no-write refusals, valid-EDC corrupt parity/reserved bytes,
valid-parity wrong addresses and explicit strict Mode 2 refusal. These host
checks do not substitute for console acceptance of the integrated runtime or
for independent confirmation that the source disc has only one session.
