# Silver Maple observer — experimental, read only

This first Silver test checks startup, controller traffic and the game's normal
Maple completion path. **It does not mount a virtual card or enable saving.**
The observer supplies no card responses and writes no save image or journal.
The existing ABC card (`p0001`) and full imported backup (`p0002`) are not opened.

The module admits only the supplied US Silver executable: product `T15108N`,
version `V1.001`, 2,956,128 executable bytes, SHA256
`4460c60597d6c1f587c125c2dd4b2b491d0a568f31b52c91bce7d8c74f9d550d`.
Other editions and modified executables are refused. Its borrowed memory is
allocated through this exact game's native heap before its other allocations;
the diagnostic reduces the game's available memory by about 192 KiB.

This revision adds visible startup checkpoints and verification progress to
locate the reported stop. The VMU admission guards and read-only behavior are
unchanged. It installs no new card or allocation profile.

## First console test

1. Back up the installed 2.0.0 `KUI/apps/games/retail-boot.kui` somewhere
   outside the `KUI` folder. Keep the usual loader for restoration afterward.
   If an earlier VMU test module is installed, keep it separately; it is not
   the usual loader.
2. With the console off, remove **all physical VMUs and controller accessories**.
   Use one ordinary controller in port A. Leave this arrangement fixed until
   the console is powered off again.
3. Copy this ZIP's `KUI/apps/games/retail-boot.kui` over the card's copy.
4. Launch the supplied original, uncompressed raw-sector Silver GDI over
   **SCI**, with the **standard native reader**, **background loading Off**,
   **CDDA emulation Off**, and **RAM patches Off**.
5. Watch the startup checkpoints below. If startup stalls, photograph the
   **last checkpoint and progress bar** and note roughly how long you waited.
   Reach the title/menu if possible. You may open its load-card screen, but no
   card will be present. Stay at a menu and wait for the diagnostic. Do not
   insert a VMU or select Save, Delete or Format during this test.
6. The observer stops after at most **3,600 attempted SDK DMA starts**. That is
   about one minute at 60 starts per second; actual timing depends on Silver.
   A memory-card request starts a shorter 600-start window. A guard refusal
   can stop it earlier. The final pending DMA is not started because the game
   has stopped for its report.
7. Photograph **all four report pages**, numbered 0 through 3. They repeat
   automatically, about ten seconds per page. Photograph any earlier refusal
   or `IMAGE READ FAILED` screen instead. Power off afterward.
8. Restore the backed-up 2.0.0 `KUI/apps/games/retail-boot.kui` before playing
   other games or testing virtual saves.

Report whether the title/menu appeared, where you waited, and whether the
controller worked. If the screen freezes or no report appears, describe the
last checkpoint, the progress bar's position and roughly how long you waited.
No imported save should appear in this diagnostic.

## Startup checkpoints

The earlier `7c0cd589610e` observer could leave `ENTERING OWNER BOOTSTRAP 2`
visible while later checks or native startup ran. That last visible line alone
does not establish that bootstrap 2 froze. The cause of the reported stop is
still unproved; this build adds evidence to locate it.

| Last checkpoint | What has been reached or completed |
| --- | --- |
| `BOOT2 REACHED OBSERVER RELAY` | Bootstrap 2 reached the executable entry trampoline. |
| `BOOT2 CHECKS PASSED` | The original entry was restored and bootstrap executable/reader checks passed. |
| `CHECKING OWNER SHA` | Exact-owner SHA verification is running. |
| `OWNER SHA PASSED` | The executable matches the admitted SHA256. |
| `CHECKING HOOK REFERENCES` | The bounded executable reference scan is running. |
| `HOOK REFERENCES PASSED` | The encoded reference scan passed. |
| `HOOK WITNESSES PASSED` | The sparse owner witnesses and resident exit-pointer checks passed. |
| `OBSERVER ARMED - STARTING SILVER` | Admission finished; the original Silver entry is about to run. |
| `NATIVE HEAP INITIALIZER RETURNED` | Silver reached and returned from its original root heap initializer. |
| `OBSERVER INSTALLED` | The leased observer and its hooks were installed. |

SHA verification and the hook-reference scan update a bottom progress bar in
64 KiB chunks. Capture both the last line and bar if either stops advancing.
The startup labels do not replace the four report pages requested above.

## Reading the report

All numbers are hexadecimal. Page 0 counts starts, decoded packets, malformed
traffic and completion matches. Page 1 counts commands and target subunits.
Page 2 retains the first and last subunit requests, or the unsafe cursor if a
guard stopped the test. Page 3 reports register modes, inherited interrupt
masks, response headers and completion context flags.

Silver calls its common completion wrapper from several contexts. Only entries
with inherited interrupt mask 15 and the complete admitted live state may
sample response headers. Lower masks are counted and skipped; the original
game consumer still runs. A nonzero mask-rejection count therefore needs review
with the other counters, rather than being treated as a failed save.

The profile checks the exact SDK manager, lock, ready/gun state, both receive
banks and all 48 slot pointers. A mismatched state stops or skips observation
before untrusted response access. It does not make Silver compatible merely
because its static addresses match. This test must establish console behavior
before a separate virtual-card build is prepared.

The package audit checks the linked stage, low reader and worker; exact profile
identity; native code/stack bounds; absence of FPU instructions; and the actual
SH4 trampoline's integer state preservation. Its instruction model does not
simulate caches, interrupts or hardware timing. Successful observation does
not establish save/load support or persistence.

The separately prepared Silver-only raw card contains `_SILVER_.SYS`,
`_SILVER_.001` and `_SILVER_.002`: 36 used blocks and 164 free. It is an offline
image awaiting its own new identity, empty journal and fresh allocation profile.
Do not replace `p0001` or `p0002` with that image or edit an old profile to point
at it.
