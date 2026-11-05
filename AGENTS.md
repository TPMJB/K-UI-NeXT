# K-UI updater integration rules

Read docs/accepted-progress.md before selecting a build or delivering an update.
Start from the current remote Main head, verified before editing or delivery.
This work extends only its updater. Never import a
recovered reader/VMU/Smash branch to obtain updater functionality. Reader/stage
sources, reader recipes, layouts and game-selection policy remain unchanged.
The preserved-main source guard and exact Standard/CDDA GUI bindings still apply.

## Required delivery path

Every NEW test/release update must use the current Main producer:

    python3 tools/make_update.py staged/KUI test.kup
    python3 tools/make_update.py staged/KUI release.kup --full --channel stable

The normal tools/update_package.py build command also creates v2. Its explicit
build-legacy command is only for engine bootstrap/migration fixtures. Never
qualify a new delivery with a frozen old decoder or a Python-only verifier.
The producer compiles the actual current C consumer and validates the exact
output before publishing it. Keep its source fingerprint/report with evidence.
Deliver the standalone .kup by default. An optional ZIP has exactly one .kup;
source/evidence archives are separate from the install payload.

For the one-time manual bootstrap use the same current implementation:

    python3 tools/make_updater_repair.py staged/KUI repair.zip --kup-output test.kup

This generates both identical physical GUIs, ROOT selection records and exactly
one embedded KUP identical to the standalone output. Historical repair, switch,
addon and test-bundle CLIs require --legacy-fixture and are not delivery tools.

Use the actual staged software inventory, not a compiled count/catalog.
Changed .kui components retain their real embedded builds; do not retag them
or rebuild settled readers merely to obtain one cohort label. Raw helpers,
release audio/assets and catalogues are typed and pinned by full SHA. Stage
only intended software; never cards, saves, preferences, credentials or games.
A GUI replacement must contain engine revision3 and matching
reader admission. Reader-only tests preserve the effective GUI automatically.
Do not disable game ABI/admission guards to force a package to run.

## Update semantics

Partial updates retain the effective running software, including checked
bank replacements and deliberate absence. They never revert omitted bank
components to an older physical root. FULL packages supply their GUI and
complete intended software; omitted software stays absent. User data remains
shared and is never part of a software package. Explicit REMOVE is for
software paths only. Package/snapshot identities are independent of GUI and
reader build IDs and include flags/configuration identity.

Wire v2 deliveries require flag2 and engine revision3. Engine2 GUIs have a
confirmed shared-data routing defect and must not be selected by revision3.
The bootstrap must update BOTH KUI/runtime.kui and KUI/boot.kui and select the
fixed root, preserving bank folders and user data. A root-only replacement
does not update an already selected GUI's compiled updater implementation.
Use the
actual running fallback for partial overlays; stale saved selection must not
prevent a valid root-based test. Preserve old banks and user files during
engine setup; do not silently install optional workers. Future tests then use
only .kup selection and Restart K-UI. Restart uses worker 126; update uses 120.

Read-only software open/stat wrappers are GUI-only. Writes and all user data
bypass them; raw pinned reads are verified before use. Root setup/verification
uses physical bypass helpers, never the currently selected bank alias.
Flat app RAW software is positively owned by the '*-helper.bin' contract.
Launch settings, selection records and generated '*-session.bin' files are
shared data even beside loaders; packages must reject them.

Run focused v1/v2 codec, producer/current-consumer, snapshot/power-cut,
ZIP/folder, boot/handoff, software-wrapper, shell and native GUI checks.
Keep the header-only normal-root boot path. Do not repeat settled game
matrices for an updater-only change. Source/host checks are not console tests.
Run the actual update_work/production-filesystem flow and real settings APIs.
When adopting the updater into an accepted GUI, preserve its original build
configuration and embedded module bundle as well as its supplied software.
Reproduce that baseline before interpreting binary size changes. The optional
embedded-module provider may serve root/inherited reads, never a pinned bank
replacement. Do not import the accepted GUI's game sources into Main.
tests/fixtures/updater-engine2 is a deliberately broken negative witness only;
never use it as a production decoder, validator, packager or build source.
