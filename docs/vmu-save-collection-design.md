# Automatic cards within one save collection

The owner clarified on 2026-10-19 that the goal is a different virtual card
for each save set, automatically selected for its game, with all saves
presented in one K-UI collection. It is not one globally shared 200-block card.

Each card exposes the standard 200 usable blocks and has its own 128 KiB image,
journal, identity and live ownership descriptor. There is no collection-wide
200-block capacity limit. For scale, 700 raw images occupy 87.5 MiB before
journal/metadata overhead; 1,000 occupy 125 MiB. These are storage examples,
not assertions about the number of released games.

## Selection and file grouping

- Select the game's default save-set card automatically before launch.
- Keep all files that a game requires together on that card: progress,
  settings, system files, guild cards and other companions. A game may create
  multiple files during a session. This is not arbitrary per-file routing
  during Maple commands.
- Allow additional independent save-set cards for separate playthroughs.
  A stable default avoids requiring manual card selection for routine play.
- Overlapping filenames on different physical cards must remain separate save
  sets. On2026-10-20 the owner reported likely PSO V1 saves with the same name
  on two cards. Do not rename, merge or overwrite either file: preserve each
  original save set on its own virtual card and provide an explicit card
  selector when game activation is implemented. The current importer already
  allocates a new card for every confirmed transfer; vNNNN IDs distinguish them.
- Define stable game bindings so original/2048 and later container variants
  of the same game do not accidentally create unrelated cards. Multi-disc
  bindings must preserve shared saves; region/version aliases require known
  save compatibility rather than a title-string guess.
- Present the cards through one VMU Manager/save collection. The binary P2
  ownership descriptor is an internal per-card object, not a user account.

## Preserve current progress

The owner confirms that Toy Commander loads the old imported save and new
saves on p0004, and those new saves survive a full reboot. That current p0004
therefore supersedes the earlier uploaded reconstruction as a migration source.
The current p0003 separately holds newer Silver progress. Neither current pair
may be replaced by its older prepared image or split from a stale upload.
Creating save-set cards should retain source pairs and copy complete verified
file groups into new destinations, then compare payload bytes and metadata.

## Implementation boundary

Create and verify each image/journal/identity/ownership allocation before a game
launch. Reuse the existing durable backend, recovery and launch-time ownership
checks. Add a checked dynamic card-selection/profile handoff so users do not
need a separately personalized loader for every new card.

The current game interception adapters still use exact Toy Commander/Silver
hook sites and SDK ownership layouts. Automatic card allocation and selection
must not make unsupported games execute either game's fixed memory patches.
Broader interception is the remaining compatibility task; removing title
checks alone does not implement it.
