# Damaged disc drive controls

The salvage menu's **Damaged disc controls** page provides two session choices:

| Choice | Values | Meaning |
| --- | --- | --- |
| Read speed | Unchanged, Maximum, 1x, 2x, 4x, 6x, 8x, 10x, 12x requested | Firmware speed request; nonzero speed codes are optional drive features. |
| Drive retries | Unchanged, 0, 1, 2, 4, 8 | Firmware decoding retries within each optical command. |

Both default to **Unchanged**. Choices apply only to New salvage, Resume latest
salvage and Retry unresolved sectors. They reset when K-UI restarts. They are
independent of application read attempts, recovery pass limits, and the existing
30-second read / 5-second abort-drain deadline. Zero firmware retries does not
disable EDC/ECC or the salvage worker's full sector validation.

## Applying a choice

The sole I/O worker prepares the disc, then snapshots its firmware mode before
identity reads or any salvage job writes. REQ_MODE command 30 returns four RAM
words: speed, standby timer, read flags and read retry count. Its parameter is a
pointer to that destination. SET_MODE command 31 instead takes the four mode
words directly. Both commands report 10 wire bytes, despite REQ_MODE writing
16 bytes of RAM. Static guarded workspaces survive an unconfirmed abort.

Only the selected speed and/or retry fields change. The snapshot's standby timer
and complete read-flags word are preserved, including the existing ECC/retry
flags. The entire four-word tuple must read back exactly. A rejected command,
short/malformed response, guard corruption or mismatch refuses acquisition; it
does not silently run under an unverified requested policy. The Unchanged /
Unchanged path issues no mode query or mode change commands.

A successful recovery INIT reapplies and verifies the selected tuple before
further optical reads. The first sector-format setup of a custom session, and
the first one after each INIT, also verifies the tuple before reading. This
checks for firmware that changes the settings during sector-format setup without
adding a mode query to every sector read. It never replaces the original snapshot. Clean completion,
Stop, SD failure and rejected-setting exits all attempt bounded restoration and
readback of that snapshot, even if B remains held. A potentially partial SET is
treated as modified before submission, so rejection still requires cleanup.

After a failed abort, live DMA/command ownership, media change or guard failure,
cleanup sends no unsafe mode commands. A failed restoration prevents ordinary
drive use until a console restart. The UI and diagnostic report state that restart
is required; committed salvage data remains saved.

## Measuring recovery

Diagnostics record requested choices, original/requested/readback tuples,
reapplication and restoration results. Existing optical timing reports record
read latency, successful bytes, failures and timeouts; salvage reports record
accepted batch/single-sector candidates and durable repaired/remaining counts.
Firmware readback proves only that the requested tuple was returned. It does not
prove a physical speed change, particularly in the GD high-density area.

For a hardware comparison, keep the same disc and saved job, record unresolved
counts and elapsed optical time for a bounded recovery pass, then try one changed
setting. Compare timeout counts and validated sectors per minute as well as
throughput. Retried samples can also improve without a setting change; separate
runs on a changing repair queue are not a controlled proof of speed efficacy.
No automatic speed sweep or vendor-specific drive command is issued.

Existing salvage headers, journal/patch formats, identity checks, FAD/EDC/P/Q
acceptance, audio agreement, collection batching and recovery scheduling remain
unchanged. These controls do not add subchannels, offset correction, archival
geometry or a reference-quality claim.

## Protocol sources

- Sega, *GD-ROM Protocol SPI Specifications v1.30*, 12 January 1999, mode table
  and REQ_MODE/SET_MODE rules: [original document mirror](https://github.com/luckyseoul/dreamcast-docs/blob/main/files/kochise/GDROM/DOCS/cdif131e.doc).
- [KallistiOS syscall declarations](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/syscalls.h).
- [redream BIOS mode command implementation](https://github.com/inolen/redream/blob/master/src/guest/bios/syscalls.c), used to cross-check BIOS word layout and wire-byte reporting.

Hardware effectiveness has not yet been established. Host tests exercise actual
adapter command parameters, successful and rejected modes, INIT reapplication,
Stop restoration, malformed replies and fatal ownership behavior.
