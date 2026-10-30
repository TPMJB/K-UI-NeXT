# VMU import wizard hardware check

Install this UI over the lean General `34dc7b5e15ee` combined native reader. The installer changes only `KUI/runtime.kui` and keeps an exact checkpoint of the UI it replaces. All existing VMU cards, journals, profiles, game settings, native modules, and the General installer's backup remain in place.

Extract **K-UI-VMU-Import-Wizard-@BUILD@.zip** and open the extracted **K-UI-VMU-Import-Wizard-@BUILD@** folder. From that folder:

```bash
python3 install.py /run/media/apollonius/Dreamcast
```

Safely eject the card, boot the Dreamcast, and open **Home → VMU tools → Import wizard**. **VMU explorer** opens the existing manager.

1. Select the physical VMU slot and import it. Check the displayed file and block counts. Importing only reads the physical VMU; it never formats or writes it.
2. Connect another VMU and import it. Compatible saves share a staged virtual card. A differing save with the same internal filename, a capacity limit, or another mini-game requires another card; the source card's files stay together. Exact duplicate saves can be skipped.
3. Press **START** to continue to the game list. Choose the games that should use each staged card. Use **Left/Right** for pages, **L/R** for cards, and **A** to select or remove a game. Multiple games can share one card; a game can select one imported card at a time. Different disc images with the same product number share that assignment.
4. Review the assignments and create the cards. Every staged card is saved in a new `pNNNN` folder, including a card without a game assignment; existing game cards are retained. The selected games prepare their card automatically on their next supported native **New** launch. No PC profile command is needed.
5. Launch a supported native game, confirm its imported save appears, save again, power the console fully off, and reload. Verify both the imported progress and the later save. Repeat with a second game sharing the card, and a game assigned to another card.
6. Reconnect the original physical VMUs in the explorer and confirm their saves remain unchanged. Check that an unassigned game still uses its previous card.

A session can stage eight virtual cards. The game picker reads `/Games`, with up to three folder levels, 2,000 visible entries, and 256 distinct product numbers. It does not guess game matches from save filenames; your game choices determine the assignments.

The wizard does not extend game compatibility. Automatic imported-card selection currently applies to the combined native reader with virtual A2. WinCE and standalone reader launches do not gain virtual VMU support. Physical A1 remains available for its normal role; virtual storage is A2. Shared cards still have the normal 200 user-block capacity.

A game assigned to an imported card refuses Legacy or standalone launch if that route cannot expose virtual A2. This prevents a launch from silently ignoring the chosen card. Native games still pass the existing executable and memory checks at launch.

Keep physical originals until the hardware checks pass. Cancellation keeps the RAM session available until restart or a new import. Once card publication starts, the game choices are frozen so a retry completes the same assignments. Interrupted publication may leave a new unassigned card, while existing cards remain intact. Retry from review, or start a new import if you want different choices. The UI reports failures rather than replacing existing saves.

To restore the exact UI present before this wizard build:

```bash
python3 install.py /run/media/apollonius/Dreamcast --restore
```

Restore leaves new imported cards and their game assignments on the SD card, and restores only the old UI. A later external UI update is protected: this installer refuses to overwrite it during a reinstall or restore of the same wizard build. Each different wizard build creates its own checkpoint. Do not use the General installer's restore to undo this UI-only update; use the command above.
