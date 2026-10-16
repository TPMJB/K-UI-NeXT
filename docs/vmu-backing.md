# Experimental VMU SD backing

`include/kui/vmu_backing.h` and `src/loader/vmu_backing.c` adapt the fixed-extent
VMU journal store to the existing freestanding SD reader and K-UI's independently
authored `src/dreamcast/sci_sd_storage.c` writer. This is an optional synchronous
native SCI building block. Ordinary game loaders do not invoke it.

Writes require **both** `KUI_VMU_EXPERIMENTAL=1` at compile time and
`allow_writes=true` in the backing configuration. Default backing objects do
not reference the SD write/sync functions. The ordinary SCI resident continues
to reject TX block transfers. Experimental resident TX uses the existing
bounded programmed path; it does not add a TX DMA buffer. Combining experimental
backing with the async, Windows CE, or Toy Pilot loader macros fails compilation.

## Preparation and object lifetimes

Before the filesystem goes away, the launcher must:

1. Create or select a plain 128 KiB VMU image and its exact two-sector journal.
   Recover the original image/journal pair before relocating either file or
   replacing its persistent nonzero image ID.
2. Fully allocate, sync, and close both files. Obtain their physical 512-byte
   sector extents from the actual mounted filesystem. Validate that the sectors
   are exclusively owned by those files, contain no metadata, and will remain
   allocated and stationary for the whole game session. Preallocation and
   disjoint extents alone do not establish exclusive ownership.
3. Record the partition's first sector and length, the independently initialized
   card's capacity/addressing mode, and the actual 16-byte CID obtained with
   `kui_vmu_sd_identity_read`. That helper requires an already acquired bus and
   no active read stream; it issues only CRC-checked CMD10 and deselects afterward.
4. Initialize a backing object with the same store configuration that will be
   passed to `kui_vmu_store_open`. Optionally supply protected game or other
   allocation extents. At most 1,024 protected extents are accepted.
5. Stop other writers and retire/unmount the filesystem before permitting raw
   handoff writes. Rebind the initialized card and every callback to resident
   code and resident-owned state.

Initialization performs no I/O. It validates the image and journal's exact
sector totals, all pairwise overlaps, card/partition bounds, protected ranges,
the nonzero image ID, and supported SD addressing. It copies the card identity
and authorized extent whitelist. Temporary configuration, extent, and protected
range arrays may be discarded after successful initialization. The store
separately copies its own map and image ID.

The backing object, card, optional reader stream/lease flag, callback context,
and callback code must survive throughout gameplay. They must not point into
the departed shell, KOS state, a FatFs object, or a launcher stack. A memcpy of a
shell card object does not rebind its function pointers. Internal work fields
are private even though they are exposed for static allocation. I/O buffers
must not alias the backing object.

The exported `kui_vmu_backing_io` callbacks use absolute 64-bit card LBAs to match
the journal store API. The backend rejects addresses outside its copied image
and journal whitelist, including values above `UINT32_MAX`, before issuing I/O.
SDSC cards additionally retain the reader/writer's byte-addressing bounds.
No filename lookup, allocation, filesystem metadata update, or fallback to a
different device occurs after handoff.

## Card identity and bus ownership

The external `enter` hook must atomically acquire the **same** storage lock used
by all game readers and mask interrupts. Its `leave` partner restores the exact
previous state. The backing does not invent an independent lock that other
readers can bypass. Callback execution is synchronous and externally serialized.
All hooks must be bounded and freestanding; none may invoke KOS, FatFs, a heap,
a scheduler, or code retired with the shell.

After entering, the backend stops a supplied active standard CMD18 stream with
the existing reader's CMD12 cleanup, returns that reader's SCI lease, and only
then acquires its own SCI lease. A failed or unowned stream state fails closed.
An acquired bus is released on every ordinary callback exit, including errors.
An acquisition rejection does not release another owner's lease.

Every acquired lease checks card readiness, fast mode, capacity, addressing
mode, optional SCI fault state, and CRC-checked CID before a sector operation.
Reserved transactions also repeat these checks before each callback. A changed
CID blocks use of an old file map even when replacement media has the same
capacity. CID checking is an identity consistency check, not authentication
against hardware that deliberately clones a CID. Hot swapping during a lease
is unsupported.

## Reserving a whole journal transaction

The optional reservation API avoids entering a journal operation that can
encounter ordinary reader contention between its individual sector writes:

```c
int reserved = kui_vmu_backing_reserve(backing);
if(reserved == KUI_VMU_BACKING_BUSY)
    return KUI_VMU_EMU_IO_BUSY; /* Store has not been entered. */
if(reserved)
    return KUI_VMU_EMU_IO_ERROR;

int committed = kui_vmu_store_write(store, block, immutable_sector);
int released = kui_vmu_backing_unreserve(backing); /* Required after any result. */
return committed == 0 && released == 0
    ? KUI_VMU_EMU_IO_OK : KUI_VMU_EMU_IO_ERROR;
```

Successful reserve holds the shared lock, interrupt mask, and acquired SCI
lease until explicit unreserve. Sector reads/writes/syncs reuse that lease;
they cannot lose arbitration to a game reader between journal barriers.
Unreserve samples final health, releases exactly once, restores the caller's
state, and preserves any poison latch. It cannot run from inside an active
callback. Nested reservation or callback attempts return BUSY without unwinding
the outer owner. Neither reservation nor release retries a failed SD command.

The final Maple save acknowledgement must require **both** a successful store
commit and successful unreserve. A final health failure cannot be hidden just
because the last sector operation returned success. Releasing a failed
transaction is cleanup, not permission to resume its card session.

Holding interrupts across four sector writes and four explicit store barriers
can stall frames while an SD card programs. The existing SD operations bound
their byte work and waits, including when a timer stops; this does not establish
acceptable game or Maple response latency. Do not weaken journal ordering or
acknowledge volatile RAM to reduce that latency. Retail game timing, exception
placement, and memory placement remain separate work.

## Contention, failure, and the poison boundary

| Event | Backing state | Required caller behavior |
| --- | --- | --- |
| Initial reserve cannot enter or acquire | No save sector issued; not poisoned | Return a protocol retry before calling the store. |
| Unreserved store callback cannot acquire | Backing can remain usable; store treats nonzero callback result as I/O failure and blocks | Recover/reopen the store under exclusive ownership; do not turn that result into a deferred success or blindly retry a partial commit. |
| Nested reserved callback or release | Outer reservation stays owned | Treat as an owner/programming error; never release the outer lease from the nested call. |
| CID/capacity mismatch, stream-stop failure, failed read/write/status, or latched bus fault | Backing poisoned; `card.ready=false` | Withhold success; release any explicit reservation; do not retry or reinitialize from the game. |
| Unreserve detects a final health failure | Backing poisoned even if store commit returned success | Withhold the final acknowledgement and return to controlled recovery. |

After an uncertain write, later callbacks fail without touching the bus. There
is no automatic reset, retry, remount, or card initialization in the backend.
A new controlled shell session must initialize the correct card and recover
the original journal/image mapping before opening it again. Recovery can
legitimately reveal a committed save whose acknowledgement was lost.

## Durability and test limits

The existing writer sends CRC-protected CMD24/CMD25 data, checks the data
response, waits for programming busy to finish, checks CMD13 status, and
deselects before reporting success. Explicit sync also waits/checks status.
The backing submits one sector per store callback and leaves barrier ordering
to the redo store. See [vmu-store.md](vmu-store.md) for the marker protocol.

The tests establish behavior under the declared sector-device contract. They
do not independently prove that a particular consumer SD card's flash
translation layer preserves unrelated sectors during arbitrary power loss.
Protocol busy/status completion is the device's completion indication, not a
measurement of its internal power-loss protection. A host model cannot prove
Dreamcast electrical behavior, SCI timing, or game compatibility.

Verification includes:

- `test_vmu_backing.c`: default write rejection, immutable allowed ranges,
  bounds/overlaps/protected allocations, CID and capacity changes, exclusive
  ownership, stream cleanup, bounded identity waits, poison behavior,
  transaction contention, nested callers, and failure/release unwinding.
- `test_vmu_sci_bus.c`: actual experimental resident SCI programmed TX/RX
  through the established MMIO model, every length from 1 to 512 bytes, CRC,
  fault latching, bounded waits, and register restoration.
- `test_vmu_integration.c`: actual Maple protocol, journal store, backing,
  reader, and writer over a packet/CRC SD model. All 256 image blocks, fragmented
  maps, write/status failures at every transaction boundary, torn base writes,
  recovery after reboot, changed CID, stream cleanup, reservation contention,
  and suppression of successful final acknowledgements after failures.
- The existing SD writer and SCI suites, plus default-source comparison. With
  experimental writes disabled, SCI preprocessed source remains identical to
  the baseline for runtime and resident transport configurations 0, 1, and 2.

The standalone harness and host tests validate building blocks. They do not
establish that a retail game can discover or save to a virtual VMU yet.
