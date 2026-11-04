# Folder, ZIP and direct-restart console check

Upgrade GUI source: `c94129de53a3dd2838f12822c92626aeaa6f8ba4`.
This documentation-only successor supplies the example updater-ready test ZIP.
Its runtime and reader source are identical to the upgrade; only the admitted
build identity changes for the switch check.

Use the existing updater to install the upgrade .kup from KUI/updates. The
older GUI uses its existing BIOS restart for this first transition. Once the
upgrade is running, open Updates -> test, choose the supplied example ZIP,
review and install it, then START -> Restart K-UI -> A. Confirm the example
build appears directly without returning to BIOS. Restore previous and use
Restart K-UI to return to the upgrade. START also relaunches an unchanged
selected version from the Updates folder list or result page.

No reader files are delivered. The actual-file host checks cover ZIP staging,
reviewed-pin refusal after archive replacement, unchanged root reader hashes
and absent paths, runtime reload, handoff adoption and rollback on FAT32/exFAT.
Only the new console UI/restart flow needs observation; do not repeat settled
game, VMU, save/import, CDDA or WinCE compatibility matrices.
