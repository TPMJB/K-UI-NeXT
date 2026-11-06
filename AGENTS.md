# K-UI work and release requirements

The current project goal is one maintained, broadly compatible launcher build
which incorporates the lessons already established across the readers and VMU
implementations. Follow `docs/universal-loader-roadmap.md` for integration order
and remaining coverage. Optional VMU and read-ahead depend on each runtime's
proved capabilities; preserve working demand-only and exact-adapter routes
while extending shared support.

Read `docs/accepted-progress.md` before selecting a build recipe, assembling an
update, or requesting console tests. It records component-specific acceptance,
preservation requirements and unresolved integration work. A newer commit or a
successful build does not establish that the selected configuration preserves
accepted behavior.

The current bootstrap test and its five native service cohorts are recorded
in `docs/shared-native-bootstrap-rollout.md`. Default Standard/native-CDDA
bindings are exact copies of its qualified GUI inputs. Preserve them in later
GUI builds for this installation. An explicitly older-cohort GUI must select
its saved manifest or supply the matching new services; do not silently pair
a new GUI allowlist with an inherited incompatible Standard module.

Before compiling, identify the accepted source/configuration, the intended
behavior change, affected module roles, and the decision a new test answers.
Check actual GUI selection and module presence/absence. Preserve accepted
features and source outside that change; never substitute a convenient older
build to make a package complete.

Updater-only changes preserve reader implementations, recipes and memory
geometry. Current v2 partial packages retain effective installed software;
intended reader changes must be explicit staged replacements. Never recover
historical adapters or add optional loaders merely to complete an updater
package. Checked replacements must not fall back to an older physical root.

Keep VMU-only, prefetch-only, combined, and neither as separate requirements.
Disabling a service does not prove its RAM reservation was released. Native,
disc, CDDA, and WinCE routes have distinct contracts; support on one does not
establish support on the others. Label unresolved behavior explicitly.

Do not repeat settled compatibility matrices for an unrelated GUI/updater
change. Use production routing, filesystem and package checks for preservation;
request console tests only for changed behavior and observable new questions.

Startup performance is a shared loader requirement. Batch consecutive reads
and keep every SD block CRC check by default across supported launch families;
do not make the same optimization a new title-specific opt-in. Every installed
loader embeds its stage, so audit actual package and GUI-route coverage when
changing shared cold code. A source-only fix or an eight-file subset does not
establish a global installed rollout. See `docs/global-launch-policy.md`.
Carry its shared lifecycle and delivery requirements into every integration;
do not rediscover batching, card-admission cost, return ownership or package
routing after another title reports the same failure. The existing gates are
in `docs/shared-loader-performance-policy.md`, `docs/loader-integration-baseline.md`
and `docs/shared-loader-vmu-regressions.md`.

Before releasing a new GUI, run its production admission path against the
actual packaged module bytes, including collection children and helper owners.
Compiler literal pools can contain record magic; synthetic record fixtures do
not establish that a real linked module is discoverable. Attribute launch delay
to its measured phase: a detached-stage improvement cannot establish a fix for
time spent in GUI preparation before the bootstrap appears.

## Current Main updater delivery

The updater production sources match Main2b4b8c673b7c (engine3), checked2026-11-05. Reader and
gameplay changes require their own explicit scope; updater integration must not
replace them or change accepted worker geometry.

## Required delivery path

Every NEW test/release update must use the current Main producer:

    python3 tools/make_update.py staged/KUI test.kup
    python3 tools/make_update.py staged/KUI release.kup --full --channel stable

The normal tools/update_package.py build command also creates v2. Its explicit
build-legacy command is only for engine bootstrap/migration fixtures. Never
qualify a new delivery with a frozen old decoder or a Python-only verifier.
The producer compiles the actual current C consumer and validates the exact
output before publishing it. Keep its source fingerprint/report with evidence.
Deliver the standalone .kup by default, unless the owner explicitly requests a
manual-copy test ZIP as recorded in docs/accepted-progress.md. An optional KUP ZIP has exactly one .kup;
source/evidence archives are separate from the install payload.

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
The one-time bootstrap updates BOTH KUI/runtime.kui and KUI/boot.kui and selects
the fixed root, preserving bank folders and user data. A root-only replacement
does not update an already selected GUI's compiled updater implementation.
Use the actual running fallback for partial overlays; stale saved selection must not
prevent a valid root-based test. Preserve old banks and user files during
engine setup; do not silently install optional workers. Future tests then use
only .kup selection and Restart K-UI. Restart uses worker 126; update uses 120.

Read-only software open/stat wrappers are GUI-only. Writes and all user data
bypass them; raw pinned reads are verified before use. Root setup/verification
uses physical bypass helpers, never the currently selected bank alias.

Run focused v1/v2 codec, producer/current-consumer, snapshot/power-cut,
ZIP/folder, boot/handoff, software-wrapper, shell and native GUI checks.
Keep the header-only normal-root boot path. Do not repeat settled game
matrices for an updater-only change. Source/host checks are not console tests.

## Main integration and updater source preservation

Verify the current remote Main head before integration or delivery, and preserve
its unrelated changes. The owner explicitly authorized publishing the current
reader, VMU and Smash sources for review; the older updater-only prohibition on
importing game sources does not apply to this integration. This authorization
adds no gameplay repair or change to accepted worker geometry.

Keep the exact Standard/CDDA GUI admission manifests and build configuration.
Preserve the optional embedded-module provider and intended bundle; it may serve
root/inherited reads, never a pinned bank replacement. Run the actual
update_work/production-filesystem flow and the real settings APIs when changing
updater integration.

For the one-time manual bootstrap use the current implementation:

    python3 tools/make_updater_repair.py staged/KUI repair.zip --kup-output test.kup

It generates identical physical boot.kui/runtime.kui, ROOT selection records,
and one embedded KUP identical to the standalone output. Historical repair,
switch, add-on and test-bundle CLIs require --legacy-fixture and are not ordinary
delivery tools.

Flat app RAW software is positively owned by the '*-helper.bin' contract.
Launch settings, selection records and generated '*-session.bin' files remain
shared data even beside loaders; packages must reject them. The frozen
`tests/fixtures/updater-engine2` sources are deliberately broken negative
witnesses only, never production decoders, validators, packagers or build inputs.

Retain `update_addon.check_sources` and the historical517 source record unchanged.
`make test-updater-source-preservation` is the explicit historical GUI-only
add-on preservation check. The generic `test-updater` target exercises current
functional updater behavior during this authorized reader integration; it must
not silently rewrite the historical record or weaken the guard.
