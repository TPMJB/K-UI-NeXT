<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Quake III Arena executable and allocator audit

Read-only follow-up to the [initial sample](quake3-startup-inventory-2026-10-15.md).
The owner supplied `Quake-III-runtime.zip`, 1,274,679 bytes, SHA256
`beac35b5103e3172bbc10ee0fa870777052f34ebe920e48c48dd6c430f237a98`.
All archive CRCs and every reported payload size, CRC32 and SHA256 checked.
Retail binaries and disassemblies remain private outside the repository.
Addresses below are scalar interface observations for this exact version.
They do not authorize patches or establish playback compatibility.

## Additional executable identities

| File | Bytes | CRC32 | SHA256 |
| --- | ---: | --- | --- |
| `QUAKE3/Q3_USA.BIN` | 1,316,164 | `21b6b077` | `42b40b4f2e94b56f16d81fbb132987a48f231a5d02962d0498e8e5a917cdd5ae` |
| `QUAKE3/INTRO.BIN` | 354,724 | `3c6d3f36` | `c8d4ea08a23a21023ddd368dcae7750fe4322afa73815c4eca8351e9f49d535b` |
| `GINXFER.BIN` | 8,436 | `688c64ed` | `fa12196993a7c757a78565963e257cd33443f80b3f226729fdc1cf7da51ed5b3` |
| `GINRESET.BIN` | 153,024 | `7bdd60e5` | `4f2d3acada47ad58d210df97f26e8b2f9821e952cb0886fdfa20a515e2e35db2` |

`GINSU.INI` selects INTRO as first/default application, names GINXFER,
sets INTRO's command to `QUAKE3/INTRO.BIN`, and sets FirstGDDA to 4.
INTRO's active call at `0x8c010450` invokes transfer function `0x8c01234a`
with the main game path. Thus the boot chain is launcher → intro → main.
FirstGDDA is passed through transfer to a routine that validates a track
number and stores the first-audio base at `0x8c0080fd`; it is not itself
an observed CDDA play request.
`SYSTEM.ZIP` passes ZIP CRC checks and contains 18 script/texture/font
members, with no named executable. Further audio payloads are not needed
to investigate the identified runtime and lifetime problems.

## Handoff conflicts with the current worker

The intro's transfer code uses top `0x8cfe0000`, subtracts each file's byte
size and aligns down to 64 KiB for staging. Reads round up to 2048-byte
sectors. With these exact files, the half-open write spans are:

| Write | Begin | End |
| --- | --- | --- |
| Transfer executable, 5 sectors | `0x8cfd0000` | `0x8cfd2800` |
| Staged intro executable, 174 sectors | `0x8cf70000` | `0x8cfc7000` |
| Staged main executable, 643 sectors | `0x8ce80000` | `0x8cfc1800` |
| Main executable copied into its load address | `0x8c010000` | `0x8c151800` |

The boot launcher uses the same transfer implementation and destination.
The transfer executable directly overlaps the current native CDDA worker
at `0x8cfd0000` during both launcher → intro and intro → main. Intro code
hooks also lie inside the main overwrite span.
GINXFER's copy/restart code resets VBR/SP to `0x8c00f400`, sets SPC to
`0xac010000`, and returns through RTE. A worker installed for the intro
cannot be assumed to survive or remain serviced by the main executable.

Changing only a driver profile or recognizing the boot name cannot resolve
this. A future adapter must establish a valid memory lease in each phase,
preserve or reinstall its state across transfer, and reattach main service
and shutdown hooks. Another apparently empty address is not a reservation.

## Main game sound path

Both intro and main contain Audio64 host SDK version `11.0.h.2`; the two
external ARM driver files remain the identical `1.90h.2` sample already
recorded. Main game initialization at `0x8c0544e0` explicitly reads the
driver file into allocated host memory, then calls `acSystemInit` at
`0x8c0bb5d8` with the buffer and file size. This differs from Toy's active
amInit-driven installation path.

| Main SDK observation | Address |
| --- | --- |
| Driver installed flag | `0x8c10f3b8` |
| Common SDK state | `0x8c526c0c` |
| Sound base / first free / queue | `0x8c526c10` / `0x8c526c18` / `0x8c526c1c` |
| Producer halfword / notify | `0x8c526c20` / `0x8c526c24` |
| Host command publisher / callback slot | `0x8c0ce660` / `0x8c10f9f0` |
| Game's saved sound first-free / base | `0x8c2e2bac` / `0x8c2e2bb0` |

The publisher uses 32 slots and publishes the opcode last. These shared
mechanics support further family analysis; the different state locations
and ARM packet-clearing mode still require their own checks.

SDK amHeap/amVoice code is present, but caller inspection does not establish
it as the game's active allocator. In particular, inspected main code has
no literal references or BSR calls to amInit/amShutdown/amHeapAlloc, and the
amVoice allocator reference is internal to an SDK wrapper. That evidence
does not prove absence of every possible indirect call. It does rule out
treating function presence alone as an ownership proof.

The active main allocator is the Quake hunk: initialization `0x8c0746b8`,
bottom allocation `0x8c074566`, top/temp allocation `0x8c0745da`, and temp
free `0x8c074632`. Its base follows aligned BSS plus `0x4000`, and its
ceiling is `0x8cff0000`. The initialized base is `0x8c54f460`, with size
`0x00aa0ba0`; the stack extends above the hunk toward `0x8d000000`.
The game's inspected sound path uses 48-byte voice records and a 48-port
bound, with a nearby additional 16-record table. Upper ports cannot be
assumed free across game use, intro, driver reload, or shutdown.

Broader native detection must therefore establish active caller paths,
allocator ownership and executable transitions as well as SDK/driver
identity. It also needs a proven normal-context service opportunity and
sound-memory/voice ownership before any playback admission. No Quake III
runtime adapter, Tested label or hardware validation
is claimed by this audit. The working Worms Armageddon module is unchanged.
