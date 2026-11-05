# GUI-only updater console check

Initial GUI source: `e1e327c021b18bb3bd361c5810107a7a92c0dd24`.
This documentation-only successor supplies the second clean GUI build used by
the add-on package. Runtime source and reader source are unchanged between the
two; distinct build IDs exercise actual selection, restart and rollback.

Copy the add-on's `KUI` contents into the existing installation. Confirm the
initial GUI build, open Settings -> Updates, install the supplied candidate,
restart, and confirm its build. Restore previous, restart, and confirm the
initial GUI again. Keep the existing readers and save data in place.

The package and exact-file filesystem checks establish preservation; this
console check covers the new updater path. No repeat of settled game, VMU,
CDDA, WinCE or import compatibility tests is requested. The build IDs and
matching source archives are recorded in the delivered START-HERE and reports.
