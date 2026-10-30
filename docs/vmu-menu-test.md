# VMU menus hardware test — @BUILD@

This combined GUI also incorporates damaged-disc controls from salvage commit
`90919fc31a63`, together with the card labels and menu responsiveness changes
from `e92b85acb897`.

This is a **GUI-only experimental test update**. It installs only
`KUI/runtime.kui`, preserving the installed combined native module, standalone
reader, ordinary game loader, virtual cards, journals, profiles, assignments,
preferences, card names and older installer checkpoints. The native VMU/reader
code and its current 324 KiB reservation are unchanged. This update does not
broaden game compatibility or complete the older persistent instruction-model
audit. Host tests do not replace testing these menus on the Dreamcast.

Finish the current disc-recovery operation, shut the console down, and mount the
card on the PC before installing. There is no need to interrupt disc recovery or
test this immediately.

Extract the complete ZIP, enter its folder, then run:

```bash
python3 install.py /run/media/apollonius/Dreamcast
```

No profile argument is needed. The installer requires an already personalized,
normal-mode VMU toggle-capable combined native module, such as the installed
`6096398d3373` module. It accepts a newer valid ripper GUI as its predecessor and
checkpoints that GUI exactly. It does not reinstall an older reader. Both
installation and rollback refuse unexpected later GUI or native-module changes.

In **Damaged disc salvage → Damaged disc controls**, read-speed requests and
firmware retry counts apply only to the current salvage session. Both default
to **Unchanged**. Selected settings are read back and verified, reapplied after
successful drive reinitialization, and restored when the session stops safely.
Unsupported or unverified requests refuse to start salvage; uncertain drive
ownership can require a console restart. Existing saved progress is retained.
Firmware readback does not establish that the physical drive changed speed.
These are the existing manual controls from the salvage thread; automatic
alternating speed/retry trials are not implemented in that commit or this merge.

Home now groups ripping and disc playback under **Disc tools**, while memory
tests and benchmarks are under **Diagnostics**. Existing saved startup-app
choices keep their previous meanings.

**Home → VMU tools** provides direct entries for **Virtual VMUs**,
**Physical VMUs**, **Import saves**, **Backups & restore**, and
**Use virtual VMU: On / Off**. Off still permits browsing/importing and applies
at the next supported native game launch, independently of prefetch.

Virtual VMUs presents active game cards and stored snapshots together. The list
uses known game titles before generated card numbers, keeps custom names, and
shows the selected card's assigned game titles or save descriptions without
opening it. Save descriptions are labelled as contents, not game assignments;
an unassigned card or backup remains identified as such. Title lookup reads the
existing Games title/cover metadata once when loading a page, never on D-pad
movement. When that cache has no title, the admitted image's save descriptions
provide context. To populate missing game titles, use **Games → START → Scan
box art**, then refresh Virtual VMUs. This scan can cache titles even where no
cover picture is available. Open a card to inspect its
saves, rename it, view details or assign games. Technical paths and identities
are shown in Details. Browsing active cards reads the committed journal into an
in-memory view; it does not replay or write the card image/journal. Rename writes
only identity-bound display-name metadata. Assignment changes occur only after
the wizard's explicit review/confirmation. Where sharing requires a new card,
the wizard says so and preserves the original. Cards that fail validation are
identified as unavailable; browsing does not attempt to repair them.

On the Virtual VMUs list, **A Open** and **X Actions** open the selected card;
**Y Refresh** rechecks the list. The card page provides **View saves**,
**Rename card**, **Assign games**, and **Details**. **B** returns through the
page that opened the current view. Physical VMU save operations retain their
displayed backup/copy/restore controls and confirmation steps.

Game launch options now show **Saving:** followed by the current named/shared
card, a dedicated game card, **Automatic game card**, or the physical/loader
specific route. This is a read-only selection lookup. Its notice distinguishes
card selection from compatibility, which is still checked at launch. No card is
created or activated merely by opening game options.

Idle menus now poll controls more often, request a redraw on fresh input, and
repeat held directions sooner. Drawing uses a 600 KiB shell-only RAM buffer
before uploading the completed frame. This optional buffer is released before
game/executable preparation and memory testing; failed allocation falls back
to the previous renderer. Card-information lookups run as cancellable background
work on the existing storage worker and do not lock menu navigation. Routine
music-status publication now skips a busy audio lock rather than waiting for
stream refill/Ogg decoding, retaining the previous coherent snapshot until the
next successful poll. Background redraw cadence is measured from the completed
frame, preventing a slow repaint from immediately scheduling another. After an
idle redraw, the UI yields and polls again instead of adding the normal 8 ms
sleep. When no frame was drawn, the configured idle polling interval is 8 ms,
with a 250 ms initial directional hold and 80 ms repeat interval; these
are software intervals, not measured input-to-screen latency. Busy-operation
redraw caps remain in effect. The actual responsiveness improvement still needs
console confirmation, and the in-game reader/VMU RAM reservation is unchanged.

The menu RAM gauge counts the complete launcher's code, static buffers, stacks,
music cache and other live heap allocations. It is separate from the native
game VMU reservation. The reported roughly 8.8 MB in use does not by itself
establish memory pressure or explain input delay; no GUI scratch-buffer or
native memory-layout reduction is included in this update.

When testing is convenient:

1. Open each of the five VMU tools entries and check that Back returns to the
   expected page. Check the new Disc tools and Diagnostics routes, then compare
   short D-pad taps and held scrolling on Home, Games, and Virtual VMUs. Compare
   Home navigation with music playing and after Music's **Y Stop**, then return
   to Home; the audio-status change specifically addresses navigation during
   playback.
2. Find a known active game card and an older stored snapshot. The list should
   identify their games or save contents without opening them. Check that a
   custom name remains, and that unassigned cards/backups are not described as
   assigned merely because they contain saves. Inspecting a card should not
   change its saves.
3. Rename one card, return to the list, then reboot and confirm its name remains.
4. Open Assign games for a card, review a selection, and cancel once before
   committing. Confirm that cancellation leaves the previous assignment intact.
   Complete one assignment when ready and check that its game uses the intended
   card. A new shared copy is explicitly identified before creation.
5. Toggle Off and verify that browsing/importing still works, then enable it
   again. Existing virtual cards and assignments remain selected.
6. Reopen a known game's launch options and check the **Saving:** label against
   its assigned card. After a rename or assignment, reopen the options so the
   read-only card lookup refreshes. Check that Off identifies physical VMUs and
   that eligible games are not described as verified compatible merely because
   they appear in the import wizard.

To restore the exact GUI this build replaced, run from the extracted package:

```bash
python3 install.py /run/media/apollonius/Dreamcast --restore
```

Rollback restores only the GUI. Saves, new imports, card names, assignments and
preferences remain. If installation/restore is interrupted, keep the complete
package and rerun the same command. An unexpected later update is protected
from overwrite; there is no force option.
