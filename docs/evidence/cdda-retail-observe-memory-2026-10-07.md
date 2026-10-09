# Profile 14 native SCI observation audit — 2026-10-07

The separate observer builds and passes the existing low-memory, conservative
private-stack and linked-instruction checks. It is prepared for console testing;
these build and host results do not establish retail compatibility or CDDA
resource ownership. The ordinary native SCI resident is byte-exact to the
independent baseline at a fixed build ID.

## Actual provisional build

Command: `make -f Makefile.retail_observe BUILD_ID=000000000000`, using the pinned
SH compiler. This is a provisional audit ID. The final package must rebuild with its exact source checkpoint ID and rerun
the same actual-ELF checks. `build.json` records whether GitHub publication
succeeded; an unpublished package contains its exact source snapshot.

The observation-only SCI resident uses `-Os -flto -mrelax
-fno-tree-scev-cprop`, reserved FPU registers and integer-only division. Its
separate build directory is `build/retail-observe`; ordinary targets and
`Makefile.dc` are unchanged. Disabling SCEV propagation reduces the diagnostic's
live compiler temporaries within the existing stack budget.

| Linked image | File payload | Memory end |
| --- | ---: | --- |
| Entry | 75,704 bytes | `0x8c0227b8` |
| Temporary high stage | 67,512 bytes | `0x8ce3d928` |
| Native SCI resident | 11,908 bytes | `0x8c0077e4` |

The resident starts at `0x8c004000`. Its code, data and BSS stay below the
unchanged `0x8c007800` stack boundary, leaving **28 bytes**. Its private stack
remains `0x8c007800..0x8c007d00`. The original checker subtracts the 16-byte guard
and 32-byte alignment gap from 1,280 reserved bytes: **1,232 bytes are usable**.
The conservative sum of all **47** final LTO C-frame reports plus the unchanged
**256-byte assembly allowance** is **1,216 bytes**, leaving **16 bytes**. This
sum includes emitted initialization and mutually exclusive terminal paths; it
is not a measured console high-water mark. No bound or allowance was widened.

`observation_layout` in [the packager](../../tools/package_cdda_preflight.py)
checks these actual three ELFs, exact `.bin` bytes, BSS, low relocation tuple,
guard symbols, executable trampoline, blank 4,096-byte card manifest and embedded
stage bytes. The stage's four legacy transport slots contain four exact copies
of the one SCI resident. Hard admission rejects other transports/readers before
selection; there are no fictitious SCIF, IDE or background-reader ELFs.

The linked instruction audit passes 16 entry, 7,544 stage and 5,006 resident
instructions. Only the existing named one-time high-stage FPSCR initialization
is allowed. No resident or relay FPU instruction is allowed. These instruction
counts are from the provisional linked images, rather than compiler flags alone.

## Admission and resource boundary

The high stage retains the full 4,096-byte wire decode and 160-slot manifest.
Before copying its 64-slot prefix into the low resident, it requires:

- The exact original 451-byte Toy Commander descriptor CRC `e25531d1`, all 15
  recorded track LBAs/control values/2,352-byte strides and zero file offsets.
- Standard synchronous native SCI, with no CD/scrambled-map flags.
- At most 64 combined track/extent slots, with a nonzero extent count for every
  track, including all audio tracks. The normal full manifest decoder still
  validates the card extents and map bounds first.
- After the existing complete loaded-IP checksum check, actual `GD-ROM` media,
  seven valid hexadecimal peripheral digits and a clear Windows CE bit in the
  loaded IP bytes. This follows the existing metadata interpretation; it does
  not guess a product code or require `BOOT_CRC` on ordinary raw GDI maps.

Profile 13 must first identify and inspect the complete card image. Profile 14
adds observation to the existing baseline launcher/reader; it does not install
the standalone CDDA engine, reserve sound RAM/channels/timers, create an IRQ hook
or add an allocation in the game's arbitrary RAM. Existing baseline reader
initialization, SCI/SH-DMA activity, pacing and launch/return operations remain
their ordinary responsibilities.

## Observed values and stopped paths

The original native assembly entry supplies the caller's pre-switch SR, PR and
SP. Observation records first/latest SR, VBR, GBR, PR, SP and MMUCR; raw first/latest
TSTR, FRQCR and all three TCOR/TCNT/TCR triplets; and validated PLAY20/PLAY21
parameter triples copied by successful REQUEST. Capturing those triples under
the existing lock adds no unvalidated guest pointer read. It reports actual
parameters without assigning unproved PLAY21 endpoint semantics.

The new MMIO sampler uses volatile **read-only** accesses. It neither suspends
G2 DMA nor writes AICA, ARM-control, TMU, clock or IRQ registers. Enabled/active
G2 DMA or an observed busy FIFO causes the sound sweep to be skipped. Sparse
sound sampling occurs on the first GD call, every 256 subsequent calls and an
accepted PLAY request. It records raw master/ARM/IRQ state, a selected-register
channel fingerprint, sampled KYONB configuration bits, and first/latest/ever
DMA masks. KYONB bits do not prove that a voice is audible or currently running.
The channel sweep is sequential, not an atomic sound-state snapshot. Unsampled
channels/registers, skipped sweeps and changes between calls remain unknown.
Raw TMU change masks include changing counter words; snapshots do not measure
or bound intervals in which the game makes no GD calls.

Terminal rendering reuses the existing image sector cache **only after a path
that will never resume image I/O**. C11 word-array union overlays keep report
iteration within actual array members; fixed size/offset assertions and host
checks cover the overlays. Normal ABXY+Start return displays four observation
pages for 1,200 video frames each, then invokes the existing firmware reboot.
A fatal GD stop first displays `14 STOP` and function/argument/guard for 1,200
frames, then the same collected pages; it holds the final page until power-off.
Terminal reporting makes no further resource samples.

## Host and baseline checks

ASan/UBSan passes **131** focused core checks for copied PLAY first/latest
values, saturation, snapshot masks/key unions, layout overlays, exact admission
refusals and native-IP/CE/malformed-field refusal. The existing GD host suite
passes **668,180** checks with the observation-only rejection-detail layout and
**668,194** with the ordinary layout; the latter includes 14 extra diagnostic
field assertions. Acceptance/refusal behavior is retained.

The final guarded source was separately compiled and linked with the ordinary
native SCI flags and fixed ID `cdda-audit00`. Its raw resident is byte-exact to
the independent original baseline:

- File bytes: **11,140**.
- SHA-256: `0e9b3df402f6488701333f8574c543e489cb479225de22040637bc69d8230d99`.

This proves ordinary native SCI payload preservation for that fixed-ID build.
It does not replace console testing of the diagnostic, demonstrate sound
ownership, bound future CDDA service opportunities or prove a retail music hook.
