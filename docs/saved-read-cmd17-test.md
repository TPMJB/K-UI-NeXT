# Saved-repair single-sector read candidate — @BUILD@

The previous console build read all 6,190 saved candidate records successfully
using both PIO profiles and bulk DMA. All three full passes had the same
SHA-256, matching the preceding build's bulk pass. The 512-byte aligned
profile then timed out after an accepted one-sector CMD18. The SCI remained
healthy; the report did not show a DMA engine fault.

This candidate uses the existing CRC-checked CMD17 single-block command for
one-sector requests through the GUI's SD media adapter, including filesystem
reads. Larger request chunks keep CMD18.
DMA, the 12.5 MHz clock, and the comparison order are unchanged. There is no
automatic retry or reconnect after an I/O failure. This is a targeted hardware
test candidate, not a confirmed fix for every earlier CRC or resume failure.

## Install

With the console powered off, mount the SD card on the PC. Extract the ZIP
into a new folder. From the folder containing `install.py`, run:

```sh
python3 install.py /run/media/apollonius/Dreamcast
```

Use the actual mount path if different. This replaces only `KUI/runtime.kui`,
checkpoints its predecessor, and preserves native game modules, VMUs,
preferences, and salvage jobs. Use the same command with `--restore` to roll
back. The installer is directly at the ZIP root.

## Run the same comparison once

Keep **Power Stone** inserted. Open:

**Disc tools → Rip a disc → START (Advanced) → Damaged disc salvage → Damaged
disc controls → Check saved repair reads**

It reads the same saved candidates in the same order: bulk unaligned PIO,
512-byte unaligned PIO, bulk aligned, then 512-byte aligned. Aligned buffers
still use DMA where eligible. File metadata may use other buffer shapes.
The report logs the policy as **SCI filesystem reads: CMD17 for one block;
CMD18 for multiple blocks**. Explicit low-level diagnostic streams retain their original
protocol.

Wait for the automatic report to save, then send the newest `diagnostics.txt`
under `/KUI/probes/`. Send any newly created `saved-read-....bin` files too.
No photo or G1 test is needed. Stop remains available; the five-minute soft
budget finishes the active bounded read before returning.

Keep recovery paused and leave `/KUI/salvage/job-0003` and its quarantine
intact. The comparison does not modify saved repairs or validate the complete
journal and tracks. Even four matching passes do not clear quarantine.
