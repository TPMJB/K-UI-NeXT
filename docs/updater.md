# K-UI software updates

Use the current Main producer and current updater implementation. The intended
workflow is a prepared .kup in KUI/updates/test, select/check/install it in
Settings -> Updates, then Restart K-UI. Folder browsing and a ZIP containing
exactly one .kup are supported. The producer validates the exact output with
its current C consumer before delivery; users should not edit manifests.

## Confirmed failures and revision3

Engine2 incorrectly classified every flat apps/*.bin file as immutable code.
FULL omission therefore hid physical launch-settings and selection slots and
runtime-generated VMU sessions, while writes still reached root. Actual app
services reproduced failed saves and overwritten preset collections. This was
a functional routing defect, not an SD-card or package-count problem.

A root-only updater replacement was also insufficient: the dispatcher could
chain into an existing GUI2 bank and run that GUI's older wrappers again. Prior
host tests loaded old SH bytes as data but then simulated the next process with
current C functions. They did not establish the recipient's actual behavior.

Revision3 fixes ownership and binds new deliveries to that behavior. Wire
format remains v2; bit0 is FULL and bit1 requires engine revision3. New packages
set bit1, so older engine2 decoders reject them before writes. GUI replacements
must carry the revision3 capability. A revision3 dispatcher refuses older GUI
implementations and legacy receipts before handing off. Legacy formats remain
parseable for migration and negative tests, not as ordinary new deliveries.

The repair bootstrap updates both KUI/boot.kui and KUI/runtime.kui with identical
fixed GUI bytes and selects that copied root. It preserves bank directories,
physical readers, cards, saves, preferences and games. This is necessary once;
a selected old GUI must not immediately undo the repair. The working VC2 game
source and accepted module/helper bytes are preserved separately from the
updater-only Main merge. The bootstrap is a manual-copy repair, while subsequent
prepared tests and version updates use .kup installation and Restart K-UI.

## Software ownership and user data

| Path | Ownership |
| --- | --- |
| runtime/tools/recovery.kui and flat apps/{games,disc,music}/*.kui | Immutable software |
| Flat apps/*/*-helper.bin with a nonempty role prefix | Immutable RAW helper |
| apps/games/probe.dat, redump.db, tosec.db | Immutable software data |
| Official apps/music/*.ogg/*.wav and bounded assets/ trees | Immutable software assets |
| launch-settings-{a,b}.bin, selection-{a,b}.bin, generated *-session.bin | Shared mutable app data |
| Cards, journals, preferences, caches, games and reports | Shared user/runtime data |

An arbitrary .bin extension is not software ownership. Mutable/generated app
records bypass software routing in ROOT, partial BANK, FULL BANK and blocked
code states. They are forbidden in package admission. FULL absence and explicit
REMOVE apply only to immutable-owned software. Writes remain physical; read
and stat routing cannot hide the records that those writes just created.

## Partial tests and complete versions

| Delivery | Result |
| --- | --- |
| Partial/test | Overlay declared software onto the actual effective running version; retain its GUI, readers, helpers, assets and deliberate absence |
| FULL/version | Supply the complete intended software set and current GUI; omit obsolete software without leaking older root modules |
| REMOVE | Preserve absence of a software path; never delete shared app/user data |

Each .kui keeps its actual embedded build and exact CRC/SHA. Package and
snapshot identities describe applied content/configuration independently of
component build IDs. Settled readers are not rebuilt or retagged to share one
cohort. Game ABI/admission remains checked; a reader experiment that needs new
admission must ship a compatible current-updater GUI prepared by its developer.

Installation verifies the package, prepares an unselected bank, rereads software,
then publishes its checked receipt and activation. Partial inheritance uses the
actual running checked root/bank, not a stale pending or rejected saved choice.
A FULL replacement does not depend on broken prior payloads or receipts.
A corrupt same-content complete bank is retained; FULL repair uses a separate
verified bank instance and publishes only after it is complete. Existing or
referenced banks are never rewritten in place. Space and repair targets are
rechecked at installation, not assumed from the earlier preview.

A positively checked running root can recover malformed activation records
through normal reviewed installation or Restore Baseline. Listing stays
read-only. I/O failures and unknown/blocked runtime state do not authorize a
metadata reset. CRC-valid contradictory bank choices remain protected when
there is no safe authoritative root slot; manual repair is required for that
ambiguous history. Restore checks receipt requirements and actual GUI capability
before confirmation, so it cannot report success then silently boot an older
incompatible updater. Power-cut tests model write/sync/cancel boundaries; they
do not certify every filesystem metadata failure on real hardware.

## Produce a delivery

Stage intended software only, never a whole live SD-card KUI directory.

    python3 tools/make_update.py stage/KUI test.kup
    make update KUI_UPDATE_STAGE=stage/KUI KUI_UPDATE_OUTPUT=test.kup

For a complete software release, stage its actual release GUI and every intended
software file. This does not invent a 2.8.5 release or version-label old binaries.

    python3 tools/make_update.py release/KUI version.kup --full --channel stable
    make update-release KUI_UPDATE_STAGE=release/KUI KUI_UPDATE_OUTPUT=version.kup

The current producer inventories staged software, supports independent builds
and raw helpers, compiles the current C consumer, and checks final bytes before
atomic output publication. Unsupported staging paths, obsolete GUIs, traversal,
symlinks and user data fail on the developer's machine before delivery. Ordinary
packages support 256 software entries and 128 MiB payload. Full release assets
are compared byte-for-byte with the installation ZIP's software inventory.
Common test packagers emit an identical standalone .kup and embedded ZIP payload.
Historical custom packagers must use make_update for new deliveries. Their old
CLI paths require --legacy-fixture; they cannot silently produce another
root-only or obsolete-engine repair.

For the one-time manual bootstrap, stage the accepted GUI with the current
updater and its intended immutable software, then run:

    python3 tools/make_updater_repair.py stage/KUI repair.zip --kup-output test.kup

The builder creates identical boot.kui/runtime.kui, both ROOT selectors, and
one embedded KUP identical to the standalone output. It validates the actual
activation bytes and KUP with current production C, records source/file hashes,
and refuses existing outputs. Each file publishes atomically; an ordinary
failure rolls back newly published outputs. Publication of two sibling files
is not a single filesystem transaction across power loss; the ZIP is complete
on its own. These checks do not execute the SH GUI.

## Regression evidence and startup

The real GUI update_work is exercised with production FatFs, installer, boot,
producer and unchanged launch-settings/selection services. Tests cover corrupt
selectors, stale confirmation, actual resulting snapshot identity, partial A/B
switching, shared presets, incompatible Restore and same-content FULL repair.

The frozen tests/fixtures/updater-engine2 source is a deliberately broken
negative witness. A separate program compiles that actual older C; another
compiles current C. A serialized dispatcher-to-old-GUI handoff reproduces the
recipient's old read/save failure. Production revision3 blocks that transfer.
The frozen source is never a package validator or production build input.
SH payloads are verified as bytes; host checks do not execute Dreamcast code or
replace console testing.

The accepted338 GUI was reproduced byte-for-byte with the current SDK before
adoption: its exact four-module compressed bundle, both original admission
manifests and KUI_SMASH_LAUNCH_TIMING=1 are required. A rebuild missing those
options is not the accepted baseline. The optional embedded-module provider
remains available for root/inherited reads; pinned bank replacements bypass it
so embedded old modules cannot shadow an installed test. The supplied game and
helper binaries remain byte-identical; Main receives no game-source merge.

There is no separate version-selection status page. Selection still performs
necessary storage and integrity work before the normal launcher splash. It
reuses the pinned transport, avoids duplicate rejected GUI reads, uses equivalent
fast updater CRC, and reuses a checked handoff-slot scan. Startup timing reports
remain available. A valid current GUI handoff adopts through receipt/header/slot
reads; it does not reread the complete GUI. Returning from a game remains a
separate route-specific loader feature.
