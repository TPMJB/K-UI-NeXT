<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Quake III Arena startup sample

The [expanded runtime audit](quake3-runtime-audit-2026-10-15.md) resolves the
missing-file request below and records the executable handoff and allocator
obstacles found in those files.

Read-only analysis of the owner-supplied `Quake-III-startup.zip`, generated
by startup helper version 2 from BIN/CUE. This records metadata and authored
observations only. Extracted retail files remain outside the repository.
No loader, runtime profile, compatibility label or working Worms module changed.

## Identity and validation

- ZIP SHA256: `320871d3fc50c6df79d18af8c046048dbac0f758dbfaec118e7c86e623427cea`.
- All archive CRCs pass; all five payload sizes, CRC32s and SHA256s match the report.
- IP identifies US `MK-51061`, version `V0.800`, native boot `1ST_READ.BIN`.
- Boot: 98,692 bytes, CRC32 `1d36993f`, SHA256
  `5c7506857fdea9062ae322dd20bb4691ca5bcee1e066e90029666eb5c994f088`.
- Layout has 20 tracks, with 16 high-density AUDIO tracks (4–19). Low-density
  AUDIO track 2 alone would not establish gameplay CDDA usage.

The CUE stores 150 sectors before INDEX 01 in each AUDIO file, and 225 sectors
before INDEX 01 in final DATA track 20. These correspond to file offsets
352,800 and 529,200 bytes respectively. The extractor reports those offsets;
an adapter cannot treat these source files as zero-offset program payloads.
Successful extraction does not establish CUE playback support in K-UI.

## Boot and missing runtime inputs

Boot strings reference GINSU configuration and transfer executable paths.
This small GINSU launcher is insufficient to inspect the game's sound hooks.
The following entries exist in the ISO inventory but were not selected by
the default helper rules:

| ISO path | Bytes | Purpose for next analysis |
| --- | ---: | --- |
| `QUAKE3/Q3_USA.BIN` | 1,316,164 | Main game executable candidate |
| `QUAKE3/INTRO.BIN` | 354,724 | Intro executable candidate |
| `GINSU.INI` | 258 | Resolve configured launch chain |
| `GINXFER.BIN` | 8,436 | GINSU transfer executable |
| `GINRESET.BIN` | 153,024 | Reset and executable lifecycle inspection |
| `QUAKE3/BASEQ3/SYSTEM/SYSTEM.ZIP` | 73,728 | Small system container; contents unverified |

The existing repeated `--include ISO/PATH` option can collect these without
conversion or a new rip. Default selection intentionally excludes arbitrary
`.BIN` assets. An empty `omitted_files` list means no *selected candidate*
was dropped by the budget; it does not prove all native code was collected.

## Included sound driver

`QUAKE3/BASEQ3/SYSTEM/AUDIO64.DRV` and `QUAKE3/INTRO/AUDIO64.DRV` are
byte-identical: 19,180 bytes, CRC32 `033c658a`, SHA256
`5f99adacb1af9c0648d9b64fdf48a4a6dc17d566bbafa105577da48ce891cb51`.
The embedded driver version is `1.90h.2`.

Header words retain Audio64 queue `0xb200` (header +`0xd0`), notification
`0xb400` (+`0xc8`) and entry `0x100` (+`0xdc`). Other published pointers
differ: +`0xe0` is `0xbac`, +`0xe4` is `0xbec`, and +`0xe8` is `0xc34`.
Toy Commander and MDK2 profiles expect different state locations. The
existing complete driver hashes and live-driver checks therefore cannot
admit this sample as either existing profile.

Focused ARM inspection finds the familiar 32-by-16-byte queue and inspected
Open/Close/Play/Stop/MultiPlay/MultiStop packet fields. State locations include
previous cursors at `0xd34`, hardware-base word at `0xba8`, default template
at `0x1234`, and 72-byte per-port templates at `0x127c`. Open calls Close
before preparing its template, unlike Toy Commander's Open. An inline flag
at `0x30e0` can suppress normal packet clearing; existing acknowledgement
logic needs review against the host initialization and use of this flag.

These similarities support an Audio64 family adapter investigation. Full
command ABI, cursor semantics and game allocation compatibility remain open.
Inspect the main and intro executables before selecting hooks, sound RAM,
voices or service callbacks. No Quake III hardware validation is claimed.
