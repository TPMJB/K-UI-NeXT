# Restore the original CD/root startup

The owner rejected the `f7229eed7699` early-artwork change: it painted over
the added delay and did not restore the original splash/chime lifecycle.
That candidate is not accepted as a boot repair.

Tag `v2.0.0` goes from RAM/video setup directly to the existing shell splash,
bounded disc STOP and I/O worker. Worker action 27 loads the existing
preferences and starts the original chime. Commit `758f62c` later inserted
`update_boot_start()` before this lifecycle. Subsequent startup diagnostic
writes and possible full GUI reloading also ran before the original splash.
The Games integration inherited those changes; Main was not a reference for
the original fast boot. The accepted Smash gameplay sequence does not establish
acceptance of that GUI's cold-boot timing.

The restoration removes early artwork and returns ordinary CD/root entry to
the original sequence. An EMPTY RAM handoff selects the physical ROOT entirely
in RAM: no updater connection, mount, activation/header reads, GUI reload or
startup report writes occur. Those operations are not moved behind a splash.
The original artwork, chime, three-second splash lifecycle, STOP ordering and
preferences remain on their original path.

Cold boot therefore starts the physical `KUI/boot.kui` / `KUI/runtime.kui`
installation, as the original loader did. It no longer activates a saved
update bank on power-on. Explicit **Restart K-UI** retains its existing checked
RAM handoff and bank validation; nonempty or corrupt contexts never authorize
the direct ROOT path. Banks and user files are retained. This is a manual-copy
ROOT test, not a change to the KUP install engine or package format.

The regression executes the actual production main prefix through worker
creation and the production action-27 body under ASan/UBSan, with the real
handoff classifier. Its frozen `v2.0.0` prefix is a positive lifecycle control;
the rejected early-paint prefix is a failing zero-preflight control. This
establishes ordering and removal of the extra operation; console timing remains
unmeasured until the owner tests the CD handoff, chime and menu arrival.

All installed Games readers/helpers remain the exact bytes from the preceding
reader-corrections test. No game-time worker, VMU/cache geometry or reader
policy is changed by this restoration. A fresh GUI also compiles the preceding
qualified early SDK family-role correction; its GUI caller uses only the
discovery result for standalone eligibility. That is accounted for separately
from byte-identical GUI Games objects.
