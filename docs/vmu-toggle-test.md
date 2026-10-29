# VMU tools: emulation on/off hardware check

**Experimental installation candidate.** Runtime host tests, exact compiled toggle-kernel checks, state-layout checks, completion-selector instruction tests and reader comparisons pass. The full persistent instruction-model audit still needs adapters for the changed installer layout and reader-only mode. This package is provided for explicit hardware testing, with that validation gap recorded, and is not a production release. Keep your existing save backups and use the supplied rollback if testing fails.

This update includes the import wizard and a saved **Emulated VMU: On / Off** control in **Home → VMU tools**. The choice applies to the next game launch. Existing installations default to On.

Extract `K-UI-VMU-Toggle-@BUILD@.zip`, enter its folder, and run:

```bash
python3 install.py /run/media/apollonius/Dreamcast
```

The installer requires the lean General `34dc7b5e15ee` combined native module or a matching later toggle-capable module. It copies the installed card profile into the new module automatically; no `--profile` argument is needed. It updates only `KUI/runtime.kui` and `KUI/apps/games/retail-native-dual.kui`, with exact checkpoints for both files. The UI includes the integrated ripper and import wizard. Game cards, journals, profiles, assignments, launch settings, the standalone reader, and the General installer's original checkpoint remain in place.

In VMU tools, select **Emulated VMU** and press **A** to change it. The displayed value changes only when the saved setting is confirmed. Turning emulation Off keeps the background reader and its full prefetch cache active. Physical VMUs remain available normally. Turning it On again selects the same virtual cards and imported-game assignments. The wizard can store cards while emulation is Off and reminds you to enable it when you want to use them.

The first version retains the current 324 KiB game-RAM reservation in both modes. This change separates card emulation from the reader; it does not reclaim VMU memory or broaden game compatibility. Virtual A2 still requires a supported combined native launch. Other loader routes retain their existing compatibility limits.

Test one known working game first, such as Code Veronica:

1. Leave emulation On. Launch with the same New reader setting used before, load the virtual A2 save, and save once. Power the console off completely and confirm the save reloads.
2. Return to VMU tools, choose Off, and reboot to confirm the setting persists.
3. Launch the same game with the same reader setting. Confirm the virtual card is absent and that the physical VMUs are available. Check the intro and normal gameplay for changes in reader behavior.
4. Choose On again. Confirm the previous virtual save is still present, save again, power off, and reload. Repeat with a game that uses an imported shared card.
5. Open the import wizard with emulation Off. Import a card and assign a supported game, confirm the reminder, then enable emulation in VMU tools and verify that game uses the imported save.

To restore the exact UI/module pair that this build replaced:

```bash
python3 install.py /run/media/apollonius/Dreamcast --restore
```

Restore leaves your saved emulation setting and all imported cards/assignments in place. The previous UI may not understand the new setting. Use this installer to restore this update; the older General or wizard installer will correctly refuse later file changes. If installation or restoration is interrupted, rerun the same command with the complete extracted package before booting the console. A later external GUI or module update is protected from overwrite by this build's installer.

Host and compiled checks do not replace the hardware save/power-off/reload checks above. This is a hardware test candidate.
