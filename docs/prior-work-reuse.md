# Independently authored prior work

The maintainer authorized reuse of independently authored contributions from the
earlier DreamShell NeXT project, now named `TPMJB/K-UI_DS`. Those contributions
remain useful even though they were first distributed in a DreamShell-based
repository. This inventory identifies candidates and their dependency boundaries;
it does not treat every modified legacy file as independent code.

The inventory examines legacy revision
[`2a5309298dde8fb100da1e2e4e10517695c9780f`](https://github.com/TPMJB/K-UI_DS/tree/2a5309298dde8fb100da1e2e4e10517695c9780f).
Introduction commits below show separate files added by TPMJB. That history is
useful provenance evidence; a future extraction must also identify the selected
code or asset, its dependencies, and the applicable K-UI license/attribution.

**M1.5 selectively reuses the original K-UI launcher badge and three app icons.**
The exact PNG/SVG inputs, source revision, byte hashes and origins are recorded in
[the artwork inventory](../resources/branding/README.md). Their RGB565 conversion
is generated for the independent renderer. No legacy application, XML, reader,
event framework or inherited DreamShell background is imported. The finished
independent reader already provides its required verification behavior.
Browser/editor work can reuse the independently authored logic identified below
without importing the legacy application framework. Explicit salvage is a
separate follow-up described in [the salvage plan](salvage-plan.md).

## Candidates

| Prior work | Introduction and boundary | Potential use |
| --- | --- | --- |
| `applications/gd_ripper/modules/folders.h` | Added as a separate file in [`3a5d0cd5`](https://github.com/TPMJB/K-UI_DS/commit/3a5d0cd50ad84bde7efe74aba88e2ad04d6ccfe3). Fixed-memory, sorted directory pagination; legacy filesystem calls and mount names form the adapter boundary. | Adapt the original bounded picker to FatFs and the existing single storage worker. |
| QWERTY editor portions of `modules/vkb/vkb.c` | `3a5d0cd5` substantially rewrote an inherited keyboard file; the source distinguishes original SWAT keyboard code from TPMJB's QWERTY editor/input isolation. | Reuse selected authored layout/edit/navigation logic and behavior in a pure shell editor; do not import the legacy module, SDL widgets or event hooks. |
| `utils/verify_gd_dump.py` and tests | Added in [`6328009e`](https://github.com/TPMJB/K-UI_DS/commit/6328009e3fb86f71782748e4b7b4b8588409d828). Python standard library only; its completion adapter understands legacy `rip.state` and `rip.complete`. | Extract DAT parsing, track matching and independent-dump comparison with a K-UI metadata adapter. |
| `utils/make_gd_redump_db.py` and tests | Added in [`fb65759d`](https://github.com/TPMJB/K-UI_DS/commit/fb65759deadfd4034157f64c723b6da28a95961e). Imports the verifier's DAT types/parser. | Share a small catalog parser and converter without importing legacy completion semantics. |
| `applications/gd_ripper/modules/verify.c/.h` | Added in `fb65759d`. Current code depends on `ds.h`, filesystem/logging APIs, zlib, legacy state files and other ripper helpers. | Extract the catalog matcher and result classification after separating those dependencies. |
| `checksum.c/.h` in the same directory | Added in [`77186c20`](https://github.com/TPMJB/K-UI_DS/commit/77186c2043a2ee8a8c33903f3d9f3ed6a3c8e306); targeted CRC replacement added in [`102a59f5`](https://github.com/TPMJB/K-UI_DS/commit/102a59f523c684c55c88fc64f3eb1d5bf2263178). Pure checksum/sector routines coexist with legacy filesystem checkpoint helpers. | Candidate CRC replacement support for future salvage; no replacement of the finished reader's existing checks. |
| `recovery.c/.h` and `readback.c/.h` | Added respectively in `102a59f5` and [`2e2b6083`](https://github.com/TPMJB/K-UI_DS/commit/2e2b608355766ef69fe8beb02980ff0cf64773d9). Depend on legacy filesystem interfaces, sidecars and checksum helpers. | Future targeted salvage and bounded diagnostics, with a separate incomplete/recovered result model. |
| `applications/*/images/icon-next.svg` | Introduced in [`f7fab643`](https://github.com/TPMJB/K-UI_DS/commit/f7fab643c3727a90a739a4d8249a6c184aee1c74), recolored in [`7d5b4477`](https://github.com/TPMJB/K-UI_DS/commit/7d5b4477a9d4f8ea406c6ec58d590f6eaafa770a). The inspected ripper icon uses self-contained geometric SVG primitives. | Select and inspect individual icons; do not copy the mixed legacy image directories. |
| `resources/branding/k-ui-badge.png` and `k-ui-splash.png` | Added in `7d5b4477`; the [origin record](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/resources/branding/README.md) identifies artwork created with image-generation assistance for TPMJB. | Retain the established visual identity, encoded for the independent renderer. |
| `utils/generate_menu_music.py` and generated loops | Introduced in [`afa3767a`](https://github.com/TPMJB/K-UI_DS/commit/afa3767ac5a6c8f9e79cf61ca2ba894ee3d6fb23), expanded in `7d5b4477`. The [music record](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/applications/launch_app/music/README.md) documents original compositions, standard-library synthesis and no external samples. | Selected for the independent menu player. The [local inventory](../resources/music/README.md) records the unchanged original generator and reproducible recordings; the DreamShell runtime player is not imported. |

## Preserve the useful behavior

The legacy verification work distinguishes full-track matches, data-track matches,
identification by a subset of data tracks, partial matches, missing catalogs and
inconclusive mismatches. It also records whether hashes came from capture or a
saved-file reread. Future catalog UI should preserve those distinctions rather
than collapse them into one green "verified" result.

Legacy recovery preserves an immutable baseline, targets unresolved sectors and
does not publish completion while unresolved work remains. Those are useful
requirements for a future salvage workflow, separate from normal capture.

Shared-input fixes in [`223502dc`](https://github.com/TPMJB/K-UI_DS/commit/223502dc347e0a1a5e0b9c217c26934331e930a2)
and [`62a5fbce`](https://github.com/TPMJB/K-UI_DS/commit/62a5fbce9e0af69a48f726cd62f77147491d0c44)
provide lessons for direct D-pad focus, single delivery of A/B actions and modal
input ownership. They modify inherited event/Lua/app code; M1.5 implements these
behaviors in its own shell instead of importing that framework. Likewise,
`applications/gd_ripper/modules/module.c` and `app.xml` contain mixed upstream
code and later changes and are not standalone original-file candidates.

The legacy File Manager (`applications/filemanager`) is not a candidate
either. It is DreamShell's File Manager application: its two-pane list widget
(`lib/SDL_gui/FileManager.cc`) and its copy, move, delete, rename, archive and
open-with logic (`lua/main.lua`, whose header names SWAT as author) are
upstream code. TPMJB's `app.xml` layout and `modules/module.c` controller
navigation drive that DreamShell Lua/SDL framework and have no use without it.
K-UI's [File Manager](files.md) is written independently for the K-UI shell
and imports none of these files.

## Catalog data is a separate dependency

The [catalog provenance record](https://github.com/TPMJB/K-UI_DS/blob/2a5309298dde8fb100da1e2e4e10517695c9780f/applications/gd_ripper/redump-db.README)
identifies `redump.db` as an adaptation of Libretro metadata under CC BY-SA 4.0.
It separately lists TOSEC source DATs and hashes for `tosec.db`, without stating
the same license for TOSEC. Neither dataset becomes original TPMJB material
because our converter produced a compact version. A future catalog import should
retain its own source/version and attribution record separately from code reuse.
