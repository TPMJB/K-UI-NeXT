# Saved-repair read comparison, PIO first — @BUILD@

The previous console run validated all 6,190 candidate records with bulk
aligned reads, then stopped on a 512-byte aligned read with FatFs
`FR_DISK_ERR` and zero returned bytes. Cleanup replaced the initial error on
screen. The unaligned read profiles were never reached.

This diagnostic runs those missing profiles first and records the underlying
SCI error before cleanup or reconnection. It preserves the first fatal error
on screen, records request offset/size and actual close results, and logs SCI
DMA/PIO block counts for each profile. It does not change the SD command
protocol, clock, normal capture path, or native game modules.

## Install

With the console powered off, mount the card on the PC. Extract the ZIP into a
new folder and run from the folder containing `install.py`:

```sh
python3 install.py /run/media/apollonius/Dreamcast
```

Use the actual mount path if different. This updates only `KUI/runtime.kui`,
checkpoints its predecessor, and preserves native game modules, VMUs,
preferences and salvage jobs. Restore uses the same command with `--restore`.

## Run one test

Keep **Power Stone** inserted. Open:

**Disc tools → Rip a disc → START (Advanced) → Damaged disc salvage → Damaged
disc controls → Check saved repair reads**

The test selects the matching job through the existing healthy disc identity
checks, then reads its saved candidates. It runs the following order:

| Order | Profile ID | Read request | Candidate buffer |
| --- | --- | --- | --- |
| 1 | 2 | Bulk, 5,120 bytes | Unaligned: SCI PIO path |
| 2 | 3 | 512 bytes | Unaligned: SCI PIO path |
| 3 | 0 | Bulk, 5,120 bytes | 32-byte aligned |
| 4 | 1 | 512 bytes | 32-byte aligned |

Alignment labels describe the candidate requests. File metadata and evidence
I/O may use other buffers; the per-profile SCI counters include those reads
and any diagnostic evidence writes too.
`DMA_rx_blocks` counts completed DMA transfers before SD CRC acceptance;
`PIO_block_attempts` counts polled block attempts, including writes, and
`bus_failures` counts bus faults. These are not validated candidate counts or
a complete count of SD protocol errors; protocol failures are logged separately.

Each profile uses a fresh read-only handle. Invalid records remain failed
observations even if another profile later succeeds. Actual I/O, short-read
and close failures stop the test; it never reconnects or silently retries an
I/O error. Stop and the five-minute soft budget remain available, finishing
the active bounded read before returning.

Wait for the automatic report to save, then send the newest `diagnostics.txt`
under `/KUI/probes/`. Also send any newly created `saved-read-....bin` files.
No photo or additional G1 run is needed.

Keep `/KUI/salvage/job-0003` and its quarantine intact. This checks candidate
self-consistency and repeated reads, not the complete journal or tracks.
Even a fully matching result leaves repair blocked. This is a diagnostic
candidate, not a confirmed fix for the console read failure.

The completed direct G1 test already showed that all seven fixed-speed
requests returned speed code zero, with exact original-mode restoration.
The G1 probe remains available in the GUI but does not need repeating for
this SD investigation.
