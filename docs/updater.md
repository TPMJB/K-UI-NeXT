# K-UI software updates

The normal workflow is: receive a test .kup, select it in Settings -> Updates,
install, Restart K-UI, then run the test. Updates may be placed in subfolders
such as KUI/updates/test. A ZIP with exactly one embedded .kup also works.
The producer checks the actual output with the current Main C decoder before
it is delivered. The user should not repair manifests or choose a decoder.

## Tests and complete versions

| Package | Meaning |
| --- | --- |
| Partial/test | Replace only declared software; keep the effective running GUI, readers, helpers, assets and absence for everything else |
| FULL/version | Install the complete declared software set and its GUI; omitted software stays absent rather than leaking older root modules |
| REMOVE entry | Deliberately hide a software path in this version; never delete user data |

Package identity is based on content/configuration. Each .kui keeps its real
embedded build and exact SHA/CRC; raw software keeps its exact SHA. Different
tests built from one source commit can coexist without same-build collisions.
There is no requirement that all components share one commit or catalog count.
Unchanged accepted readers do not need rebuilding to fit a package label.
Game ABI/admission remains checked: a reader experiment needing new admission
must include a compatible current-engine GUI, prepared by its developer.

A partial update snapshots the actual running version, including a checked
fallback. Omitted components never silently revert from an installed bank to
physical root. Preserved files are verified before copying. New replacements
can repair corrupt overwritten components; FULL does not depend on a broken
old bank receipt. Installation writes/rereads an unselected bank and publishes
its checked receipt and activation last. Failed or stopped installs leave the
prior selected software available. Source/host fault tests do not certify
filesystem metadata against every real power loss.

Save data, cards, journals, preferences, credentials and games are outside the
software namespace. They stay shared; rolling back software does not roll
back saves. Root writes from ordinary apps are not redirected into banks.

## Current engine setup

Engine2 adds typed components and snapshot receipts that engine1 cannot read.
The physical KUI/runtime.kui dispatcher must be engine2 once; merely selecting
a newer bank leaves the older dispatcher in charge at cold boot. Retain a
backup of the working root and use the supplied small engine setup delivery.
Keep every existing reader and user file. If an old KUI/boot.kui override is
present, preserve/rename it so the boot CD reaches the updated root.

Engine2 skips selected GUI banks with an older updater. A prior bank is not
deleted; the capable root runs and a FULL test can establish the new selected
software set. The setup preserves current GUI features and already verified
reader bytes. It does not require individual test folders to be recopied.
After setup, ordinary tests and complete version updates use .kup only.

The earlier literal 'Incomplete update cohort' comes from older exact26/34
consumers. The failed136 delivery declared34 rows but replaced only its GUI;
all33 other rows inherited root while its evidence ZIP contained26 modules
and helpers. That was not a self-contained software update. The v2 producer
inventories the actual staged software, includes its bytes, rejects an old
GUI before producing output, and validates through the current consumer.

## Build one update

Use a checkout containing the current Main updater. Stage intended software
under a KUI directory. For a test changing only one reader, stage that reader;
the effective GUI and other software will be retained automatically.

    python3 tools/make_update.py stage/KUI test.kup
    make update KUI_UPDATE_STAGE=stage/KUI KUI_UPDATE_OUTPUT=test.kup

For a complete software version, stage its current-engine GUI plus its entire
intended software set. The version displayed by the GUI comes from that
release's actual source; the updater does not invent a 2.8.5 build or retag old
binaries as new software.

    python3 tools/make_update.py release/KUI version.kup --full --channel stable
    make update-release KUI_UPDATE_STAGE=release/KUI KUI_UPDATE_OUTPUT=version.kup

The central producer can also convert a software ZIP. It reads every software
member, ignores source/evidence outside KUI and reports replaced embedded
update artifacts. Old GUI code must first receive the current updater source;
renaming the ZIP or .kup cannot upgrade its embedded engine.

Common GUI/disc test builders emit a standalone sibling .kup and the same
payload inside their evidence ZIP. The full release pipeline publishes a
standalone .kup with checksums and checks it against the installation ZIP.
Historical standalone packagers are not automatically trusted: route new
deliveries through make_update. The explicit build-legacy CLI exists only for
bootstrap and migration tests, not ordinary test/release deliveries.

## Supported software

Typed .kui and raw .bin modules may live under apps/games, apps/disc and
apps/music. Root runtime/tools/recovery images, official music .ogg/.wav,
probe.dat, redump.db/tosec.db and bounded immutable assets are supported.
No traversal, symlinks, arbitrary KUI data or user files are allowed. The
producer rejects unsupported staging contents before an artifact is delivered.
V2 supports up to 256 software entries and 128 MiB payload; resource bounds have
specific errors, independent of historical catalog counts.

## Integrity and startup

Declared replacements never fall back to root when missing or corrupt. Raw
helpers/assets are checked through read-only GUI software routing. .kui SHA
verification shares the load stream. Full snapshots hide omitted software;
partial snapshots retain that mode. Checked handoff binds GUI identity,
selection sequence and manifest independently of the snapshot ID. Equal GUI
build labels with different bytes/configuration still execute the right image.
The ordinary empty root startup retains its 64-byte header fast path.

The startup selection screen covers storage/selection work and has no timed
pause; package integrity and copying still take time on the selected adapter.
Restart K-UI reloads directly. Returning from a game remains a separate loader
feature; this updater does not add universal CDDA/WinCE in-game return.
