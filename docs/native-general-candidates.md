<!-- SPDX-License-Identifier: GPL-3.0-only -->
# General split controller and structural New reader

Two separately built candidates extend the accepted October pilots to other
games. Neither replaces an accepted module, and neither has been built for or
tested on the console yet.

| Candidate | Makefile | Installs as | Extends |
| --- | --- | --- | --- |
| General split controller | `Makefile.vmu-native-isolation-split-controller-general` | `KUI/apps/games/retail-native-dual.kui` (New/Legacy) | Evolution 1 split `6be76940ccc9` |
| Structural New reader | `Makefile.native-standalone-general` | `KUI/apps/games/retail-native-standalone.kui` (new file) | DOA2 standalone `921318c3d9cb` |

## General split controller

`6be76940ccc9` refuses every game except Evolution 1: the stage stops with
**SPLIT CONTROLLER NEEDS EVOLUTION 1**, and the worker refuses to arm.
With it installed, Rayman 2, Lodoss, SA2 and the other combined games stop
on that screen. Its capture and completion use only discovered driver and
layout roles, and its SDK manager checks match the general fast path that
passed in Rayman 2 and Lodoss on `f158ba5e24f9`.

`KUI_NATIVE_SPLIT_CONTROLLER_GENERAL` removes only those two identity checks.
NORMAL mode, lifetime, layout, packet, response and storage checks remain.
The stage banner reads **SPLIT CONTROLLER ALL**. Like `6be7`, it keeps
prefetch and controller TICK off.

## Structural New reader

The DOA2 pilot admits one executable by SHA-256, binds five DOA2 constants,
and replaces the ordinary `retail-boot.kui`, so **Background Off** stops for
every other game while it is installed. The structural build:

- finds the SDK SCI interrupt family in the loaded executable with
  `kui_native_sdk_sci_family_find()`, the image-only half of the existing
  structural route discovery;
- publishes the roles it finds; the binder takes the game's live VBR
  (256-byte aligned, at or above `0x8c008000`) instead of DOA2's;
- has no virtual VMU and no game-RAM reservation, like the DOA2 pilot;
- carries its own capability record and accepts only an SCI **New (X)**
  launch, so it is installed beside the existing modules.

The launcher uses it only when all of these hold: the launch is **New (X)**;
the combined module's complete executable proof refuses; the executable has
the SDK SCI family; and the module is installed. Any other launch keeps its
existing module. Without the module, the combined module reports its refusal
as before. The launch report records **Background New (X), no virtual VMU**
and the standalone package path.

In the 26-executable corpus, 18 have the SDK SCI family and 8 pass the
combined module. The refusals of DOA2, Frogger 2, Rush 2049, Crazy Taxi 2,
Omikron, PSO, Sega Smash Pack and Jet Grind Radio happen in the static proof
the launcher repeats, so they would fall back. Code Veronica's 384 KiB
placement refusal happens later, in the stage, and does not.

## What protects the accepted modules

- Every compile command of the DOA2, split and dual builds preprocesses to
  identical output before and after these changes, so those binaries are
  unchanged. The shared SDK family tables moved into
  `src/core/native_sdk_sci_family.inc`; `native_sdk_sci_route.c` still
  preprocesses byte for byte as before.
- The new modules are separate builds. The standalone module is a new
  file; removing it restores the previous behavior.
- The accepted ZIPs remain the rollback.

## Host evidence so far

- Split controller: the exact build also refuses a non-Evolution identity
  (6382 checks). The general build runs the whole suite under both
  identities and refuses every diagnostic mode (12974 checks). ASan/UBSan.
- Structural reader: launch, capability and finder input (69808 checks);
  binder and demand suite at another VBR with compact/U0 roles (12187 UBSan,
  10468 ASan); the DOA2 suite is unchanged (11844/10468).
- Launcher: 23 New/Legacy preparation cases on FAT32 and exFAT, including
  five standalone routing cases; native cache preflight (38961 checks).
- All C units of both new builds pass ILP32 type checks with their real
  flags. This is not an SH-4 build: layout, stack and instruction audits
  and the delivered-binary comparison still need the pinned toolchain.

The finder's positive cases need original executables, which are never
committed. Run `tests/test_native_sdk_sci_route_discovery.c` with them; it
now compares the finder with every live admission.

`build/test-armada-ce-excerpt-prepare` already fails to link on the imported
tree (`f_unlink`/`f_rename` undefined); this is unrelated to these changes.

## Proposed console checks

Each check answers one question. Nothing here repeats an accepted result.

1. **General split, Rayman 2:** load a save, write a new one, power off, reload.
   This is the first time the split path runs outside Evolution 1, and saves
   use the full storage path beside it.
2. **General split, Lodoss:** reach gameplay. This checks the graphics-pool
   correction together with the split path.
3. **General split, Evolution 1:** boot once. Only the identity check changed,
   so this is a short regression check.
4. **Structural reader, DOA2 with New (X):** play a fight. The binder is now
   general, and DOA2 reaches it through the new launcher route.
5. **Structural reader, two new SDK games** (for example Crazy Taxi 2 and
   Jet Grind Radio): boot, an FMV and a few minutes of play.
