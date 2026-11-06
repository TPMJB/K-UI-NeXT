# CE GIN terminal diagnostics use the validated K-UI video seed

The original GIN continuation owns the collection's current scanout. The cold
CE stage captures that current video state and then keeps successful adoption
quiet. It previously passed the captured collection mode to the low reader and
later high VMU helper for terminal reports.

The terminal renderer has a narrower contract: it restores the supplied video
registers but draws640-pixel RGB565 rows at framebuffer0 (`A5000000`). An
inherited320-pixel mode or nonzero front-buffer base violates that contract.
For example, restoring a front buffer at1MiB while drawing at0 leaves the
visible frame unchanged. A real loader failure can therefore look like a
silent freeze or an incorrect image instead of showing its recorded error.
This is an evidence-visibility defect, not a demonstrated cause of the game
failure.

The collection already carries an independently validated `VID1` record from
K-UI's post-shutdown640x480 RGB565 mode. `smash_ce_gin_begin()` now snapshots
that volatile record once, validates its checksum and exact display shape,
then copies only its register data to the stage's saved diagnostic state.
This happens after the existing original GIN/frame and retained IP-header
checks, while adoption is quiet, and before the resident receives the saved
display state. The later prepared return record retains its own existing
validation.

Successful adoption performs no additional PVR or VRAM operation. The actual
game mode, original GIN frame/IP state, disc/VMU scheduling, helper code,
resident code and return controller remain unchanged. The local80-byte
snapshot lives on the cold stage's stack; it adds no live reservation.
Invalid video seeds refuse before publishing a replacement saved state.
Profiles without the collection return descriptor retain their previous
behavior.

The current VC2 helper retains its actual91fa component identity and complete
41152-byte SHA256, including that build's GD arbitration correction. Swirl
retains its unchanged ab1 component. Rebuilding an unchanged helper with a new
stage ID can change string merging/linker relaxation and move its code. The
normal helper recipe therefore reproduces each current component's real ID
and requires its exact bytes; the historical ab1 guard remains unchanged.

`tests/test_smash_ce_diagnostic_video.py` executes the actual early function
from the source, changing only its SH SR-read and native GIN/IP address
boundaries for the host. The real GIN/VID1 validators, CRC and terminal
renderer execute. During quiet successful adoption, PVR/VRAM are unmapped;
the same renderer is then exercised against host-mapped registers/VRAM.

The test covers both CE child profiles, progressive/interlaced canonical
modes, an inherited320-pixel/nonzero-frontbuffer state, bad checksum,
unsupported shape/base/bounds, descriptor size/reserved fields, original GIN
refusal and an adversarial descriptor change after validation. The copied
bytes must remain the checked snapshot. An optional `--negative-source`
argument runs the actual preceding GIN function and demonstrates its
invisible fault frame. No original retail game bytes are used or emitted.

The existing `tests/test_smash_ce_gin_display.py` quiet-renderer sanitizer
check still passes. These checks model CPU state and MMIO; they do not prove
that an observed hardware freeze reached a terminal report. No independent
post-NK watchdog or blocked-thread PC capture is introduced here. If the
failure occurs outside these reporting paths, this change alone cannot
produce new evidence.
