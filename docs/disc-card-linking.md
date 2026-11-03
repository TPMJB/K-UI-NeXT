# Cards for an inserted disc

Copy only `KUI/runtime.kui` from this ZIP onto your SD card with the console
powered off. Keep the previous runtime on the PC to revert the GUI. The update
includes the black-VMU backup recovery, improved labels, Save details and
remembered import choices. No loader module replacement is needed.

## Open the disc's cards

Insert the game, open **Disc tools → GD Play**, then press **START: Cards**.
K-UI reads the inserted disc's real title, product/release ID, region and version.
It does not require a ripped image or a `/Games` folder. Identification reads
checked metadata without hashing the executable.

The list shows the cards linked to that product, their names and free space,
and any saved default. **Up/Down** navigates cards across pages; **Left/Right**
changes pages. **X Refresh** reads the disc and its links again. **B** returns to
GD Play without launching a game.

- **Y Link imported card** opens the virtual-card collection. A matching save
  is a suggestion to review, not an automatic assignment. Select a card with
  **A**, review the exact source and inserted disc, then confirm with **A**.
  Archives and cards needing a playable copy retain their original data; cards
  that can be shared directly retain their journal. The confirmation explains
  which operation applies.
- **START New card** creates and links a fresh 200-block virtual card after
  confirmation. Existing cards and save files remain available.
- **A** on a linked card lets you set it as the launch default after reviewing
  the choice. **R Clear default** removes that preference; multiple cards will
  then require a choice at launch. A sole linked card is selected automatically.

Browsing creates no card. On supported native disc launches with Virtual VMU
enabled, the existing launcher automatically creates a dedicated blank card
when the release has no assigned card. This menu lets you link imported saves
or create the new card before launching, then inspect the resulting links.
The card image is created first; the game creates its internal save files when
you save during play.

## Release checks and compatibility

Changes require the same reviewed disc metadata, SD card/partition, link
catalogue generation and selected source identity. Changing discs or cards
between review and confirmation requires a refresh. Links are additive;
existing defaults are retained unless you explicitly change or clear them.
Interrupted card creation retains a completed copy for retry where possible.
No physical VMU is written by this menu.

The saved assignment namespace remains the disc's normalized product ID.
Region/version and a metadata/TOC signature prevent a stale menu choice from
silently acting on a swapped disc; this does not introduce a separate persistent
namespace for different pressings sharing the same product ID.

Card management does not enable or disable Virtual VMU. Use **GD Play → Y
Options** to enable it and choose A1/A2 for the next launch. Physical-disc VMU
playback keeps its existing SCI/native-game admission checks. Identifying a
WinCE disc or storing a link does not establish that the physical-disc VMU
loader supports that game. Existing executable, allocator and memory checks
still run before launch.

## Console test

1. Open the cards menu with a known disc and check its title/release information
   and any existing card/default. Browse once with Virtual VMU Off as well.
2. Link an imported card, refresh and reopen the menu, and verify the same card
   remains linked. Launch a supported game with Virtual VMU enabled and check
   that its old save loads and a new save survives return/restart.
3. For a disc without a saved card, confirm **New card** and check that the
   labelled empty card appears with 200 free blocks. Alternatively, let the
   supported launcher create the blank card on first launch, then reopen Cards.
4. Check multiple cards/default/clear-default, and cancel one confirmation.
5. Swap the disc after opening a confirmation. The old request must refuse;
   refresh before linking the new disc. Save Diagnostics if a refusal is unclear.

Host tests exercise metadata-only native/WinCE/PAL identification, real
FAT32/exFAT card linking and creation, archive/journal preservation, defaults,
interrupted publication, stale disc/storage/source requests, and UI dispatch.
Run `make test-disc-cards` for the focused host regression suite.
The Dreamcast runtime is built and its worker stack is checked. Console
acceptance of this new card-management path remains pending.
