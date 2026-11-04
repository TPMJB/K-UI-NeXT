# K-UI work and release requirements

Read `docs/accepted-progress.md` before selecting a build recipe, assembling an
update, or requesting console tests. It records component-specific acceptance,
preservation requirements and unresolved integration work. A newer commit or a
successful build does not establish that the selected configuration preserves
accepted behavior.

Before compiling, identify the accepted source/configuration, the intended
behavior change, affected module roles, and the decision a new test answers.
Check actual GUI selection and module presence/absence. Preserve accepted
features and source outside that change; never substitute a convenient older
build to make a package complete.

The updater add-on installs the GUI and explicitly inherits existing root
readers. Its delivery must contain no `KUI/apps/` files. Do not rebuild readers,
recover historical adapters, or add optional loaders for an updater-only task.
Replacement bank members remain checked and never fall back to root. Inherited
members deliberately share the installed root and its normal validation.

Keep VMU-only, prefetch-only, combined, and neither as separate requirements.
Disabling a service does not prove its RAM reservation was released. Native,
disc, CDDA, and WinCE routes have distinct contracts; support on one does not
establish support on the others. Label unresolved behavior explicitly.

Do not repeat settled compatibility matrices for an unrelated GUI/updater
change. Use production routing, filesystem and package checks for preservation;
request console tests only for changed behavior and observable new questions.
