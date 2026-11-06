# Loader integration baseline

New game-menu integrations must carry the project's existing efficient read
and verification paths into their first gameplay test. A working first boot
does not establish an acceptable launch or return lifecycle.

For immutable launch capsules, read each contiguous physical run in one
session. A single-block probe before overwriting the old executable may be a
separate session; it does not justify reopening the stream for every remaining
sector. Preserve every block CRC16 and the complete capsule CRC. The linked
Smash fixture spans four blocks in two runs and must issue exactly three reads
including its probe. The full SCI test also checks one contiguous 456-block
run, CRC failure and failed-stop quarantine.

Use the existing fast full-file CRC implementation in the native cold stage.
Keep complete executable and source SHA256 verification. Execute the pure
native image-proof replay with its established operand-cache wrapper, publish
all private output before restoring the original cache state, and retain the
linked ABI/cache mutation tests. The architecture audit requires these paths;
they are delivery requirements rather than later tuning after a slow report.
The runtime also retains its known `-O2 -fomit-frame-pointer -fno-PIE`
settings. Packaging checks effective options in the actual linked DWARF
producer records, so an old unoptimized object cannot pass on the strength
of a changed environment alone.

Fresh ownership admission must also use the bounded FAT batching path.
Two independent 16-sector windows handle alternating FAT copies; geometry
comes from the current volume boot record, and identity, write and sync
boundaries invalidate the cache. Preserve every namespace, mirror and
incoming-link check. The FAT32/exFAT sanitizer fixtures compare every logical
sector and admission result against the original uncached reader, including
allocation changes and CRC/teardown faults. The linked gate rejects omitted
epoch invalidation and unverified-prefetch publication.

Exercise successive selections as one session: Shining Force, original menu,
Phantasy Star II, original menu, Shining Force. Verify that each new worker
recovers the current shared card, that accepted saves and settings survive,
and that a pending command cannot be retired. A checked collection allocation
epoch performs full host ownership admission once and uses cheap legal
transfers thereafter, retaining fresh CID, journal/image validation and
checkpoint/handoff checks. Never replace this with repeated full scans or an
on-disk receipt replay. Check the real reset entry before SDK teardown;
separate successful first-launch and storage tests do not prove the transition.

An exit replacement needs source-verified code/data lifetime and terminal
ownership. Preserve the old execution domain while work is busy; only after
idle and producer quiescence may code, vectors or storage authority move.
Prove both a fresh cold launch and a launch after another game returns. Carry
accepted worker geometry and exact helper code into the test, including the
separate low reader that hydrates a helper. A helper hash alone cannot detect
a changed hydration or completion policy.

Report operation counts and source/model evidence separately from console
elapsed time. Preserve a working restore and request console timing for the
new build without promising an unmeasured speedup. A gameplay test package
must pass these gates before delivery.
