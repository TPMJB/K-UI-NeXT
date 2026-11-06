# Nested VC2 persistent A2 pilot

This profile continues the hardware-confirmed four-phase WRITE staging test
2058e4fcb07d. It replaces the deliberate pre-commit stop with the existing
deferred512-byte SD journal provider and a source-bound foreground reader loan.
The original Maple DMA fence stays held while storage work runs with interrupts
enabled on its private4KiB stack. The native CE worker waits on its own HANDLE
at extension+180, using the original CE stack and source-verified Wait thunk.
Commit ACK follows journal/base barriers, readback and successful reader release.

K-UI selects a dedicated KUIVC2TEST card through the existing card service.
After K-UI shuts down, the high stage validates its current filesystem ownership,
recovers/checkpoints the journal, and seals the card geometry, CID, maps and block
CRCs. A generated99-sector capsule contains the unchanged48KiB helper image and
a1536-byte handoff tail. Its allocations must exclude the game and every selected
card allocation, including slack. The native loader reads the helper only after
native startup has finished; its initializer reads and checks the handoff tail.
No K-UI callback or filesystem object survives into CE.

The total persistent reservation is64KiB at8cfc0000..8cfd0000. Lower16KiB holds
zeroed working state; code entry remainsacfc4000. High records, proof stack and
I/O stack retain their fixed addresses. Native cache publication, all private
memory exclusions, explicit DMA destination rejection and CE ROMHDR RAMEND use
the complete reservation. No prefetch buffer is allocated. The13.89MiB boot
allocator arena from the earlier layout is not runtime free-memory telemetry;
minimum free RAM during real gameplay remains unmeasured.

Build with the restored KOS environment:

```sh
make -f Makefile.vc2-ce-vmu VC2_LATE=1 VC2_PERSIST=1 BUILD=build/vc2-ce-vmu-persist
```

The first console check is normal save, power-cycle and load, with physical VMUs
removed and the launcher's VMU/background/CD-music/RAM-patch switches off. This
source-specific nested A2 pilot is selected by capability3 independently of those
switches. There is no deliberate success stop. Any terminal helper fault has three
numbered SAVE pages. Host tests and linked audits cover protocol, card ownership,
storage fault paths, helper/capsule pairing, late initialization and stacks;
console persistence is unverified until the save survives the user's power-cycle.

The31b7c4be4c1b console run initialized the backend but hit fault100 before
provider I/O: one bound-owner check still used Armada's8c145894 current-thread
slot. It now uses VC2's profile-selected8c1d5894 consistently. Fault100 records
the actual differing register/address and expected value. Production guard tests
use distinct KData slots for both profiles and reject stale cross-profile loads.

Launch preparation reuses verified session code rather than rewriting it on
repeat launches. The VC2 stage keeps its post-recovery snapshot across the
checkpoint, which changes only the journal marker, removing526 duplicate sector
reads. Dual builds produce identical1272-byte handoffs for clean, recovered and
imported cards; all256block CRCs and the whole-card SHA remain current. Fresh
filesystem ownership admission, recovery and final identity checks remain.

The November 3 diagnostic log confirms an exFAT card with128KiB clusters.
Cold-stage FAT windows now hold32sectors per copy; directory/bitmap sectors
also pass through an aligned512-byte receiver. The core's metadata buffers
were12mod32 on SH, which could refuse the existing DMA path. All live chain
walks and exact ownership comparisons remain. A negative target filter skips
impossible matches before the exact lookup. These buffers are cold-stage
scratch and do not change the64KiB runtime reservation.

The ownership display refreshes in place during admission. It reports geometry,
elapsed/read milliseconds, raw runs/sectors, existing SCI DMA/PIO counters, and
FAT/chain/directory counts. Timing borrows TMU2 only if idle with no pending
underflow/capture and with interrupts masked; all registers are restored before
handoff. If unavailable, time fields readFFFFFFFF and counters still update.
This instrumentation is stage-only; high-helper and low-resident binaries are
unchanged by it. Hardware startup timing remains to be measured.

The same log shows both native RPG tests stopped with `Invalid image request`
before handoff. Retargeting `1ST_READ.BIN` to shorter `MDE_US.BIN` left a nonzero
byte after the new terminator, violating canonical launch-wire text validation.
Both filename arrays are now cleared before assignment. The regression uses
the original40-track/six-extent map and round-trips both cold child requests.
This fixes launch preparation; native RPG VMU detection still needs console
results. Test via Games -> Sega Smash Pack details -> Y Options -> Advanced ->
Developer tests -> Phantasy Star II VMU / Shining Force VMU -> A.

Subsequent user tests confirmed Phantasy Star II saving and loading after a
relaunch from Developer tests, and Shining Force saving. These reports do not
establish normal-menu RPG transitions or full-collection compatibility.

The following regular-launch failure photo includes `NATIVE VMU: LIVE OWNERSHIP
/ RECOVERY - KEEP POWER ON`, a marker found only in the generic native VMU
stage. Standard launch with global VMU On selected `retail-boot.kui`, bypassing
the supplied collection reader in `retail-physical.kui`. The collection route
must be selected through exact original menu/IP admission and the installed
VC2 capability, independently of the native preference and without changing
that preference. The regular menu enters VC2; RPG save support continues to use
the verified direct child entries until warm child switching is implemented.

The later63775 video confirmed the aligned DMA path: all sampled blocks used
DMA and PIO calls stayed zero. Ownership progress continued for over30seconds
of the recording, which started after the scan had begun. CLOCK0/FFFFFFFF meant
TMU2 could not be borrowed. The crowded text was an instrumentation layout bug:
the fixed live page began two pixels below an existing row and restored the old
cursor, so later phase summaries overwrote its counters. Page transitions now
clear the owned body once and retire it before printing results.

The relevant performance path is the post-bootstrap relay, not the initial
stage entry. In159f182 its linked relay wrote CCR0x808 before calling the P1 C
relay, disabling both caches for ownership/SD-feed/CRC work. The VC2 persistent
late stage now uses the existing instruction-cache-only relay policy, writing
0x908 through P2 and restoring the original owner's cache state on exit. The
data cache stays disabled. Invalidation bits self-clear, so live CCR should
normally read0x100 (ICE set, OCE clear). Console speedup remains unmeasured.

The user also reported corrupt VC2 textures with comparison02. That excludes
its disabled sampler/fence/VMU-I/O work as a sufficient explanation; the helper,
entry hook and64KiB reservation were still present. Comparison03 removes those
VC2 additions. Its first attempted hardware run showed build1734830d08df and
the generic native VMU stage, with resident-init result2. The unmarked generic
control could still fall back to `retail-boot.kui` when the native VMU preference
was On, so that run did not test the intended no-reservation reader.

`Makefile.vc2-reader-control` now emits a unique typed24-byte variant3 tag in
the cold stage, retaining the ordinary foreground ABI,64slots and zero lease.
Exact original menu/IP proof plus this tag selects the physical control with
either native VMU preference. Unmarked generic controls keep their existing
routes. Malformed/duplicate tags and contradictory persistent capabilities
are refused. The stage prints `SMASH PACK COMPARISON 03` before boot. It has no
VC2 helper or RAMEND change, grants no save authority, and leaves the preference
unchanged. A valid03 hardware result remains pending; this startup change does
not claim to fix textures.


The valid tagged comparison03 subsequently rendered VC2 textures correctly on
hardware. Comparison02 still corrupted them. This narrows the difference to
installed Maple changes, helper/reservation presence, or remaining low-reader
policy; it does not uniquely prove a RAM collision or a hook ABI defect.

Comparison04 retains the complete persistent helper/session,64KiB RAMEND
reservation, low-reader service ownership and exclusion policy. Its immutable
mode4 selector tells the source-admitted installer to change only RAMEND,
leaving Maple text bounds, bridge padding and the worker instruction original.
It installs no Maple hook and advertises no virtual card. Existing0/1/3 modes
retain their installer behavior; all delivered helpers have identical code and
layout except the four-byte selector. Continue without saving in this test.

Source-bound ASan/UBSan tests compare the entire original kernel body against
mode4 output: only RAMEND may differ. They check original worker400bd015,
unchanged text/padding, one cache publication, rollback after injected RAMEND
corruption, mutation rejection and boundary canaries. An independent review
repeated these checks. The helper still fits before the fixed wait gate with
92bytes spare; no stack or guard was reduced. Console04 results are pending.
A clean04 result implicates installed Maple modifications; corruption keeps
the reservation/helper/reader differences in scope. Normal saving and direct
RPG routes remain available; normal-menu RPG switching is still pending.


The user subsequently reported corrupted textures with comparison04 as well.
The absence of its Maple hook is therefore insufficient to prevent corruption;
no root cause has been established. The remaining differences include its
reservation, low-reader copy policy and owned destination exclusions.

An exact-source review found a concrete dangerous path: this kernel's DDHAL
maps physical04000000/05000000 VRAM with MmMapIoSpace, while WSEGACD retains
original virtual pointers on its even-aligned PIO16/39 fallback. The VC2 late
reader forces byte memcpy, unlike generic03's aligned word path. Numeric-only
VRAM detection does not identify a virtual VRAM alias. This is a demonstrated
source-supported path, not proof that a particular corrupt texture used it.
The source windows/literals and allocator geometry are bound in
`docs/evidence/vc2-texture-copy-source.json` without commercial code bytes.

`Makefile.vc2-texture-probe` now creates a dedicated read-only mode4 probe.
It retains the original payload copy policy and absence of the Maple hook.
After authenticated helper ownership, a fixed P2 shim compares each delivered
chunk with destination readback before progress/IRQ completion publication.
Aligned16-bit destination reads avoid forbidden VRAM byte accesses. It records
FNV32 source/readback hashes, first differing byte, exact chunk extent, request
and caller context, read-only matching UTLB endpoints and CPU registers. An
immutable terminal snapshot also records source-derived RAMEND/effective bounds
and checked physical allocation descriptors. No MMU/TLB/cache register writes
or VMU save writes are added to the measuring path; the low private stack and
existing reservation guards remain. The normal helper/runtime are separate.

The probe deliberately stops at a mismatch/map failure/observed virtual VRAM,
otherwise near T_COMMON's tail or after32768chunks, and shows three numbered
pages. Its hashes describe one chunk, not an independently verified asset.
Matching readback cannot prove the right source sector or later graphics
processing. UTLB endpoints withzero/multiple matches stay unknown. This is an
actual-data collection step, not a texture fix or a free-RAM/prefetch result.


The9c7 hardware capture stopped with WHY2 on an A6-byte PIO39 payload. A unique
4KiB UTLB entry maps destination0e969f3a to mainRAM0c513f3a; the first source /
destination difference is offset5e,48 versus43. The read's source sector is in
SOUND11.WVP. Photograph transcription uncertainty is retained in the separate
hardware receipt, not silently resolved. The allocator descriptor is consistent
with the intended reservation; it does not show a helper overlap or free-page
count. This capture does not demonstrate the virtual-VRAM byte-write candidate.

The emitted old translation routine reads a P1 literal at operand-cache index
1f80, also used by the differing destination line. It ran before comparison,
so its cached data access can perturb the view. The follow-up retains a full
pre-copy source in P2 storage, performs immediate destination/live-source
comparisons in P2 code before cached metadata, then compares again after
controlled matching-index immutable P1 reads. DMA1 snapshots and strict PRE/POST
tuple matching separate source lifetime, immediate delivery and later cache
activity. Copy policy and cache/MMU registers remain unchanged. Matching-index
reads deliberately perturb cache occupancy; no texture fix is claimed.
