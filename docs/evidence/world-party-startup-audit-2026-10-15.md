<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Worms World Party startup audit

Owner-supplied `Worms-World-Party-startup.zip`, 5,523,775 bytes, SHA256
`02fd9e9b88b838258917814b41e88830830b44e738585f2a652355cfc9289b72`.
All 91 ZIP members pass CRC validation; every declared payload size, CRC32
and SHA256 matches. Extracted retail inputs remain private outside git.

The helper v2 report identifies US `T22904N`, `V1.001`, Windows CE,
`0WINCEOS.BIN`, and GDI input referencing raw 2352-byte track BINs with
zero file offsets. There are 18 tracks, with 14 high-density AUDIO tracks
(4–17) and final DATA track 18. The bundle's input descriptor is GDI, so
this sample does not establish the launcher's CUE CDDA capability.

## Completeness

No additional files are required for initial static adapter analysis.
The bundle has `GAME.EXE`, the kernel, and the required external sound
dependencies. Its 17-EXE ambiguity includes one root game candidate and
16 WINCE utilities; it is not a missing-file error. Exact active autorun
selection remains unproven: SYSSTART reads the `init` / `Autorun` registry
configuration, which has not been decoded from the supplied kernel.

GAME imports DSOUND ordinal 1 (`DirectSoundCreate`) and COREDLL device-I/O
functions, and names `\\Device\\CDROM0`. Those observations support a CE
device-I/O audio investigation but do not prove a particular CDDA request.
DSOUND depends on SNDCORE/COREDLL; SNDCORE and WSEGACD also use PLATUTIL and
WDMLIB. The needed externals and kernel COREDLL are supplied. SYSSTART
explicitly searches for ARMCOM and reports sound failure if it is absent.

| Input | Bytes | SHA256 |
| --- | ---: | --- |
| `0WINCEOS.BIN` | 1,828,864 | `189719d8d3763284f74c5e66704710b9f9968118cb58aeb0a0844bd121a629ab` |
| `GAME.EXE` | 1,363,456 | `458f0df3238f31a40239f9a5507b29e4aca63f468982457f65216e0ed0d97444` |
| `WINCE/DSOUND.DLL` | 33,363 | `645e879953a9d9cec7cd0f03c77f37d3b19e5fa16da8d783af093320de3b4115` |
| `WINCE/SNDCORE.DLL` | 20,052 | `ba3cf8f6cb5c19c37042b13568e0254f5f36f3bcd2b385b66e73f0aa7303e4ae` |
| `WINCE/WSEGACD.DLL` | 24,657 | `3c74f32ee5fb9277a85cc486f4e4921daf6065eb780120a6db4400cdbd15c28b` |
| `WINCE/ARMCOM.DRV` | 31,068 | `be21517e85a9df38e6f7e03e056ece2b6996739b0ae79e874a9795deb6477477` |

## Comparison with admitted CE families

World Party is a fourth complete kernel identity. ROMHDR is `0x8c1cd614`
(84-byte CRC32 `935ae972`); RAM start/free/end are `0x8c1ce000`,
`0x8c1dc000`, `0x8cfd0000`. It has the same 27 module names and nominal
audio-module bases as Bust-A-Move 4, but reviewed addresses cannot be
inherited from that similarity.

COREDLL text differs from BAM4 by only 7 of 183,808 bytes, with the remaining
three sections identical. All 13 inspected existing wrapper prefixes match.
Other components require a new contract review:

- DSOUND stored text grows from 25,088 to 27,648 bytes; DirectSoundCreate
  moves from RVA `0x15ec` to `0x16e8`.
- SNDCORE exports grow from 70 to 72, adding SndSetMasterVolume and
  SndSetPortFrequency; existing ordinals/RVAs shift.
- WSEGACD stored text grows from 19,968 to 20,480 bytes, with another
  PLATUTIL import.
- ARMCOM grows from 30,888 to 31,068 bytes and has a different complete SHA.
- NK has shifted code and metadata; it is not merely a known kernel with
  rebased globals.

External DSOUND/SNDCORE/WSEGACD exports agree with their embedded ROM
counterparts, and external ARMCOM equals the embedded firmware byte for
byte. No mismatched sound-file input was found.

At the time of this initial audit, implementation requirements were the new kernel's globals/MMU/callback
contracts, revised DSOUND object/method identities, SNDCORE bindings and
pool reservation, followed by live validation. Existing streaming and
COREDLL mechanisms are candidates for reuse. A title or SHA table entry
alone would not establish these contracts. This static audit did not establish
a playback build or Tested status. Subsequent implementation and the scoped
accepted console run of `b08802f8945f` are recorded in the
[status-restart evidence](world-party-status-restart-2026-10-15.md). World Party
remains generally Experimental; the working Armageddon module is unchanged.
