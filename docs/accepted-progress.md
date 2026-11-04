# Accepted progress and updater preservation

Reconciled 4 November 2026 against current main
`0045dd96937ee7485ecd4fe0432b28babea8646a`, actual retained deliveries and the
current performance-test audit. This is a component ledger, not a claim that
every combination has been tested. Update it when new evidence supersedes a
component. Never use this source anchor to discard subsequently accepted work.

| Area | Progress to retain | Updater treatment |
| --- | --- | --- |
| Physical VMU/import | Bounded reads, whole-save recovery, black-card backup recovery, importer choices and duplicate handling | Keep current main implementation; reader binaries remain installed |
| Save labels/matching | Embedded save details, broader recognition including Zombie Revenge/Prince of Persia, remembered confirmations and contradictory-metadata checks | Keep existing catalog, match-memory and wizard code; preserve user data |
| Disc cards | Identify inserted discs without SD rips, link imports, create blank cards, defaults and stale-media checks | Keep current implementation; do not inflate the recorded hardware acceptance of the management menu |
| GD Play | A1/A2 choice, executable admission up to 12 MiB subject to staging geometry, fresh proof replay and launch preparation optimizations | Keep current source and installed disc modules; a GUI add-on cannot upgrade an old installed module's limits |
| PAL Shenmue II | Owner reported successful physical boot on NTSC and completed ripping; main records the accepted correction | Keep multi-DATA-track bounds, acquisition/recovery and bootstrap handoff code |
| Games | Favorites/Recent, cached catalogue, multidisc grouping, selection and per-disc choices | Preserve implementation and preferences. Main also contains later Flow/search/recommendation/history work whose combined console acceptance remains pending |
| Standard VMU | Separate paced Standard service uses 72 KiB without the 192 KiB prefetch cache | Inherit installed module unchanged; never substitute ordinary `retail-boot.kui` |
| Compact combined Background | Accepted 72 KiB worker, 284 KiB total reservation including 192 KiB cache | Inherit installed module unchanged; do not install General112/324 KiB |
| WinCE/CDDA | Accepted bounded shared 32-pump playing allowance, aligned copies, 16-sector jobs, priority 2 and ownership/durability rules | Inherit installed modules; newer diagnostics do not become accepted replacements automatically |

The compared VMU recovery/import/matching, disc-card/launcher and catalogue/group
source files are identical between main and the updater source before the
intentional resolver changes. `tools/update_addon.py` checks native loader,
worker-recipe and critical application source parity before its GUI build.
The exact checked paths and source anchor are recorded in its build report.

## Memory modes and remaining work

The independent VMU service was introduced in `a148ce85fe62` and reduced to
72 KiB in `0b9dcb4bcd41`. The paced Standard VMU-only route retains that
separation. The combined Background profile retains its full reservation when
its VMU service is disabled. Its Auto-prefetch capability already appears in
the accepted retained binary; that string alone cannot identify a regression.

The required choices remain VMU alone, prefetch alone, both, and neither.
This review has not established a matched modern72 + New + cacheless joint
profile or a true standalone prefetch-only allocation on every launch route.
The updater preserves installed behavior and does not claim to repair that
reader integration gap. In particular, Sega Smash Pack/WinCE must not be
advertised as using a native Background route merely because that module exists.

## Package scope and acceptance

The withdrawn `28cadccf0e71` to `33b04057db86` ZIP incorrectly replaced the
213,352-byte paced Standard file with a 74,936-byte ordinary file and added
presence-sensitive historical adapters. Its A/B agreement proved neither
preservation of the previous installation nor actual game-route compatibility.
The full-cohort release helpers remain blocked.

The replacement add-on delivers `KUI/runtime.kui` and one GUI-only `.kup`.
Every other catalog entry explicitly retains the existing root module and its
presence or absence. These are shared root files, not snapshots in each bank;
manual changes to them affect all versions that inherit them. Included bank
replacements are immutable, SHA-checked and never fall back to root.

Host checks cover package/GUI admission, explicit inheritance, real filesystem
installation, absent optional modules, runtime handoff, rollback, unchanged root
bytes and protected save data. Console acceptance is limited to opening Updates,
installing the supplied candidate, restarting and restoring the previous GUI.
No repeat of settled game compatibility or save/import tests is requested.

Evidence includes `docs/shenmue-ii-disc-test.md`, `docs/disc-card-linking.md`,
`docs/3.0-medium-first-pass.md`, `docs/games-scan-stack-fix.md`,
`docs/wince-cdda-copy-test.md`, the accepted `1734830d08df` module bytes, and
the current **K-UI-Test-Suite-Audit-2026-11-04.txt**. Historical working modules
establish their own behavior; they are not a replacement for the complete
current project state.
