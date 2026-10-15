# Legacy Games / ISO Loader feature audit

Audit date: 2026-10-15. Read-only comparison; no DreamShell implementation code
was copied into K-UI.

Legacy source: `TPMJB/DreamShell_NeXT` at
`2a5309298dde8fb100da1e2e4e10517695c9780f`.
Independent source: `TPMJB/K-UI-NeXT`, release/2.0-tyrfing integration worktree
based on `359918874090423e89764af15b0a8cbf0120a819`, including the wireless merge
in progress. Line numbers are navigation aids; named functions are the stable
references when integration shifts lines.

## Scope and interpretation

The old **Games Menu** really exposes its preset editor: Y invokes
`ShowPresetMenu` (`applications/games_menu/modules/module.c:1387`), and the
action bar invokes the same route (`module.c:2074`). Its settings dialog is
also reachable from the action bar (`module.c:2081`). The findings below are
therefore reachable product features, not a list of every upstream DreamShell
loader capability.

The separate old **ISO Loader** application has some additional reachable
features. Those are identified separately below. A menu control or loader
path establishes implemented functionality, not successful hardware
compatibility for every game. This audit did not retest either project on a
Dreamcast.

Abbreviations in the evidence column:

- `GM/` = old `applications/games_menu/modules/`.
- `ISO/` = old `applications/iso_loader/`.
- Unprefixed `src/` and `include/` paths refer to independent K-UI.

## Comparison

Implementation update: the first independent Games follow-up adds the scoped
native boot patches, defaults action and pre-handoff reports described in
[Games launch options](games-launch-options.md). The next source increment
adds the initial [native standard-LZ4 ZSO path](games-zso.md), with console
acceptance pending. Other rows retain the audit's original evidence and
runtime limits.

| Functionality | Independent K-UI status | Evidence and practical difference | Release priority |
| --- | --- | --- | --- |
| Saved per-game launch choices | **Existing, smaller set** | Old `GM/menu.c:1361` (`LoadPresetGame`) and `:1646` (`SavePresetGame`) load/save a large preset. New `include/kui/game_launch_settings.h:14` stores reader, CD music and executable encoding; `src/core/shell.c:373` restores valid choices and `:394` saves changes. A saved-choice mechanism is already present; do not list it as missing. Old presets are not imported. | Keep in Tyrfing; document supported choices. |
| Restore defaults / temporary baseline | **Defaults action implemented; unsaved baseline remains absent** | New `game_launch_defaults` in `src/core/shell.c` resets this exact image to detected reader/encoding, CD music Off and patches Off, then uses the existing verified settings save. Other image presets are retained. | Validate on console; no temporary unsaved-baseline mode is claimed. |
| Persistent pre-handoff launch report | **Implemented in normal shell launch preparation** | `kui_games_retail_prepare_launch` hashes the complete source executable before handoff and saves actual metadata/options via `src/apps/game_launch_report.c`. Two bounded CRC/sequence text slots preserve the previous complete report under partial writes. Report failure is logged while read-only-card launching remains available. | Keep for Tyrfing preparation; reports prove preparation, not successful game startup. |
| GDI, ISO and CDI images | **Existing with layout limits** | Old discovery accepts these at `GM/menu.c:3307`. New `src/core/game_image.c:340` lists GDI/CUE/ISO/BIN/IMG/CDI; `:864` (`kui_game_image_open_named`) routes descriptor, CDI and single-file parsing. New support additionally includes CUE/BIN and original/2048 pairing (`src/apps/games.c:219`, `src/core/shell.c:1450`). Parsing an image does not prove runtime compatibility. | Keep; no format-parity work needed for these extensions. |
| Direct compressed-image loading | **Partial: native LZ4 ZSO first slice implemented; console acceptance pending** | Old Games discovers `.cso` at `GM/menu.c:3307`; old `modules/isoldr/module.c:366` prepares CSO/ZSO data. Independent `src/core/zso.c` and `src/core/retail_zso.c` provide bounded standard-LZ4 decoding and a distinct 32-slot physical-file map. The dedicated `KUIRZB01` package admits native SCI Standard with 2048-byte blocks and at most 31 file fragments; WinCE, CDDA, background loading and RAM patches are excluded. CSO/CHD and historical LZO ZSO still use offline import. The existing ripper's ZSO export is LZO, despite sharing `ZISO` magic. | Development test capability; require linked package/stack/instruction gates and console acceptance before promoting compatibility or final-release status. See [ZSO scope](games-zso.md). |
| CD music detection and emulation | **Partial; support is still profile limited** | Old Games auto-enables CDDA using track-4/track-6 heuristics (`GM/menu.c:1331`), and exposes source/destination/timer/channel modes (`GM/preset.c:818`–`:980`). New `src/core/game_image.c:166` classifies represented audio tracks; `src/core/shell.c:336` offers an opt-in for candidate GDI audio. Runtime admission remains guarded: native profiles at `src/apps/games_retail.c:606`, WinCE adapters at `:315` and `:462`. The latter are Armada, Bust-A-Move 4 and Worms Armageddon; the broad option is **not** generic WinCE CDDA compatibility. Unsupported WinCE adapters return a clear “turn CD music Off” message at `:465`. | Keep opt-in and exact guards. Broader metadata detection is useful, but generic CDDA needs independent runtime work and hardware tests. Do not transplant the old track-number heuristic or claim general support. |
| Emulated VMU backed by SD files | **Missing in game runtime** | Reachable old choices include shared root dump, private 128 KB and private 1024 KB (`GM/preset.c:1310`); `GM/menu.c:1136` creates files and `:1606` passes emulation to the loader. New game's persisted settings contain no VMU field (`include/kui/game_launch_settings.h:14`), and the launch preparation has no equivalent. The independent VMU Manager is a separate backup/restore app, not an emulated VMU during gameplay. | Later; substantial Maple/storage/runtime work. |
| In-game screenshots with chosen hotkey | **Missing** | Old `GM/preset.c:1126` enables screenshots and configures buttons; `GM/menu.c:1602` passes the hotkey into the loader. New `include/kui/game_launch_settings.h:14` and `src/core/shell.c:1541` have no screenshot option or persisted hotkey. | Later; resident capture and writable storage coordination need design and testing. |
| User-entered RAM patches | **Scoped native boot implementation** | Two saved address/value slots with a controller hex editor. `src/core/game_ram_patches.c` validates both aligned destinations inside the actual executable before any write; the ordinary native high stage applies them after original CRC/resident guards. No file patching or resident growth. | Ordinary native with CD music Off; CE/CDDA, ZSO and arbitrary RAM/runtime cheats remain outside this implementation. Console acceptance pending. |
| Manual OS, IP.BIN/direct boot, alternate executable, loader/heap placement | **Partial: automatic paths, no old-style controls** | Old `GM/preset.c:614` exposes Auto/Katana/Homebrew/WinCE, `:640` Direct/IP.BIN/IP.BIN CUT, `:706` loader address, `:776` heap location and `:1103` `2ND_READ.BIN`; `GM/menu.c:1597`–`:1633` passes the settings. New auto-classifies executable metadata (`src/apps/games.c:744`), exposes plain/scrambled encoding (`src/core/shell.c:1548`), and uses defined layouts (`include/kui/retail_loader_layout.h:18`). No interchangeable old boot-mode/heap UI exists. Old numeric settings cannot be mapped blindly to this loader. | Later, driven by a reproducible compatibility need. Preserve current guarded layouts for Tyrfing. |
| Reader/DMA/IRQ/async/bypass tuning | **Partial, intentionally different interface** | Old editor has DMA, async sizes, bypass and IRQ (`GM/preset.c:467`–`:568`) but serial SD forcibly disables DMA/bypass in launch preparation (`GM/menu.c:1637`). New offers Standard/Background and Normal/Aggressive background work (`src/core/shell.c:398`, `:1523`, `:1557`), with transport-specific implementation rather than identical DreamShell parameters. | Current controls are sufficient release scope; do not copy old knobs without corresponding independent semantics. |
| List/grid/gallery, artwork extraction and user art | **Existing** | Old X changes views (`GM/module.c:1401`); scan artwork is reachable at `:2075`. New cycles views at `src/core/shell.c:1407`; `src/apps/games_covers.c:630` scans with cancellation and separate unchanged/disc/user/missing/failed counts. These are retained features, not gaps. | Keep in Tyrfing. |
| Per-game browsing media previews | **Missing** | Old `GM/menu.c:477` (`PlayCDDAThread`) selects external gameplay audio after a delay; `GM/module.c:2052` invokes it on selection. This is separate from CDDA emulation after launch. New `src/core/shell.c:1430` changes selection/artwork state only; new background music (`src/dreamcast/main.c:449`) plays menu music, not selected-game tracks. | Later convenience feature; keep optional because reads contend with browsing. |
| Flat recursive library, category filter, persistent last game | **Partial** | Old recursively walks game roots (`GM/menu.c:3241`), exposes category/start-in-last-game controls (`GM/system_menu.c:368`, `:392`) and saves the last selected game on launch (`GM/module.c:1957`). New lists immediate directory entries (`src/apps/games.c:400`), allows folder browsing (`src/core/shell.c:1445`) and caches pages, but `list_games` resets selection and no last-game identity is saved (`src/core/shell.c:430`; persisted fields at `include/kui/game_launch_settings.h:14`). | Later usability work. Remembering the last game is smaller than adding a flat catalogue/filter system. |
| Main-menu game shortcuts | **Missing** | Old `GM/preset.c:1366` exposes shortcut name, icon size/rotation and create action; `GM/utils.c:439` writes a launch script/icon. New Game settings/advanced dispatch (`src/core/shell.c:1512`, `:1541`) has no equivalent, and independent home is an app menu. | Later convenience feature; requires its own independent home-menu design. |
| Mouse/analog-pointer selection and Games-specific themes | **Missing/changed UI** | Old `GM/module.c:2030` handles pointer selection; reachable system settings expose themes/colours at `GM/system_menu.c:444`. New Games input is controller dispatch (`src/core/shell.c:1396`) with the independent shell renderer (`src/dreamcast/shell_draw.c`, `games`). | Low priority; no loader compatibility effect. |
| Combined storage library / additional storage transports | **Partial; excluded hardware work** | Old combines IDE + SD roots (`GM/menu.c:3241`–`:3248`) using shell device abstractions. New image preparation uses `kui_sd_connect`/mounted `0:` (`src/apps/games.c:707`); runtime media transport is separately guarded (`src/apps/games_retail.c:457`, `:464`). Shared logical storage code does not establish working G1 hardware. | Defer G1 SD adapter work as requested. Wireless networking does not itself make network shares game-launch volumes. |

## Separate old ISO Loader features

These controls exist in the old standalone ISO Loader, but should not be
described as missing from the old custom Games Menu itself:

- Unsaved diagnostic **Baseline**, described above:
  `ISO/app.xml:679`, `ISO/modules/next_ui.h:144`.
- **Check** executable without launching:
  `ISO/modules/module.c:1710`. Independent image inspection validates bounded
  metadata/track structure (`src/apps/games.c:695`); launch preparation hashes
  executable bytes (`src/apps/games_retail.c:537`) and the detached loader
  checks them. There is no matching shell action for “prepare/check all boot
  executable bytes and stay here”; the resident/image tests are different.
- **Manual region override**: `ISO/app.xml:582`,
  `ISO/modules/module.c:1699`, and old
  `firmware/isoldr/loader/utils.c:170` pass/apply a region choice to the
  in-memory firmware information. Independent K-UI records image regions
  (`src/apps/games_retail.c:510`) but exposes no per-game region override.
  This is a later compatibility feature if a concrete title requires it; it
  is separate from permanently changing the console's region.
- **GPIO-0 reset** and **NAOMI test mode** controls:
  `ISO/app.xml:554`–`:562`; `.dni` registration at `ISO/app.xml:2`, file routing
  at `ISO/modules/module.c:1957`. NAOMI-specific title/region handling appears
  at `ISO/modules/module.c:474`. NAOMI is separate-platform scope and should
  not enter Tyrfing merely for old-application parity.

## Recommended release decision

1. Retain the already integrated saved choices, views/artwork, additional image
   parsing and guarded CDDA/WinCE work. Describe the tested image/transport
   limits accurately.
2. Validate the newly implemented pre-handoff reports, per-image defaults
   and bounded native boot patches on console. They were implemented
   independently; the first patch scope deliberately excludes CE/CDDA.
3. Validate the independent native LZ4 ZSO first slice on SCI, retaining its
   separate map/package and strict host/native audit gates. An exposed
   development launch path is not a compatibility result or a final 2.0 release.
4. Track VMU emulation, remaining compressed formats/dialects, broader RAM
   patches, alternate boot modes, screenshots and general CDDA runtime support
   as later features. They are meaningful gaps requiring independent work.

The original audit was read-only. The implementation update above records
independent source changes and does not claim new hardware-tested compatibility.
