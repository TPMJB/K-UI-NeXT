# Native SCI asynchronous reader: integration plan

Recorded 2026-10-02. This is a source/layout review and implementation plan,
not a native-game implementation or a new console result. No native reader,
game interrupt routing or CE support is changed by this document.

The [first complete autonomous-read console pass](sci-async-repeat-read-pass-2026-10-02.md)
establishes repeated CMD17 receive DMA, checked bytes, CPU overlap, the SCI
module-reset handoff and ordinary-read recovery in the isolated runtime probe.
The immediate gate remains the reusable runtime reader and varied-sector
stress client, including an independent timer/scheduler heartbeat. Native game
service-call cadence has not been measured. Neither gate should be replaced
by a throughput prediction.

## Current native contract

`retail_resident.S` saves the caller's SR, applies `0x100000f0` (BL and IMASK),
checks the entry lock and guard, and switches to a single private stack. It
restores the exact original SR, PR and ABI callee-saved GPRs on return, without
changing register banks or using the FPU. All native service work currently
runs synchronously within that protected entry.

The installed service routes are the BC supervisor vector at `0x8c0000bc`,
the C0 raw GD vector at `0x8c0000c0`, and redirected firmware entries at
`0x8c001000` and `0x8c0010f0`. Preserve their existing calling conventions:
C0 ignores R6; the other entries acknowledge R6=-1 setup calls. Keep the
original menu forwarding behavior for menu commands other than return.

`retail_gd.c:execute()` calls `ops.read()` synchronously and treats every
nonzero return as an I/O error. It publishes completion for a whole successful
chunk. `retail_image.c:file_read()` walks extents and fills one aligned cache
block synchronously. `retail_storage_impl.h` acquires the bus, uses CMD18
streams, closes every stream and releases the bus before returning. A new
physical begin/poll API cannot be substituted for this callback without
changing the image and GD state machines.

## Smallest scoped native candidate

Implement an opt-in, SCI-only, **polled CMD17 receive-DMA backend** first.
There is no new game VBR patch, KOS callback or synthetic GD interrupt in this
candidate. Its purpose is to establish correct return to game execution while
one physical transfer remains active, then measure whether the game's call
cadence can sustain useful throughput.

### Physical operation and ownership

Use resident-owned persistent operation state and a persistent aligned receive
area. Receive 514 bytes, including the two wire CRC bytes, into at least 544
bytes of exclusive cache lines. The existing image object already has an
aligned 512-byte cache; expanding its data area requires 32 additional bytes,
before any new guards or operation state. Do not add the diagnostic's second
baseline buffer to the game resident. Do not point an outstanding DMA at the
service stack or a caller-owned temporary allocation.

The proposed contract is:

| Operation | Contract |
| --- | --- |
| Begin | Validate the card LBA, buffer and idle state; acquire an exclusive SCI/channel-1 lease; start bounded CMD17 framing and eventually receive DMA; return pending without waiting for payload completion. |
| Poll | Perform a bounded amount of framing or completion work; return pending, verified success or a latched error. Never busy-wait through the whole payload merely to make one call succeed. |
| Finish | After owned `TE && count==0`, stop reception, verify CRC/guards, restore usable framing state and publish the checked cache block. |
| Cancel | Stop new framing, remove owned request sources, and release only after safe completion/ownership is proved; otherwise retain the persistent receive area and quarantine the transport. |

Disable the channel completion interrupt (`CHCR.IE=0`). SCI `RIE` is still
needed to generate receive-DMA requests, so temporarily set **only the SCI
interrupt priority field to zero** during the lease. Otherwise the trailing
SCI error or receive request could enter an unowned game handler. Validate
idle SCI state first; preserve and later restore the original SCI priority
and all unrelated priority-register fields. Do not change the DMAC group's
priority, other channels, DMAOR's global state, SCIF or timer configuration.
Recheck expected SCI/DMA ownership on each resumed operation and before
restoration; never overwrite resources that the game has changed.

Keep the native hook's exact SR restoration. Start or inspect the operation
inside its short protected section and then return normally to the game.
Do not clear BL or unmask interrupts while using the resident's private stack.
The existing entry lock protects C state during each call; a separate physical
operation state must prevent a second request from claiming an in-flight
buffer after the lock is released.

After confirmed completion, preserve the successful probe's stop order,
CRC checking, buffer guards and bounded SCI-only MSTP0 reset for a validated
trailing overrun. Do not perform SCI MMIO while its module clock is stopped.
Failure to prove module resumption or DMA quiescence remains restart-required;
it is not an ordinary completed read with a warning.

The game is not guaranteed to call the service again promptly. Without a
separately owned timing/interrupt facility, this polled candidate can promise
bounded per-call work and finite poll/work budgets, not an autonomous
wall-clock timeout while the title stops calling. Retain the receive storage
until completion or restart even in that case. Do not rely on ORER stopping
the serial clock.

Use CMD17 initially. The established receive-only experiment permits trailing
clocks after a single block; during CMD18 those clocks could consume the next
token or payload. Reusing the old multi-block callback would therefore change
the protocol safety argument.

### Resumable image cursor

Add a cursor with explicit idle, framing, receiving, checking/copying, complete
and failed states. It retains the request's guest sector/format, destination,
remaining output, track/extent position and physical-block offset. Keep the
validated manifest and storage lease stable for the outstanding operation.

Preserve full request preflight before I/O. Resolve extents with the current
range checks; never cross track gaps or consume allocation padding as image
data. On a cache miss, begin one physical block and return pending. On a later
call, verify that block before copying any of its bytes to the game, then
consume cache hits/copies under a bounded CPU-work budget until the next miss
or a completed guest sector. Invalidate the cache before starting a DMA and
after any failed block; perform cache maintenance on the whole exclusive
receive allocation, including the CRC/padding line.

Mode1 still checks the raw sector's 16-byte sync/mode header before exposing
its 2048-byte payload; RAW still copies the full 2352 bytes. No new 2352-byte
bounce buffer is required. Since 2352 is a multiple of 16, each raw header
starts at a multiple-of-16 offset within its 512-byte physical block, so its
16 bytes fit within that block and can be checked directly in the verified
cache. Extent boundaries must still be handled independently.

Keep guest P1/P2 range validation and protected-resident exclusion. Do not
retain a pointer to the old launcher or to an expired caller frame. Apply
destination cache maintenance when about to publish checked bytes, without
re-purging the entire request on every pending poll. The existing contract
permits partial output on a later I/O error; completed-byte accounting must
nevertheless describe only fully completed guest sectors.

### GD request/completion and cancellation

Extend the backend contract to distinguish pending, success and error; do not
reinterpret the existing synchronous callback's nonzero result globally.
SCIF/IDE and host clients retain their existing behavior unless they explicitly
bind the new contract. `EXEC` advances the native async cursor. A pending-read
`CHECK` may also pump the adapter before the ordinary status/acknowledgment
path; document that extension rather than leaving the current "CHECK never
reads storage" assertion in place.

Keep one outstanding token. A second request is rejected while it is pending.
Advance `completed_bytes`, `sectors_read` and disc position only after complete
guest sectors have been published; a pending DMA is not completion. Preserve
the one-time terminal CHECK acknowledgment and subsequent NOT_FOUND behavior.
Keep metadata commands, TOC/subcode behavior, drive state, callback rejection
and all existing service entry conventions unchanged.

`ABORT`, `RESET`, `INIT` and menu return must clean up the physical operation
before clearing or reinitializing its GD state. Preserve already completed
sector accounting for ABORT. Do not zero the operation, release a buffer or
start another request while an old DMA could still write. A partial DMA that
cannot be proved safely stopped enters the existing fatal/quarantine policy;
normal reset must not erase that fact.

### PIO and synchronous fallback boundaries

GD PIOREAD and DMAREAD are both token/EXEC/CHECK requests in the current
virtual service. PIOREAD does not by itself require that the physical storage
transfer block the CPU; both can use the resumable backend while preserving
their existing guest alignment requirements and completion behavior. Neither
one gains a virtual GD interrupt merely because SCI physically uses DMA.

The temporary high stage retains its accepted synchronous reader for loading
IP/bootstrap/executable data. It does not need to carry the new native state
machine, and keeping its code does not require retaining the old synchronous
pipeline in the low resident.

If a synchronous wrapper around the new engine is needed, it must explicitly
drive begin/poll/finish within a finite budget and retain the same CRC and
quarantine rules. That wrapper is CPU-blocking and must not be described as
the gameplay async path. A programmed-byte fallback is permissible only
before an uncertain transfer starts, or at a proven idle protocol boundary;
never switch transfer methods halfway through a sector. Its code and timing
cost must be measured before including it. A launch-time choice of the
existing stable resident is another fallback, requiring an explicit package
selection contract; this plan does not assume an in-game resident swap.

## Proven existing layout and replacement budgets

The delivered `0e9a2f814231` SCI resident has 11,156 payload bytes and ends at
`0x8c00baec`, leaving 20 bytes before `0x8c00bb00`. Its conservative stack
audit is 1,172 of 1,232 available bytes, leaving 60 bytes. Keep the linker
boundary, stack guard and instruction audit. DOA2 writes its startup marker
over `0x8c00c000..0x8c00f3ff`; moving the reader or its stack upward into that
range is not a memory-reclamation option.

Symbol measurements used the cached SH-linked
`native-crc-inline-next/build/retail/resident-sci.elf` and its matching binary.
The ELF payload was checked byte-for-byte against that binary. The 11,156-byte
binary was then compared with the resident embedded at absolute byte offset
35,844 in the delivered `KUI/apps/games/retail-boot.kui`. Only 11 bytes differed,
all within the 12-character build-ID string at resident offsets 10,358..10,369;
the cached ID is `000000000000`, the delivered ID is `0e9a2f814231`. Code,
layout and every other payload byte match. Relevant SHA-256 values:

- Cached resident binary: `19ab4020a6669b24c417e08334974644664d4ce758fec23817a7d5146d66619a`.
- Embedded delivered resident: `4ae3284bac1927bfaa7fbf50de97afffd5edee16376f91c710ec76e985d00376`.

`sh-elf-nm -S` on that ELF supplies the following replacement budgets:

| Existing retained code | Bytes |
| --- | ---: |
| `multi_active` | 40 |
| `multi_ready` | 112 |
| `multi_command.constprop.0` | 244 |
| `stream_finish` | 176 |
| `kui_loader_sd_stream_next` | 424 |
| `read_run` | 444 |
| CMD18/read-run subtotal | **1,440** |
| Synchronous `transfer_block` | **816** |
| `file_read` | 324 |
| `kui_retail_image_read` | 376 |
| `read_sectors` | 248 |
| Image/read wrapper subtotal | **948** |

These are sizes of code that the async implementation would replace, **not
net savings or a demonstrated final fit**. New framing, reset, CRC, ownership,
cursor and cleanup code still has to fit. For an SCI async build, omit the
old CMD18 path and synchronous DMA implementation instead of retaining two
complete read pipelines. Explicitly remove an unused `transfer_block` callback
binding if its address would otherwise keep the old implementation alive.
Keep the original implementations for other residents and the high stage.

The full resident manifest occupies 2,164 bytes; its 128 extents occupy
1,536 of those bytes. A separate compact resident extent representation can
save exactly **512 bytes of extent data** without reducing the supported
extent count:

| Representation | Fields per extent | Total for 128 |
| --- | --- | ---: |
| Current | `file_block`, `card_lba`, `blocks`: three 32-bit words | 1,536 |
| Proposed | `file_end_block`, `card_lba`: two 32-bit words | 1,024 |

For the first extent of a track, start is zero; for later extents, start is
the preceding extent's end within that track. Length is end minus start.
Binary search finds the first end greater than the requested file block,
then translates using `card_lba + file_block - start`. The high stage must
validate monotonic exact coverage and overflow before creating this immutable
representation. Keep track boundaries, all 128 extents and card-range checks.
Do not silently alter the existing serialized 4096-byte manifest format or
reduce its limits. Code/alignment costs can reduce the eventual net saving.

## Throughput gate and later interrupt work

One 512-byte receive advanced per service call has a maximum payload rate of
`0.5 * calls_per_second` KiB/s. At one call per 60 Hz frame that is **30 KiB/s**,
before framing/check work. Returning CPU time does not by itself overcome
this scheduling limit. DOA2's actual pending-read EXEC/CHECK cadence remains
unmeasured; the existing video-pacing counters do not establish it.

Record calls/polls while pending, calls per observed frame, completed physical
receives, and completion-to-next-start gaps. Distinguish frame-paced gameplay
from a loading loop that repeatedly polls. Repeated CHECK polling must make
progress without needing an EXEC, but no extra polling may be invented on
behalf of a title that does not call the service. Do not promise the remaining
fight-start slowdown will improve from the polled candidate alone.

If frame-paced polling starves throughput, continuous operation needs an
independently audited interrupt/deferred-progress mechanism. Its prerequisites
include ownership of the actual game exception entry, complete interrupted
register/SSR/SPC/register-bank preservation, safe stack and reentrancy rules,
handling only owned events, forwarding all unrelated original game interrupts,
and bounded command/check/copy work. A completion-only ISR that waits for the
next once-per-frame service call still has the scheduling limitation. Do not
transplant runtime KOS IRQ callbacks or silently take a game timer.

The CE boundary remains as recorded in
[the CE polling review](windows-ce-polling-review-2026-10-02.md): original
GD/Holly completion semantics, MMU address roles, virtual-stack accesses and
BL/exception behavior are additional requirements. A native SCI polled reader
does not satisfy them or authorize removing the CE launch gate.

## Next implementation files and acceptance checks

1. **New freestanding SCI backend under `src/loader/`**, with a small header:
   resident-owned begin/poll/finish/cancel state, channel/priority lease,
   exclusive receive area, CMD17 framing, checked handoff and quarantine.
   Factor reusable original register/protocol helpers out of the runtime code
   only where their platform and lifetime contracts truly match; retain
   separate runtime KOS and native-polled adapters.
2. **`src/core/retail_image.c` and `include/kui/retail_image.h`**: explicit
   cursor and pending result, verified-cache publication, track/extent and
   Mode1/RAW continuation tests. Add a distinct compact resident-map type if
   the linked budget requires it; convert validated data during the high-stage
   handoff without changing the on-card wire representation.
3. **`src/core/retail_gd.c` and `include/kui/retail_gd.h`**: optional async read
   contract, token/completion accounting and cancellation hooks. Test repeated
   pending CHECK/EXEC, completed-sector accounting, all reset/abort paths,
   unchanged metadata commands and existing synchronous clients.
4. **`src/loader/retail_resident.c`, `retail_storage_impl.h` and transport
   selection**: SCI-only binding, short service pumping, pending-read call
   counters, cleanup before menu/reset and no old-launcher pointers. Keep
   `retail_resident.S` entry/return semantics unchanged for the initial polled
   candidate; verify its machine code and caller-state tests.
5. **`Makefile.dc`, `tools/check_retail_loader_layout.py` and relevant tests**:
   remove obsolete required CMD18 symbols only for the new SCI implementation,
   require its actual async entry points and stack reports instead, and retain
   the original memory/stack limits, forbidden runtime-symbol checks and
   linked no-FPU audit. Report real linked size; do not infer fit from the
   replacement budget.
6. **Console sequence**: first accept varied-sector runtime stress and recovery;
   then a clearly identified native opt-in candidate with menu-return counters,
   CRC/error evidence and DOA2 call cadence. Compare load time, first-fight
   responsiveness, FMV/audio, input and return behavior. Advance to sustained
   interrupt-driven operation only if cadence measurements require it and
   after its separate ownership/exception design is tested.

All proposed implementation is original K-UI work. Hardware and interface
behavior may inform the design; no SWAT/DreamShell implementation is copied.
