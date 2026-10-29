# VMU/menu and salvage integration

The owner requested incorporating the existing changes from Salvage Retry
Improvements into this VMU/menu branch. Merge parents are:

- `e92b85acb897231d006da1f3d269e3da419d674f`: card identification and menu input fixes.
- `90919fc31a639295e35eeee8dbb6af019cdd64a5`: verified damaged-disc speed and firmware retry controls.

The salvage commit's parent is `e506b571d3a8ce57e19601bb5dcd85e687c024e2`,
already present in the VMU/menu history. Its shallow delivery was imported as
the original commit; the combined repository retains complete history.

Merge conflicts concerned two additive shell includes, the page enum and test
setup/assertions. Both includes and page groups are retained. The new salvage
page is appended after the existing VMU/card pages. Tests retain both suites and
use the existing width-clipped progress assertion while also checking the new
requested-policy line.

No physical-disc VMU implementation, automatic trial scheduler, new recovery
strategy or native runtime change is part of this integration. The controls
select one session policy. Physical speed/recovery effectiveness remains
unverified; the separate trial-scheduler discussion remains future work.

The GUI-only installer preserves installed native modules, virtual cards,
journals, assignments and earlier checkpoints. Validation covers the combined
shell, drive controls/native optical adapter, audio/input scheduling, build and
extracted install/rollback paths. Unchanged salvage/save engines retain their
existing fixture evidence, with source equality checked during integration.
