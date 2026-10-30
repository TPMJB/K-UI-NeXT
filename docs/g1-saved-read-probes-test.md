# G1 settings and saved-repair read comparisons — @BUILD@

This GUI-only test adds two entries to **Damaged disc controls**. The existing
salvage job stays quarantined. Neither diagnostic retries damaged sectors,
opens job files for writing, or clears the quarantine.

## Install

After the current operation ends and its report saves, power off the console
before changing the SD card. Extract the ZIP into a new folder. `install.py`
and this README are directly inside it. From that folder, run:

```sh
python3 install.py /run/media/apollonius/Dreamcast
```

Use the card's actual mount path if different. This updates `KUI/runtime.kui`
and checkpoints its exact predecessor. Existing native game modules, VMU
cards, assignments, settings and salvage jobs are preserved. The same command
with `--restore` restores the previous GUI.

Boot with the same **Power Stone** disc inserted. Open:

**Disc tools → Rip a disc → START (Advanced) → Damaged disc salvage → Damaged
disc controls**

## 1. Check saved repair reads

Choose **Check saved repair reads**. Initial disc preparation and healthy
identity-anchor reads select the matching saved job. The comparison then reads
its SD files; it makes no unresolved-sector recovery reads.

Four profiles each open a fresh read-only handle to `patches.bin`:

| Profile | File read size | Buffer address |
| --- | --- | --- |
| Bulk aligned | 5,120 bytes | 32-byte aligned |
| 512-byte aligned | 512 bytes | 32-byte aligned |
| Bulk unaligned | 5,120 bytes | One byte after alignment |
| 512-byte unaligned | 512 bytes | One byte after alignment |

These request shapes exercise different filesystem and SCI transfer paths.
The unaligned SCI destination uses the existing PIO fallback; the labels do
not claim that every aligned request or metadata read uses DMA. All shapes
still use the existing SD command framing and sector CRC checks.

Each record undergoes format, identity, record/payload CRC and data-sector
checks. Complete valid observations also compare whole-file SHA-256. A bad
record ends that profile; later profiles can collect independent observations.
Any disagreement remains a failed consistency result even if a later read is
good. I/O errors, short reads, close errors, Stop and the five-minute soft
budget stop the comparison. The active bounded read finishes first.

The first invalid record from each profile is saved as a new
`/KUI/probes/saved-read-....bin` observation. Send those files if created,
together with the automatically saved `diagnostics.txt`. Failed evidence
writes are reported; an incomplete observation file is not trusted evidence.

Even four matching profiles do not authorize repair. This checks candidate
self-consistency and repeat reads; it does not validate the complete journal
or track files. Keep `job-0003` and its quarantine marker intact.

## 2. Probe direct G1 settings

Choose **Probe direct G1 settings**. This prepares the drive, obtains exclusive
G1 access, then sends Sega SPI `REQ_MODE`/`SET_MODE` packets directly through
the taskfile registers, bypassing BIOS mode commands during the comparison.

The probe tests Maximum and all seven documented fixed-speed codes. It changes
only the speed byte; the original retry count and every other mode byte are
preserved. Each request records the returned mode before restoring and checking
the complete original ten-byte mode. The BIOS tuple is checked again after
the raw bus lease ends.

This diagnostic issues no optical sector reads. A verified mode means the
request was retained in the returned mode page. It does not establish physical
RPM, real data throughput, or that damaged sectors are recoverable at that
setting. No drive firmware is flashed and no reset command is issued by the
raw comparison.

The probe refuses unsafe bus/interrupt ownership. If a packet cannot safely
finish, it retains the G1 lease, blocks further drive use and requests a
restart. Wait for its report to save, then power the console off before
continuing. Stop between commands still performs safe original-mode cleanup.

Send this probe's automatically saved `diagnostics.txt` too. Photos are
optional. These are hardware diagnostic candidates; host simulations and
native compilation do not establish acceptance on the real drive.
