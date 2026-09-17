# mstats and RAM comparisons

Pull the **left trigger** to run the `mstats` action on either page, including
during capture. It samples counters and appends them to the in-memory log; it
does not read the disc, write the card, or allocate test buffers. The capture
page also samples RAM once per second and displays used/reserved and the sampled
peak. Startup and capture/verification start/end record snapshots automatically.
After stopping or completing, switch to diagnostics and press Y to save the log.

There is no command-line shell in this milestone. `mstats` is the named
controller action, using the pinned KOS allocator's `mallinfo()` counters.

| Logged counter | Meaning |
| --- | --- |
| RAM total | KOS-detected main RAM size, normally 16 MiB |
| RAM used/reserved estimate | Main RAM minus unclaimed heap growth space and reusable free heap blocks |
| RAM available estimate | Unclaimed heap growth space plus allocator free blocks; not a guarantee of one contiguous allocation |
| RAM sampled peak used/reserved | Highest estimate observed since runtime startup; periodic sampling can miss brief peaks |
| Image+BSS | Linked K-UI/KOS program, globals and static buffers, including alignment up to the heap start |
| Firmware reserve | First 64 KiB below the executable at 0x8c010000 |
| Main stack reserve | KOS's fixed kernel/main stack reservation at the top of RAM |
| Heap system bytes | Current allocator arena (`mallinfo.arena`) |
| Heap in use bytes | Allocator in-use bytes (`mallinfo.uordblks`), including worker stacks and allocator overhead |
| Heap free bytes | Reusable free bytes inside the arena (`mallinfo.fordblks`) |
| Heap max system bytes | Historical peak allocator system reservation (`mallinfo.usmblks` in this pinned KOS dlmalloc); not peak live allocations |
| Unclaimed main RAM | Space between the current heap break and the main-stack reservation |
| Video framebuffers | Active mode's framebuffer count × size, in separate VRAM; excluded from main-RAM totals |

The main-RAM estimate includes static capture buffers even when idle. It does
not add the separate 8 MiB video RAM or 2 MiB sound RAM. The 64 KiB I/O worker
stack is allocated from the heap and is already counted there; it must not be
added again. Stack reservations do not measure deepest actual stack usage.

For comparison with DreamShell, save snapshots at idle, during track-3 capture,
and during saved-file verification, using the same console/card/disc. Compare
its allocator `system bytes`, `in use bytes` and `max system bytes` to the
corresponding **heap** counters here if that is what its command reports. Do
not compare a heap-only number to K-UI's whole-main-RAM estimate. Record the
build IDs and read mode; this first capture uses paired PIO reads.

Implementation basis: pinned upstream KOS `kernel/mm/mm.c`,
`kernel/libc/koslib/malloc.c`, `kos/linker.h` and `arch/stack.h`. `mallinfo()`
holds KOS's allocator lock. Sampling occurs outside the UI/log lock, and checks
the heap break before/after the allocator snapshot. The peak has a separate
mutex. These are software accounting estimates, not a physical memory scan.
