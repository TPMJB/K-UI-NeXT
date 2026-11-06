# CE return RAM lifetime

The3337 hardware result separates two failures. VC2 boots first after a power
cycle; after Swirl returns it reaches the AM2 logo and freezes white. VC2's own
return independently reports CE000004 with zero detail. Fresh startup and
Swirl's private return dispatch are accepted baselines, not candidates for
replacement with older components.

The original MAIGO return clears main RAM from8c010000 through8d000000.
Next-title SAMPLE clears only through8ce00000. Executing both original clear
loops confirms that the original return erases an additional2MiB which the
next startup preserves. The new cold return previously omitted the entire
main-RAM retirement clear. VC2 rebuilds its OEM allocator metadata but does
not clear the payload of its high-RAM allocations. No specific consumer of
retained payload has yet been established as the white-screen cause.

The candidate restores the original complete main-RAM clear on the terminal
CE path. Its order is:

1. Check controller/runtime, raw-reader and immutable-helper integrity; admit
   an idle helper and freeze its producers while interrupts remain masked.
2. Stop Maple retrigger, drain DMA owners, disarm PVR-interface DMA and wait
   for its state and the separate sort-DMA state to become idle. A busy or
   stuck engine refuses retirement before memory is replaced.
3. Retire caches and translation, then clear ac010000..ad000000 using the
   authenticated low controller and low stack. The integer leaf batches eight
   aligned words per loop, touches no device registers and uses no FPU state.
4. Read the outer capsule using the existing checked PIO gate, verify its
   complete CRC and blank slots, publish the low-carried ROOT/video/receipt,
   restore firmware and enter the new outer stack.

The clear cannot run on a BUSY retry or before the last helper integrity
check. No caller, stack, raw reader, return seed, original video or receipt
needed after the clear resides in the retired interval. No old CE code may
resume afterward. This does not erase SD saves or discard pending VMU writes.
The native return keeps its existing RAM lifetime; the independent DMA
admission improvement applies to the shared retirement routine.

The linked leaf proof checks every word store, both interval boundaries,
special registers and FPU preservation. The controller proof separately
checks ordering and models only that independently verified bulk effect.
Original source inputs remain external and hash-pinned. These are bounded
software checks, not a measurement of console return time or proof that the
white-screen failure is fixed.

VC2's zero-detail return also remains unresolved. The exact3337 packaged
failure-to-renderer chain preserves and draws nonzero masks, while the old
d04 chain draws zero. Package, embedded-module, helper and ROOT-selection
checks found no supported stale route. The next controller's CE01 reason
family independently identifies its executing trace revision; all existing
integrity predicates and detail masks remain intact. A new diagnostic is not
acceptance of VC2 return.
