<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Tyrfing integration record

Preparation date: 2026-10-15. Version **2.0-rc10**, release name **Tyrfing**.
This prepares the next release without publishing a final 2.0 tag.

## Selected source

| Source | Disposition |
| --- | --- |
| `sci-armada-cdda` through `359918874090423e89764af15b0a8cbf0120a819` | Integrate 35 commits beyond `main` (`e4c521d`), including the tested `d9f861b` word-copy improvement, bounded worker, saved per-image launch choices and the three exact US WinCE CD music profiles. Historical diagnostic tools are source records, not enabled release modes. |
| `milestone/wifi-performance-test`: `a66186c`, `3f44de8`, `fbbc3df`, `f430f26` | Integrate the four missing wireless software commits, preserving current reader and Games behavior. The final wireless timing still needs console acceptance. |
| `codex/home-icons`: `c91820c`, `10cd1bb` | Integrate the seven icon pairs and television refinement. Preserve current Wi-Fi text, version text and preview cases. |
| FTP close-storm, 2048 converter, original C5 support, PRs #2/#7/#8 | Already in `main` or the selected source ancestry; no duplicate code merge. |
| New G1 SD adapter board branches and older mixed hardware/software branches | Excluded at the owner's request. Existing PIO code does not certify new G1 hardware. |
| Toy scheduling experiments and older validation-link-only commits | Excluded from promotion. No native CDDA module or Toy FMV fix is claimed. |

The ordinary build now compiles and packages the separate Armada, Bust-A-Move 4
and Worms modules alongside the ordinary CE fallback. Each module must pass its
linked profile, memory, stack, bootstrap, blank-map and source-identity audits.
The CE fallback retains its tested DMA-resume correction and original placement;
native loader placement remains independently selected. CD music remains opt-in.

The wireless merge originally reused worker action 76, already occupied by
saving launch settings. The integration preserves 76 for that action and uses
77/78 for adapter-update checks and installation. Wi-Fi dispatch explicitly
excludes launch-setting saves.

## Restored source completeness

The remote CDDA branch referenced an absent `src/core/games_cdda_profile.h`.
It was recovered byte-for-byte from the delivered, hardware-tested
`K-UI-2.0-WinCE-CDDA-RAM-Copy-Test.zip`, member
`source/K-UI-WinCE-CDDA-RAM-Copy-1aa16821209b.tar.gz`.

- Reference ZIP SHA-256: `48feff2cfd3d8dabe04e9a47b89d6379e432b360148b94032340a50249b962b6`.
- Header SHA-256: `3e6f1c14f1b126c85e608ac178b7a63715e2735614e18ae1910ca588b05e8714`.

The two absent launch-settings test sources were replaced with independent
codec and fault-injection tests covering persisted-slot integrity and recovery.
Audio regressions now run under the ordinary host test gate. A stale Worms
test recipe was corrected to link the actual native implementation with its
matching profile definition.

## Feature comparison

See the [Games audit](audit-legacy-games.md) and [GD Ripper audit](audit-legacy-ripper.md).
They compare the adapted project's reachable UI with this independent code.
No DreamShell implementation was copied. Suggested convenience features and
larger runtime gaps remain recommendations, not new release features.

Final acceptance requires the complete assembled candidate's build/package
checks and console confirmation. Per-feature hardware reports remain scoped to
their original binaries; a fresh integration is not automatically hardware-tested.
