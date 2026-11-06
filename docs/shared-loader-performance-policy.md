# Shared launch performance policy

Return controls must preserve any original options menu needed for saving,
configuration or disc actions. Prove both combo-to-options and explicit
menu-to-return paths before replacing an exit edge. A completed worker SYNC
does not substitute for the engine's cartridge-SRAM Save Game command. Keep
that command reachable and retain original dirty-save confirmations.

The default is selected by a loader's capabilities and execution phase. A game
does not need an opt-in name, hash or additional launch profile to receive an
existing performance optimization. SDK recognition and ABI support are separate
from the implementation used for reads, hashes, ownership and recovery.

## Common native stage

`Makefile.vmu-native-compat` supplies these defaults to Standard, Background and
the derived physical-disc build:

| Work | Shared policy | Bound retained |
|---|---|---|
| Full executable CRC | Existing byte-table CRC | Same complete bytes and IEEE CRC |
| Program/heap/exit scan | Existing aligned decoder | Same recognition/refusal rules |
| Pure executable extent proof | Scoped operand cache | Original proof; unknown CCR uses unchanged uncached call |
| Immutable resolution replay | Scoped operand cache | All seven arguments, full SHA, independent record digest |
| Fresh FAT ownership reads | Verified 16-sector windows | Fresh VBR, each block CRC, mirror checks and epochs |

Image and physical-disc stages include the same assembly proof scopes. The
resolution scope copies the three incoming stack arguments into its own outgoing
argument area and preserves all four register arguments. Only the pure replay
leaf runs with operands cached. The stage publishes RAM before restoring the
original cache profile; SDK calls and live ownership checks remain outside.

The existing Smash zero-argument scope retains its separately checked symbols
and execution contract. Other native families receive the equivalent shared
seven-argument scope without an adapter tied to a particular executable.

Stage FAT windows are temporary installer scratch. They do not enlarge the VMU
worker, reserve game RAM, reuse ownership snapshots across launches or permit a
cached filesystem profile to authorize writes. Identity, write and sync epochs
invalidate both windows and the learned geometry.

## Preserve current features

The current Background default is the combined compact profile built by
`Makefile.vmu-native-background-compact` and its A1 variant. Both New/X and
Legacy/Y select the matching `retail-native-dual[-a1].kui`, preserving the
72 KiB worker, 284 KiB complete reservation, full 192 KiB read cache and
physical/virtual VMU toggle. Every stage, worker, low engine and auxiliary
must carry the same exact compact geometry; a General or session-control
module cannot satisfy the normal release gate.

The older 112 KiB General and optional session-control profiles remain
separate source variants. Their return/disc-set candidate must not become the
normal update by changing a dispatch filename or by treating its larger
reservation as the baseline. No such module is installed by this release.
Old `retail-native-session[-a1].kui` files from the superseded update are not
selected by the corrected default route.

The default Standard SCI family retains its paced receive policy, virtual A2,
72 KiB worker, 64 manifest slots and guarded low stack. The shared New-low
compiler policy is applied at both compilation and LTO linking. Legacy reader
and gameplay-worker compiler policies are unchanged.

WinCE/CDDA execution has different ABI and memory ownership. It still uses the
shared UI/history and core file operations. Native cache-mode assumptions must
not be applied to a live CE runtime merely because a title uses the same card.

A bootstrap execution slot is not automatically immutable during gameplay.
Preserve its admitted image in authenticated, already-owned initialized data
when its lifetime cannot be proved, and validate the complete restored slot at
the execution boundary after device/cache/MMU retirement. A compact initialized
prefix may represent canonical zero padding only when a fresh-build byte check
proves the complete image; CRC coverage must still include every padded byte.
Do not add startup writes or a new saved-copy lease without proving ownership,
or refresh an expected checksum from changed game RAM to make a guard pass.

For a return-only change, identical reader/helper hashes and unchanged copy
assembler are insufficient startup preservation. Compare the linked copy
entry and bounds, complete pre-kernel write-address trace, mutable runtime
window, BSS/saved-hook locations and non-return forwarding state with the
accepted configuration. Pin those boundaries when the additional low-memory
lifetime is unproved. A larger return image is also a boot change if it is
installed before the kernel starts, even when its new functions run only on
exit. A negative control must reject the known expanded-image candidate.

## Build-coherent Standard admission

The GUI's historical Standard allowlist remains valid. A coordinated new build
also generates an exact manifest from its linked Standard SCI module:

```
python3 tools/native_standard_manifest.py \
  --build build/universal-standard-sci \
  --header build/universal-native-standard-manifest.h \
  --report build/universal-native-standard-manifest.json
```

Build the GUI with `NATIVE_STANDARD_MANIFEST` set to that header. The recognizer
pins the complete payload, size, profile offset and exact build ID. Only its one
valid 4096-byte prepared card profile is mutable. It never learns an allowlist
from files found on the SD card. New and removed manifest options invalidate the
recognizer object; stale or mixed cohorts fail closed.

The generator binds entry/stage/worker/low files, source provenance, paced SCI,
virtual A2 and the 64-slot layout. Final packaging must also run the complete
linked family audit, verify the GUI's embedded digest and include the generated
manifest. Generation replaces handwritten per-build offsets and hashes, not
functional proof or current card admission.

## Regression gates

- `test_native_performance_defaults.py` evaluates all ten effective build
  families, including slots and transport. It explicitly checks that the
  default combined Background variants retain the compact geometry; historical
  General and optional session profiles remain separately labelled variants.
- `test_native_stage_proof_scope.py` executes the seven-argument wrapper over
  3,840 ABI/cache cases and rejects 26 unsafe mutations. The original immutable
  replay and live ownership tests remain mandatory.
- `test_native_standard_manifest.py` executes production admission with and
  without the generated cohort, validates mutable-profile behavior and refuses
  changed code, labels, size, build, duplicate profile and corrupt records.
- `test_shared_loader_compact_release.py` rejects larger or mixed component
  geometries, crossed A1/A2 slots, optional session capabilities and any
  installable module outside the exact eight-file functional cohort.
- The existing full loader, raw CRC, cache gateway, return, worker lease, FAT32,
  exFAT and physical-disc handoff tests remain delivery gates.

These gates establish implementation and artifact contracts. They do not claim
console elapsed times or that an unrecognized SDK family is already supported.

For collection loading, report selection-to-bootstrap and internal
selection-to-Start/Resume separately, with VMU settings and cold/after-return
state. Preserve the current allocation-epoch/checkpoint path instead of
reintroducing a complete host scan for every child. Fresh identity and
current-data validation remain required; a stored receipt or cached profile
is not a replacement. See `global-launch-policy.md` for shared lifecycle and
delivery requirements, including transport-window reuse and deferred UI I/O.
