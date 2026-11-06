# Shared native bootstrap consolidation

This records the historical7fc candidate. The current Games-only cohort and
its exact default admission are recorded in `shared-game-launch-performance.md`
and `evidence/games-eeed38b`. Do not restore this candidate's bindings over a
later qualified Games cohort.

The owner reports Power Stone 2 reaches bootstrap in about four seconds on
9d6680a, versus almost twenty seconds before. This establishes the reported
preparation improvement, separately from bootstrap and save/return coverage.
The owner also recalls about two seconds through Smash bootstrap with VMU
hooks; its particular child/build and an instrumented timing were not supplied.

## Established and missing work

The recovered dbadd archive has SHA256
`23c3a4387c44980682b643f62d732e60eb4037dc307822b7d8a75f0a009d94ed`.
Its five actual modules and linked reports already establish byte CRC,
aligned decoders, cached extent/SDK replay and fresh sixteen-sector FAT
windows. The recent KUPs inherited those services; package omission does not
prove an optimization is missing from a module.

Exact source and binary comparison established three later shared changes:

| Change | Refreshed services |
| --- | --- |
| Bounded cached whole-executable CRC after BOOT2, with full byte coverage and unknown-state fallback | Standard and Background A1/A2 |
| Fresh admission metadata memo and full-base validation reads in contiguous runs of at most 32 blocks | All five native VMU services |
| Remove two fixed thirty-frame informational holds; retain diagnostic opt-in and fault screens | Standard and Background A1/A2 |

The physical-disc path retains its existing CRC/cache contract. Do not apply
a native cache-mode assumption to a live WinCE runtime. The metadata/snapshot
cache is temporary high-stage scratch, outside retained game RAM. Identity,
write, sync and phase boundaries invalidate it. Both complete image validations,
recovery, allocation, block CRC and whole hash checks remain. Smash's collection
allocation epoch does not authorize an unrelated cold launch.

## Actual composition

Candidate: `KUI-Unified-Bootstrap-06NOV2026-1244-CST-7fca3e3.kup`, 6,697,156
bytes, SHA256 `52d06112910f84c53b6e1d2ab500c604cafb320b9f181c4b448177df9d82180d`.
Its 26 replacements contain the GUI, these five services, and all twenty
non-GUI files from the 9d test unchanged. Accepted Smash modules/helpers and
the four-module embedded bundle stay exact.

| Service | Install path |
| --- | --- |
| Paced SCI Standard, virtual A2 | `apps/games/retail-boot.kui` |
| Compact Background New/Legacy, virtual A2 | `apps/games/retail-native-dual.kui` |
| Compact Background New/Legacy, virtual A1 | `apps/games/retail-native-dual-a1.kui` |
| Physical-disc virtual A2 | `apps/disc/native-disc-vmu.kui` |
| Physical-disc virtual A1 | `apps/disc/native-disc-vmu-a1.kui` |

All five retained VMU workers match independent verified references apart from
permitted build-label/slot metadata. Background retains its 72 KiB worker,
284 KiB complete reservation and existing 192 KiB cache. Both reader engines,
physical/virtual toggle and paired slots remain. Compact New/Legacy low readers
also receive the previously accepted SCI lost-byte quarantine; source comparison
found no other active-reader semantic change. No SCIF VMU module or ordinary
`ce-probe.kui` replacement is installed. No new SDK, format or memory layout is
claimed; the personalized Armada adapter remains inherited.

## Reproduce and preserve the configuration

The canonical recipes are `Makefile.vmu-native-vmu-lite-paced`,
`Makefile.vmu-native-background-compact[-a1]` and `Makefile.disc-vmu[-a1]`.
Use committed source and separate output directories. Generate Standard
admission from its linked paced-SCI module with `native_standard_manifest.py`.
Qualify full families, selected callers and actual installed bytes before the
current KUP producer. Use shared-performance mechanisms for these services and
qualify accepted Smash separately; old packaging scripts which rebuild or
substitute Smash are not this candidate's producer.

The binaries were compiled at `7fca3e37f40557066116e88ab817e91ef7dd50d3`.
GUI inputs were the new Standard manifest, retained 72db native-CDDA manifest,
accepted cc81 bundle and launch timing enabled. Both exact manifest/header
pairs are promoted to the default `resources/updater-bindings` paths in the
preservation commit. No binary rebuild or retag occurs during that promotion.
Later default GUI builds must not revert admission to an older service cohort.

Old defaults are retained in `evidence/bootstrap-7fca3e3/previous-defaults` for
an explicitly old-cohort build. A GUI-only update for an older installation
must retain its exact admission or include the new services. Neither presence
nor a build string alone grants admission.

## Qualification and remaining hardware check

`evidence/bootstrap-7fca3e3/qualification.json` binds source, modules, GUI,
manifest tables and embedded copies. Five worker comparisons, geometry,
stack/embed checks, full CRC/cache models, selected callers, FAT windows,
snapshot invalidation, zero-frame holds and negative controls pass. Production
GUI admission accepts all four preserved Smash modules and both helpers. The
exact KUP passes 5,742 install/restart/restore/recovery checks on each of FAT32
and exFAT through the actual GUI and current C consumer.

The old Standard support symbol comparator refuses changed zero-size/debug
records; that refusal remains recorded. Complete initialized runtime bytes,
allocated functional symbols/BSS and the current safety audit pass. Do not
report that metadata-only refusal as a passing historical check.

Console bootstrap duration with Power Stone 2's same settings and VMU hooks
remains pending. Models and command counts are not console seconds. Main
remains unchanged pending the test.
