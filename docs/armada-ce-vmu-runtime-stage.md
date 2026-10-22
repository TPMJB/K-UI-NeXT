# Armada CE VMU durable launch preparation

The writable CE experiment selects an existing prepared VMU pair before NK
starts. It uses the existing filesystem admission, redo-journal recovery,
VMU validation and checkpoint code. It does not create a blank store or guess
sector addresses. The ordinary cached readonly build retains its accepted
24 KiB reservation and byte-identical payloads.

Three explicit Make switches are required together:

| Switch | Capability |
| --- | --- |
| `ARMADA_A2_WRITE_LAYOUT=1` | Reserve the separate 48 KiB CE profile |
| `ARMADA_A2_WRITE_STAGING=1` | Admit the bounded WRITE packet staging path |
| `ARMADA_A2_WRITE_RUNTIME=1` | Link the durable provider and native continuation |

The runtime also requires `SD_PROFILE` naming an existing canonical 4096-byte
`KUIVMUP2` profile. The profile is checked by `tools/vmu_persist_profile.py`
and embedded only in the isolated high stage. No profile is committed with
the source. Selecting this flag authorizes preparation of that existing pair,
including recovery and checkpoint writes; a failed preparation stops launch.

## Preparation and publication

`armada_maple_hook_reserve()` calls preparation only after exact CE source and
kernel admission, reservation checks and worker-source verification. NK and
the asynchronous reader are not running yet. The stage acquires the already
initialized SCI card exclusively and supplies serialized identity, read,
programmed-write, sync and health callbacks to the common preparation code.

The CE wrapper accepts exactly the existing SCI/ASYNC manifest. It preserves
the original manifest and every executable/audio extent. Only a private copy's
reader selector becomes STANDARD so that the shared scanner's conservative
pre-NK admission can be reused. The real I/O still uses the explicitly
serialized callbacks. Wrong transport, reader mode, filesystem ownership,
card identity or overlapping allocation fails closed.

Preparation recovers an existing redo journal if necessary, validates the VMU,
checkpoints it, refreshes current block hashes and verifies a zero journal
marker. It then seals a 1272-byte handoff. Successful reader stop, physical
release and a persistent fault-latch check are required before publication.

The high stage separately reads the five-word loan descriptor from the exact
SCIA resident blob: offset 52 in this STREAM=0 profile, or 56 with STREAM=1.
Magic, version, size and both distinct even P1 code pointers are checked.
Pointers must be in that actual blob and below `0x8c00d800`. A separate sealed
32-byte service binding carries those admitted entrypoints to the runtime.

The stage zeroes and installs the reservation, then copies the sealed handoff
and service binding through P1. The existing full-RAM cache purge publishes
those writes. It does not mix P2 stores with dirty P1 zero-fill lines. The
cache-off relay compares all reservation bytes, allowing only installed code,
the exact handoff/services and the existing terminal vector; it independently
revalidates both seals before installing the owner hook.

## Retained memory

| P2 range | Purpose |
| --- | --- |
| `0xacfc4000` up to `0xacfce200` | Immutable helper code plus private runtime state |
| `0xacfce200..0xacfce6f8` | Sealed durable handoff, 1272 bytes |
| `0xacfce700..0xacfce720` | Sealed resident service binding, 32 bytes |
| `0xacfce800..0xacfcf800` | Dedicated unmasked storage worker stack, 4096 bytes |
| `0xacfcf800..0xacfcfe00` | Existing guarded Maple callback stack, 1536 bytes |
| `0xacfcfe00..0xacfd0000` | Existing diagnostic reports and terminal metadata |

The separate runtime linker requires exact section sizes and rejects code or
state reaching the handoff. The stage's embedded-code bound also ends at the
handoff. ELF-only runtime markers pair the helper, high stage and SCIA image.
The stage also publishes exact absolute handoff and service address markers;
package admission rejects a stage from a different retained-record layout.
This placement leaves 224 bytes before the unchanged worker stack and moves
512 bytes of previously unused padding into the code/state capacity.

The low runtime resident omits its periodic framebuffer meter and unused CE
history ring while retaining fault reports. Its separately gated service stack
is `0x8c00dc00..0x8c00e000`. The top remains the original boundary before
bootstrap2; the bootstrap and retained boot VBR are not used as stack space.
The readonly low linker and its 2 KiB service stack remain unchanged.

## Validation scope

`make -f Makefile.armada-persist-test test` runs the new ASan/UBSan fixture
checks. They cover successful serialized admission, immutable original game
extents, real journal recovery/checkpoint, one-shot state, identity/ownership
and allocation refusals, failure-zeroed handoffs, and malformed resident
descriptor rejection. `tests/test_armada_ce_vmu_layout.py` checks retained
boundaries, explicit capability selection and absent runtime sections by default.

An integrated native SH build with a clearly synthetic profile links the high
stage, low resident and retained helper. The measured low paths are 452 bytes
for GD dispatch, 244 for SCI IRQ including its 28-byte assembly frame, and 452
for the terminal. The GD entry's 36-byte save and deferred callback's 28-byte
frame are on the original CE stack, outside the low private stack. The
1024-byte private stack retains 460 bytes beyond the worst path, 48-byte
guard/alignment allowance and 64-byte audit margin. Low loan begin/end run on
the high worker stack and are measured separately.

Complete native continuation and provider admission now pass as well: the
masked proof uses 1,100 of 1,536 bytes including its shim and audit margin;
the unmasked storage service uses 1,268 of 4,096 bytes including a nested IRQ
and audit margin. The package rejects 1,420 tested mutations of critical
instructions, runtime and retained-address markers, the loan descriptor and
canonical profile.
The default package audit also passes. Detailed machine-readable results
are included with the console test package.

Hardware save/readback and restart recovery remain acceptance gates. The
accepted cached readonly playback result remains the comparison point; no
new writable hardware performance claim is made. This source-specific CE path
does not establish universal owner-hook detection or a physical GD-ROM loader
integration; those can share the storage/protocol contract after their own
launch, reservation and owner checks are implemented and validated.
