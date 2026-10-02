# Windows CE loader audit — 2026-10-01

The owner requested Windows CE game support while testing SCI microSD. The
assumption to check was that the launch restriction existed only because SCIF
was too slow. This audit examines K-UI `6cc2abb460b56fd47b1b421672f89f94fc28cd74`.
It does not establish Windows CE boot or gameplay compatibility and does not
remove the launch restriction.

## Finding

The current restriction identifies an unimplemented boot profile, not a
transport throughput threshold. SCI improves the available storage throughput,
but there is a concrete executable-loading difference and a high-memory overlap
to resolve before a Windows CE launch experiment is meaningful. There is no
evidence here of a universal minimum storage speed for all Windows CE games.

The existing [storage transport guide](../storage-transports.md) already states
that faster storage does not add Windows CE support. The
[native Games milestone](../games-milestone-plan.md) deferred Windows CE as a
separate capability rather than rejecting it after a throughput measurement.

## Existing gates and assumptions

| Area | Current behavior | Consequence for Windows CE |
| --- | --- | --- |
| `src/core/game_metadata.c`, `boot_profile` | Parses the IP peripheral-field bit; `windows_ce` and `native_gd` are mutually exclusive. | Metadata inspection can identify CE images without launching them. |
| `src/core/shell.c`, `kui_shell_games_retail_ready` | Requires native GD and excludes CE. | Ordinary Launch is unavailable. |
| `src/apps/games.c` and `src/dreamcast/shell_draw.c` | Explain that CE launching is unsupported. | This is an explicit compatibility gate, not a failed speed test. |
| `src/apps/games_retail.c`, `kui_games_retail_prepare` | Unconditionally rejects `metadata.windows_ce`, then requires `metadata.native_gd`. | Selecting SCI or IDE does not bypass the preparation gate. |
| `src/loader/retail_stage.c` | Copies the entire boot file to `0x8c010000`; installs a 128-byte entry relay there. | There is no CE boot-file prefix handling. |
| `include/kui/retail_loader_layout.h` and `src/loader/retail_stage.ld` | The temporary high stage begins at `0x8ce00000`. | The CE prefix destination described below lies within the stage allocation. |
| `src/loader/retail_stage.S` and the stage relay | Enter owner bootstrap 2 at `0xac00e000`, with native VBR/stack `0x8c00f400`; later check that native state. | CE bootstrap/entry behavior has not been established against these assumptions. |
| `src/core/retail_gd.c`, `guest`, and resident `map_guest` | Recognize native physical RAM aliases and convert them to direct RAM mappings. | Arbitrary MMU-translated guest buffers are not implemented or tested. |
| `include/kui/retail_gd.h` and `src/core/retail_gd.c` | Virtual GD reads complete by polling; nonzero callback registration is unsupported. | A CE title requiring virtual GD completion interrupts or callbacks would need additional work. Physical SCI DMA is a separate mechanism. |

The associated shell, metadata and retail-preparation host tests intentionally
assert the CE rejection. Changing only their expected result would not supply
the missing boot behavior.

## Concrete raw-GD boot difference

The independent Flycast BIOS implementation provides a primary implementation
reference for the raw GD Windows CE file layout:

- Repository revision:
  [`flyinghead/flycast` `04669ebe4164bb589e6a2050aa1559724aa94291`](https://github.com/flyinghead/flycast/commit/04669ebe4164bb589e6a2050aa1559724aa94291).
- File:
  [`core/reios/reios.cpp`, `reios_locate_bootfile`](https://github.com/flyinghead/flycast/blob/04669ebe4164bb589e6a2050aa1559724aa94291/core/reios/reios.cpp#L91-L115).
- Retrieved content blob: `0ecde7993bc546bd49f473c5ef40eabfe9b423ab`.
- For a CE-marked, unscrambled GD boot file, that implementation loads the first
  2,048 bytes at `0x8ce01000`, then loads the remaining file bytes at
  `0x8c010000`. Its non-CE path has no such offset.

K-UI currently loads offset zero at `0x8c010000`. Simply allowing a CE image
would therefore retain a known mismatch with this reference. Adding only the
prefix copy would introduce another defect: its destination
`[0x8ce01000, 0x8ce01800)` overlaps K-UI's executing high-stage reservation.
The linked native stage contains code there. The stage also remains needed by
the entry relay, so it cannot be treated as dead immediately after loading.

These are concrete implementation differences, not observations from a CE
console launch. The correct CE handoff still needs independent validation. Do
not assume that copying the prefix and skipping it is the complete solution,
or that the native bootstrap flag change, low resident reservation and relay
checks are all valid for CE.

No Flycast or DreamShell implementation was copied or mechanically translated.
The referenced behavior supplies a format/layout fact for an independently
authored loader.

## Known gaps versus questions to measure

**Established in current source:** the transport-independent rejection, missing
prefix handling, overlapping high-stage placement, native-only boot-state
checks, limited guest mapping and polling-only virtual GD completion.

**Not established for an owned title:** whether its CE kernel overwrites our low
resident area; which virtual addresses it passes to the GD service; whether it
requires callbacks, additional GD commands, or a different interrupt contract;
whether its particular boot IP reaches the same relay entry; and whether its
runtime streaming load is satisfied by SCI. Mode 2 data is separately
unsupported, but the CE flag is not proof that an image uses Mode 2 sectors.

The Flycast/Libretro developers' original
[Windows CE support announcement](https://www.libretro.com/index.php/flycast-wince-libretro-experimental-core-released/)
identifies full MMU support as part of CE emulation. On the physical Dreamcast
the MMU already exists. K-UI's question is whether its resident can coexist with
the CE kernel and correctly interpret service arguments; it does not need to
implement a CPU emulator. That source is context, not evidence that ARMADA
passes translated buffers to K-UI's GD entry.

## First implementable milestone

Use the owner's **ARMADA** image as the first CE target. Existing console
[inspection evidence](games-armada-inspection-2026-09-24.json) already establishes
`/Games/ARMADA/ARMADA.gdi`, product `T40301N 00`, five tracks and
`0WINCEOS.BIN` at LBA 548388, length 1,253,376 bytes. It does not contain the
executable bytes or a CE boot trace. A fresh full rip or repeated metadata-only
inspection is not needed.

A separate **experimental CE boot probe** can be scoped without changing the
accepted native launch path:

1. Add an explicit CE boot profile and a separate package identity. The
   preparation/package handshake must reject older native-only packages and
   must not infer the profile solely from a boot filename. Keep normal Launch
   unavailable until the probe has established a working handoff.
2. Independently implement a checked load plan: a 2,048-byte prefix, executable
   body, destinations, rounded-sector limits and non-overlap with every live
   stage/resident/stack range. Give the CE stage its own linked memory layout,
   or an equally explicit final-copy handoff whose source code and relay survive
   the prefix write. Do not silently move or enlarge the native reservation.
3. Add a read-only preparation report for the prefix and body and instrument
   the CE experiment's handoff milestones and first GD requests. Include
   caller SR, VBR/MMU state, command and buffer address, and guard integrity.
   Stop with a durable diagnostic screen on an unsupported condition. This
   establishes what the actual title asks for before adding speculative
   address translation or interrupt behavior.
4. Validate the CE bootstrap path and resident survival on ARMADA, then add
   only the required request/mapping/completion behavior. Reaching a title
   screen remains separate from gameplay, FMV/audio and VMU save/load checks.

SCI can be the first probe transport because it is the owner's current setup.
It must not be treated as proof that the boot profile is correct or as a
universal CE eligibility rule. Native pacing improvements can ship independently
while this new boot path is developed.

## Required checks before a CE console probe

- Synthetic host fixtures for native versus CE IP flags, prefix/body placement,
  minimum/truncated files, exact limits, rounding and address overflow.
- Negative cases for every load/stack/resident overlap, including the current
  `0x8ce01000` conflict, and old/new package mismatches.
- Entry-relay state preservation and separate CE linker, stack and machine-code
  audits. Current native layout and native tests must remain unchanged.
- CRC verification on every physical SD block, bounded errors and CMD12
  cleanup. CE support does not justify removing those protections.
- A console photograph/trace proving the prefix/body handoff, first GD command
  and resident integrity before describing any title as supported.

This audit produces a concrete development target. It does not ship a CE
unlock, claim an observed CE crash, or attribute CE incompatibility to card
speed alone.

## Proposed development sequence after native testing

This sequence is **not implemented**. It records the owner's request to try
more native games before working on CE, with concrete, independently authored
probe boundaries. The [SCI inline CRC candidate](sci-inline-crc-2026-10-01.md)
is the next native change to measure first; host/build checks do not establish
a console speed improvement.

1. Retest DOA2's load, FMV and early-fight behavior, then sample other owned
   native titles with gameplay and VMU save/load checks. Record each title's
   result separately. Native launch support is not a promise that every game
   works; the present virtual GD service accepts CD audio commands without
   producing CDDA sound.
2. Add a CE-only preparation/probe package with an explicit profile handshake.
   Require CE metadata and the matching package, rejecting native/CE package
   mismatches. Preserve the existing native package and ordinary CE Launch
   restriction while these probes establish the handoff.
3. Link the CE stage at the candidate address `0x8ce10000`, retaining memory
   end `0x8cfe0000` and stack `0x8cff0000`. This starts 64 KiB above the native
   stage and leaves the prefix destination clear. The existing period-fix
   stage's linked code/data/BSS span was `0x16ec0` bytes; the candidate range
   provides `0x1d0000` bytes before the memory end. These are layout calculations,
   not proof that the CE bootstrap leaves this allocation intact. Re-link the
   entry, relay and package addresses; merely copying native-linked code to
   the new address is invalid. Keep native memory reservations unchanged.
4. Implement a pure checked load plan, then a placement-only console probe.
   For the inspected ARMADA file, the proposed split is:

   | Part | File offset | Image LBA | Bytes | Destination, end exclusive |
   | --- | --- | --- | --- | --- |
   | Prefix | `0` | `548388` | `0x800` | `[0x8ce01000, 0x8ce01800)` |
   | Body | `0x800` | `548389` | `0x131800` | `[0x8c010000, 0x8c141800)` |

   The file is exactly 612 sectors: one prefix sector and 611 body sectors.
   The prefix ends `0xe800` bytes below the proposed stage. Validate every
   sector header and physical SD CRC, then report separate prefix/body
   checksums and verified destinations. This first probe stops before entering
   the owner bootstrap. For other files, validate sector rounding, declared
   byte lengths, arithmetic overflow and every live source/destination range.
5. Once placement passes, add a separate bootstrap-entry probe that preserves
   entry state and reports stack, SR, VBR, cache state and resident integrity.
   Independently validate the CE IP flag behavior and relay assumptions;
   neither the native flag edit nor its exact VBR/stack expectations is an
   established CE contract. Keep the stage and original-entry data live until
   the relay is finished, and leave no runtime pointers into that stage.
6. Probe the first GD requests, recording original argument/buffer addresses
   before `retail_gd.c` canonicalizes them, together with caller SR/VBR and
   MMU state. Stop before dereferencing an unsupported mapping. The current
   physical-alias conversion is not a general virtual-address translator.
   Fit probe diagnostics within checked resident/stack limits; do not assume
   the CE kernel preserves disposable high-stage memory for a trace buffer.
7. Implement the request, mapping or completion behavior that the owned title
   actually requires. Confirm resident survival and a working title screen,
   then test gameplay, FMV/audio and VMU save/load as separate milestones.

Before console execution, test truncated prefix/body data, exact load limits,
rounding/overflow, package/profile mismatches, every live-range collision
(including the original `0x8ce01000` conflict), relocated relay targets and
entry-state preservation. Audit the CE ELF, BSS, stack and instructions while
retaining native checks. Faster SCI and successful placement alone do not
establish Windows CE boot or gameplay compatibility.
