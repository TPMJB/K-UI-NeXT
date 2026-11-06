# VC2 blank virtual-card test

This is the first nested WinCE VMU integration test following the working
VC2 reader `71256dd80b87`. It presents a generated, formatted, empty 200-block
A2 card. It performs no VMU save-file, journal or other SD writes. The existing
foreground image reader, two receive buffers, direct VRAM support and zero
speculative read-ahead remain in use.

The test intentionally stops after the first card-read finalization. It also
stops on an owner/fence/publication fault or after 1,200 completion visits if
the requested checkpoint never occurs. Two diagnostic pages then cycle every
eight seconds. A blue screen at this checkpoint is an expected test result,
not evidence by itself that saving works.

## Install and test

Copy the package's KUI folder onto the existing SD installation. It contains
the matching UI, the experimental `apps/games/retail-physical.kui`, and the
native-disc module carrying main's tested GD-Play read recovery. The UI also
includes main's ripper fixes. Remove physical VMUs for this run so VC2 must
check the generated A2 card instead of choosing an existing physical card.

Launch the same Sega Smash Pack SD image with Native/Default boot and virtual
VMU, background reading, CD music and RAM patches OFF. The experimental module
installs its own readonly A2 only at the VC2 transition. Select Virtua Cop 2.
Photograph both diagnostic pages. If only the ordinary GD STOP page appears,
photograph it instead. If no diagnostic appears, record the last visible stage
and whether the SD LED is active.

This module is source-bound to the supplied US Smash Pack V1.002 native boot
file and VC2 kernel. Its resident map has 48 slots. The console reported a
46-slot map, which the previous 44-slot test incorrectly refused. The complete
40-track TOC is retained; the remaining slots describe data-file fragments.
Maps above this test budget are rejected before native execution; this is not
a new general mapping limit.

The 48-slot correction keeps the low guarded stack at 352 bytes. The two
initialization-only callback tables are copied by value into live state before
their original bytes become SCI receive buffers. Linked-reference auditing and
tests that overwrite the source tables check this lifetime explicitly. The
high initializer retains separate stack space and its C argument-home area.

Build `fd4c92ad2c3c` subsequently stopped on the ordinary single-page GD STOP
screen with WHY `20A`, full-reservation CRC `44242a59`. This precedes the high
two-page checkpoint. It establishes that staged memory changed during native
startup, but cannot distinguish immutable-code damage from mutable-tail use.
The revised gate checks every immutable byte, then its verified P2 entry clears
the complete working tail before its first stack save. Changed code still
stops; arbitrary native scratch contents never become helper state.

## Reservation and publication

The high stage verifies the outer executable and waits until bootstrap 2 has
completed before copying the prepared helper into `8cfc4000..8cfd0000`. This is
unowned staging, not an early heap reservation. The native program may overwrite
it. No staged instruction executes during native startup.

At the exact original MAINANA FN8 caller, the trusted low gate requires the
caller's IMASK15, MMU off, supported cache index mode, and idle/unarmed hardware
DMA. It publishes candidate P1 cache lines across all 48 KiB, then checks every
initialized immutable byte through P2. Changed code is refused. The admitted
P2 entry clears the complete remaining tail through P2 before saving anything
on its high stack or reading helper state; the immutable prefix is untouched.

The admitted high initializer uses a separate stack. Its native-ABI service
adapter verifies the complete CMD28 prefix/body transfer, token and released
SCI state. The initializer checks the original prefix and full loaded kernel,
ROMHDR, Maple records/worker and zero padding. It excludes the helper from
WinCE allocation, extends the existing stored text, copies the authored mapped
bridge and publishes the worker branch last. Kernel writes use P2 with explicit
code-cache publication. Failed publication restores the original kernel words.

Only successful preparation sets the previously unused GD ops context marker.
The mapper then rejects explicit main-RAM destinations overlapping the owned
helper, including DMA aliases. CPU virtual pointers retain their separate
meaning, VRAM remains supported, and the upper OEM globals remain accessible.

VC2 selects reviewed NK/process, extension and function geometry in the shared
Maple sampler. All repeated mapping, cache, packet, IRP and DMA-fence checks
remain enabled. The original CE completion runs once in its original mode on
the ordinary CE stack, with a protected 16-byte argument-home area.

## Diagnostic key

The low GD STOP page's AUX, VALUE and WHY fields identify admission failures:

| WHY | Meaning |
| --- | --- |
| `201` | Original caller was not fully IRQ-masked; VALUE is its SR |
| `205` | MMU, cache-index or DMA hardware refusal; AUX is the exact register and VALUE its contents |
| `20A` | Immutable helper CRC mismatch; VALUE is the observed code CRC, AUX is its first instruction word |
| `20B` | High service admission refusal; VALUE is the returned reason (`203` stream geometry, `204` SCI ownership) |
| `20D` | Ownership-marker write/readback failed |

The two high pages retain checkpoint/phase, traffic/fence/A2 counters, all first
fault tuples, owner and CPU context, request/reply witnesses, kernel preparation
CRC/words and reader state. Preparation failures after service admission use
these same pages with phase zero and reasons `301..304`.
The ordinary low GD STOP screen has one page. It appears before the high
diagnostic can run, so waiting on it does not reveal a second page.

## Verification and limits

Build with the existing SDK environment and `make -f Makefile.vc2-ce-vmu`.
Run `python3 tools/check_vc2_ce_vmu_package.py build/vc2-ce-vmu` for actual linked
layout, integer instruction, source embedding, CRC and compiler-stack audits.
The mapped bridge test executes its actual SH instructions with adversarial CE
callees. Source tests use the privately supplied original kernel; it is never
included in the repository or package. Gate/service, publication order,
rollback, protocol, mapping and ownership failure tests run on the host.
The linked original-MAINANA transition test poisons the entire mutable tail,
then checks the actual entry clears it before any high stack save, preserves
the immutable prefix and neighboring canaries, and retains all incoming
arguments. It also poisons all 16 C argument-home bytes before normal startup.

The original MAINANA tail fixture proves bounded CPU cleanup to NK. It does
not establish inherited DMA/cache hardware state; the runtime gates test that
state directly. Padding has no detected direct references. The reviewed NK ROM
text path maps readonly, pre-relocated source, but exclusion of every possible
later relocation path has not been established. This experiment establishes
neither all WinCE-family compatibility nor persistent saving. The next step
after a successful readonly test is a foreground SCI storage loan and durable
write integration.
