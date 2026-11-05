# K-UI updater integration rules

Read docs/accepted-progress.md before selecting a build or delivering an update.
This work extends only the updater from published Main commit 758f62c. Never import a
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

Use the actual staged software inventory, not a compiled count/catalog.
Changed .kui components retain their real embedded builds; do not retag them
or rebuild settled readers merely to obtain one cohort label. Raw helpers,
release audio/assets and catalogues are typed and pinned by full SHA. Stage
only intended software; never cards, saves, preferences, credentials or games.
A GUI replacement must contain the current engine capability and matching
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

V2 requires a persistent engine2 root dispatcher. It refuses engine downgrades
and skips old incompatible selected GUIs after that one-time setup. Use the
actual running fallback for partial overlays; stale saved selection must not
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
