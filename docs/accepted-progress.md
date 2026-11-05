# Updater-only integration and preserved baseline

The source baseline is published main
`0045dd96937ee7485ecd4fe0432b28babea8646a`. This integration ports the updater
without merging the recovered ab1 loader/VMU/Smash development. The owner's
2026-11-04 instruction explicitly limits this merge to updater code.

| Area | Included change | Preservation boundary |
| --- | --- | --- |
| Installed versions | Checked banks, selection records, restart and rollback | Existing root readers and their absence remain shared |
| Package admission | Bounded namespace, sparse inheritance, old26/old34 compatibility | No exact-count gate and no new optional modules |
| Test selection | Folder browsing and one embedded .kup per ZIP | Legacy manual-copy ZIPs require conversion |
| GUI restart | Fully verify/reload selected GUI directly | Updates action120; restart action126 |
| Module paths | Three bank-aware existence checks in the existing launcher | No title-specific routing or new backend selection |
| Reader admission | Exact verified Standard/CDDA identities in the GUI | No reader code, build flags, memory geometry or binary changes |
| Root migration | Preserve system/optional boot override, then copy runtime | No timing-sensitive button required; all user files retained |

`docs/evidence/updater-preserved-main-source.json` records main's protected
source blobs and the four deliberately reviewed GUI glue paths. All other
protected sources and reader recipes must match main byte for byte. The four
binding files under resources/updater-bindings retain exact admission for the
already installed 233,964-byte Standard reader and native CDDA aliases.
These manifests are not permission to replace readers or infer an allowlist
from arbitrary SD files.

The owner confirmed that the earlier root repair GUI06cbd036bf0d booted and
resolved the reported cohort error. That binary remains their working repair;
this source-only main integration is a separate narrow port. No replacement
reader or new console test matrix is required. The owner reported a longer
than desired installed-version selection screen; this merge does not claim
to resolve its latency. A completed console CDDA-package round trip and
universal in-game return are not established by the root-boot confirmation.

The failed full-cohort updater substituted a wrong Standard reader and added
presence-sensitive modules. Its release/build recipes are not included here.
GUI-only packages and the manual root repair contain no KUI/apps files.
Readers, VMUs, journals, game metadata and settings are left in place.

The accepted compact 72-KiB VMU and combined284-KiB/cache192-KiB installations
remain distinct from VMU-only operation. This updater does not change any
service reservation, promote session-controls variants or add a universal
cacheless profile. Native/disc/CDDA/WinCE support remains route-specific.

## Robust updater v2 follow-up

The next updater-only change fixes the contract rather than adding another
catalog exception. The source remains the published-main updater; no reader,
stage, memory geometry or reader recipe is rebuilt or merged here.

Partial packages materialize a checked snapshot of the actual running ROOT or
BANK plus intended changes. FULL packages are independent of the prior bank,
can replace corrupt prior files/receipts, and preserve absence for omitted
software. Component builds are independent; configuration/content determines
package and snapshot identities. Raw helpers, official audio/assets and data
catalogues are supported. The current C consumer preflights every generated
KUP before delivery; default test/release producer paths create v2.

The earlier v2 delivery attempted a Root engine2 setup and a136 GUI adoption
with its26 original module/helper binaries. That delivery is superseded by the
functional revision3 repair below; do not use its engine2 producer/bootstrap
for new packages. The current accepted game baseline is working338 and its six
supplied software files. A GUI adoption is a test deliverable, not a merge of
its game code into Main.

## Startup latency follow-up

The owner reported about five seconds at the pre-splash version-selection
screen. There is no fixed delay in that function. Current code could reread
one refused multi-MiB bank GUI for both activation slots, rescan the EMPTY
slot after validation, and remount an already pinned boot device for discovery.
The updater-only follow-up deduplicates refused GUI sources after each slot's
handoff attempt, reuses the checked slot offset, uses the existing equivalent
fast CRC only in updater paths, and opens the pinned transport directly.
KUI/updater-startup.txt records phase timings so another video is unnecessary.
Cold selection still reads and verifies a distinct bank GUI once; no console
timing improvement is asserted from host read-budget or integrity tests.

## Functional repair after the failed root-only delivery

The owner reports the startup ZIP did not fix the issue, while the manual VC2
338aaa6d6456 package did. Comparing actual artifacts established that the manual
package updates boot.kui AND runtime.kui, selects ROOT through both activation
records, and fixes shared app-data classification. The root-only 9b88 delivery
could chain back into an older GUI2 implementation; its host model did not
execute that recipient's C code. Earlier host checks were insufficient evidence
that the fix would run on the console.

Actual unchanged launch-settings/selection services reproduced hidden physical
files, failed save verification and overwritten presets under engine2 FULL
routing. Revision3 positively owns immutable helper binaries, leaves mutable
and generated records shared in every resolver state, rejects those records
from packages, and prevents old updater semantics from being selected again.
New wire-v2 deliveries carry the engine3-required flag, independent of historical
catalog counts. Tests now execute frozen old C and current C in separate host
programs and exercise actual GUI update_work with the production filesystem.

Installer repair also covers malformed selectors from checked ROOT, safe
same-content FULL repair into a separate bank, and updater compatibility at
Restore review. Ambiguous CRC-valid contradictory bank histories fail closed
rather than risking an unexpected selection during a torn recovery write.
The working338 source and six supplied game/helper binaries remain the game
baseline for the repair deliverable; their code is not merged into Main.

The original338 GUI was also reproduced byte-for-byte from its frozen clean
source with the current SDK: 3,491,912 bytes, SHA256
ea4adc4886a6e8eb698e46f5df96a840061452f9754f4ff5885707e67c4d47d8.
Its exact original compressed four-module bundle, both original admission
manifests and KUI_SMASH_LAUNCH_TIMING=1 are part of that accepted build.
An initial adoption rebuild omitted the bundle and timing flag; it is blocked
from delivery. The final adoption preserves those options and the optional
root/inherited bundle provider. Pinned bank replacements bypass that provider.
This generic GUI compatibility hook changes no reader or game source on Main.
The canonical make_updater_repair builder supplies both GUI paths, generated
ROOT selectors and identical embedded/standalone current KUPs. Historical CLI
repair/switch/addon/bundle paths require an explicit negative-fixture flag.
