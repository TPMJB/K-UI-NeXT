# Experimental VMU service

`kui_vmu_service` joins the existing Maple card protocol, redo-journal store and
fixed-extent SCI backing into one freestanding, caller-owned object. Its frame
entry point is suitable for a future validated game bridge. It does not install
that bridge, advertise a card to retail games, or reserve game RAM by itself.
The ordinary 2.0 loader does not invoke this service.

The implementation adds no second protocol parser and allocates no heap or
128 KiB card mirror. A compile-time assertion limits the complete service work
state to 4 KiB. This bound excludes caller-owned frame buffers, SD reader/card
state, callback code and stacks. All of those still need proven resident
placement and game lifetime ownership before retail integration.

## Preparing and using the object

1. Follow [vmu-backing.md](vmu-backing.md) to prepare a fully allocated image,
   journal, persistent identity, independently verified exclusive physical file
   map, card CID and shared SCI bus hooks. The filesystem must no longer write
   the mapped allocations when raw access begins.
2. Zero the service object, then call `kui_vmu_service_init` before game handoff.
   Supply a valid Maple unit address and a writable configuration. A resident
   `healthy` hook is mandatory: it must report faults from SCI lease restoration
   as well as transfers while the shared lock remains held.
3. Initialization copies configuration, acquires one whole-bus reservation,
   recovers the original journal/image pair, releases its lease, and only then
   enables frame handling. Temporary maps/configuration may expire once this
   call returns. The service, bus context, card, streams and all callback code
   must remain at their original resident addresses. Moving or copying the
   initialized service invalidates its self-referencing protocol callbacks.
4. Pass complete Maple frames to `kui_vmu_service_handle`. Its ordinary results
   match `kui_vmu_emulation_handle`; additional negative results distinguish an
   uninitialized service and recursive entry. Calls must be externally serialized
   with the game's Maple consumer. This recursion guard is not a cross-thread
   synchronization primitive.

An initial reservation rejection returns `KUI_VMU_SERVICE_BUSY` during init or
Maple `AGAIN` during read/commit. The store is never entered in that case, so it
remains usable and the exact request can be retried. The service returns a
surviving standard CMD18 reader lease before acquiring its own. A successful
read or commit reserves the shared lock, interrupt mask and SCI lease for the
entire store operation; individual journal callbacks cannot lose ownership
between barriers. The backend still validates card identity on each callback.

## Acknowledgements and failures

The four 128-byte Maple write-phase acknowledgements promise receipt in the
protocol buffer. The final phase-four sync acknowledgement requires all of:

- A successfully reserved bus before entering the store.
- Durable redo payload, commit marker and base block, with the store's ordering
  barriers and SD programming/status checks intact.
- Successful reservation cleanup, including health checks before **and after**
  the SCI release callback, while the shared lock/interrupt mask remains held.

A fault observed only during release therefore withholds the final success even
if every sector operation succeeded. The save can already be durable; controlled
recovery determines that outcome. A repeated final sync after a previously
successful acknowledgement returns that same durable result without rewriting
it. Persistence is atomic per 512-byte VMU block, rather than across an entire
game save containing several blocks.

Any storage, identity, recovery or release failure latches `service.faulted` and
blocks its store. Subsequent reads and new commits return Maple `FILEERR`
without storage I/O, including after Maple `RESET`. New phases may still be
received in the protocol staging buffer; they cannot commit while faulted.
An already initialized or faulted object cannot be initialized again. A fresh
object may be used only after controlled card initialization and recovery of
the original image/journal pair. There is no in-game retry, card reinitialization,
filesystem remount or automatic fallback to another storage device.

`last_store_result` and `last_backing_result` retain the most recent callback
result for diagnosis; the latter preserves the first transport failure instead
of replacing it with a later cleanup `POISONED` result. They are diagnostics,
not permission to clear the fault latch or resume the abandoned session.
Invalid initialization arguments fail before I/O and do not latch a storage
fault. Recursive initialization/handling cannot disturb the outer owner.

Holding interrupts across a journal transaction can stall frames during SD
programming. The host tests establish ordering and bounded failure handling,
not retail Maple deadlines or acceptable game latency. Async readers, CDDA
workers and WinCE remain excluded by the experimental backing's compile-time
guards.

## Verification

`tests/test_vmu_service.c` drives only the production service frame API over the
same independent SPI card model as `test_vmu_integration.c`. It uses the actual
SD reader and writer, command/data CRCs, deferred programming/busy completion,
fragmented image/journal maps and shared-bus ownership assertions. It verifies:

- Every one of the 256 card blocks, durable final acknowledgement, lost-reply
  sync retry, readback, reboot recovery and protection of unrelated sectors.
- Init/read/commit contention before store entry; successful later retries.
- Every journal sector-write and status failure, torn writes, bounded final busy
  timeout, CID replacement, read failure and initial recovery failure.
- Faults appearing during the release callback, including initial recovery
  cleanup, with no premature final acknowledgement.
- Fault persistence through Maple reset, copied configuration lifetimes,
  surviving CMD18 reader cleanup and recursive init/frame rejection.
- Response-capacity failure before any storage I/O and invalid initialization
  rejection before touching the bus.

Build with the same host ASan/UBSan flags as the integration test and
`KUI_VMU_EXPERIMENTAL=1`; `build/test-vmu-service` is included in the prototype
suite. The standalone console harness currently tests protocol/store through
owned file I/O; this raw backing service has separate host coverage and has not
yet been admitted into a retail game or tested electrically on the console.

All code and the SPI model are independently authored within K-UI; no DreamShell
loader implementation was used.
