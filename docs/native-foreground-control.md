# Optional foreground reader control

This separate module uses the Standard launch route and keeps the native SCI
reader, manifest and state below `0x8c007d00`. It reserves no game heap and
installs no SDK interrupt callbacks. Each finished or cancelled read stops the
card stream and returns the SCI controls before a later request can acquire
them again. A failed teardown stops the reader instead of reusing that owner.

This is a diagnostic control for physical VMUs. It adds no virtual VMU, CD
audio, or proof that Quake III completes a console boot or plays correctly.
The real Quake III GIN copies overwrite the current Background reader's heap
lease; the low foreground control avoids that particular overlap. Other
secondary-program behavior and SD hardware timing still need console testing.

Use **VMU Off**, **Background Off**, **CD audio Off**, and **RAM patches Off**.
The module accepts
plain native SCI maps with at most **64 combined track and extent slots**;
the supported flags are the ordinary boot CRC flag or zero. The normal
initial executable checksum and complete supported SDK-family checks remain.
Larger maps, other transports, compressed/CD boot profiles, and New reader
requests are refused.

The Games frontend maps against this control's 64-slot capacity. When all
audio-file extents would exceed it, the existing mapping fallback retains
every track's TOC entry and maps the data files only. A 40-track disc with
three contiguous data files therefore uses 43 slots, rather than refusing
after preparing an 80-slot map for the ordinary 160-slot reader. Excess data
fragmentation still refuses; audio sectors without extents remain unreadable.

Before trying the control, back up both `KUI/runtime.kui` and
`KUI/apps/games/retail-physical.kui`. Install the matching updated GUI at
`KUI/runtime.kui` and the control module at
`KUI/apps/games/retail-physical.kui`. Keep the backups outside those paths
so they can be copied back for rollback. Other game modules and saves do
not need replacement. The current GUI's VMU Off + Standard route selects
`retail-physical.kui`; replacing `retail-boot.kui` does not select this control.
The launch log confirms the exact path with `Foreground control selected:`.
If the earlier `48453aad190b` control was copied over `retail-boot.kui`, restore
that file from the backup made before that installation. In particular, do
this before returning to Standard with VMU emulation On. The corrected
control package does not replace `retail-boot.kui`.
To stop using the corrected control, restore the saved `retail-physical.kui`;
the updated GUI can remain installed.

A terminal reader failure or intercepted menu return stays on the diagnostic
screen. Record it and power off the console; the control does not automatically
reboot after that return screen. Other original firmware/reset paths in the
game remain unchanged.

Build and verify with the repository's existing SH toolchain:

```sh
make -f Makefile.native-standalone-foreground
make -f Makefile.native-standalone-foreground foreground-host-tests
make -f Makefile.native-standalone-foreground foreground-launch-route-tests
python3 tools/check_native_standalone_foreground.py \
  --build build/native-standalone-foreground \
  --report build/native-standalone-foreground/artifact-proof.json
```

The build output is `retail-native-foreground.kui`; rename it to
`retail-physical.kui` only in the optional control package. Its separate
`KGF1/FLOW` capability declares Standard/64 slots/zero lease and does not
advertise the New reader capability. Clean release builds also run the
artifact checker with `--require-clean`.

The host models exercise production GD, cursor and SCI stream C against
explicit card/register fixtures. The optional original-file tests execute
the real GIN copies and bounded secondary CRT clearing loops, checking the
actual linked low code and retained-state canaries. These tests establish
their stated memory and request contracts, not console compatibility.

On 2026-11-02 the owner reported Quake III working with the corrected b46a8f805a5a
foreground control and VMU, background loading, CD audio and RAM patches off.
This report does not establish virtual VMU or CDDA support. The physical
reader module can be reused unchanged while the GUI's map preparation is fixed.
