# Windows CE boot test

Windows CE games are being brought up in steps, with ARMADA first. The test
from the Games detail screen of a CE image is now the **boot test**: it loads
the Windows CE kernel exactly as the placement test below did, then starts it
and stops at the first disc request K-UI cannot serve, showing what CE asked
for. The game is not expected to run yet.

## What it needs

- `KUI/apps/games/ce-probe.kui` and `KUI/runtime.kui` from the same build.
- A raw GDI (2352-byte tracks) whose IP selects Windows CE, for example ARMADA.
- **SCI microSD.** Only the SCI reader is built CE-safe (below); preparation
  refuses other storage.

## How to run it

1. Games, select the CE game, A to inspect it.
2. The detail screen says `A Windows CE boot test`. Press A.
3. The confirmation says the game is not expected to start. Press A.
4. The launcher closes. The stage loads IP.BIN, then the kernel file
   (`0WINCEOS.BIN`), checking every sector's header and every SD block's CRC.
5. Photograph the last screen shown, then power cycle.

## Step 1: placement (passed on ARMADA)

## What it does

The kernel file is a 2048-byte load prefix followed by the kernel body. The
prefix's header (one load section: address, file offset, length, entry) says
where the body goes. Following Flycast's independent BIOS (see the
[loader audit](evidence/windows-ce-loader-audit-2026-10-01.md)), the prefix
goes to `0x8ce01000` and the body to `0x8c010000`.

The native stage occupies `0x8ce00000`, which covers the prefix's place, so
this test has its own package, `ce-probe.kui`, whose stage is linked 64 KiB
higher, at `0x8ce10000`. Its header carries the magic `KUIRCE01`; the launcher
refuses to mix it up with `retail-boot.kui`, and each stage checks the IP's
Windows CE flag itself.

The stage then:

1. Reads the prefix sector and builds the load plan
   (`src/core/ce_load_plan.c`) against everything still in use: firmware, IP
   and bootstrap area (`0x8c000000`–`0x8c010000`) and the stage with its stack
   (`0x8ce10000`–`0x8cff0000`). A rejected plan stops with `CE LOAD PLAN
   REJECTED`, its result code and the header fields.
2. Loads the body to `0x8c010000` and copies the prefix to `0x8ce01000`.
3. Shows the checksums, placement, the kernel's ROM header if it has one, and
   the IP's boot bytes.

## The result screen

| Line | Meaning |
| --- | --- |
| `PREFIX CRC32` | CRC32 of the 2048-byte prefix, read back from `0x8ce01000` |
| `BODY CRC32` | CRC32 of the body bytes in place at `0x8c010000` |
| `PREFIX AT` | Where the prefix was put |
| `BODY AT / BYTES / SECTORS / ENTRY` | The body's place, length, sectors read and the entry address |
| `ROMHDR / PHYSFRST / PHYSLAST / RAMSTART / RAMEND` | The CE ROM header found through `ECEC` at body offset 0x40: the kernel image's span and the RAM CE will use |
| `NO CE ROM HEADER AT BODY OFFSET 40` | Shown instead when the body has no such header (Worms Armageddon) |
| `IP F0 / F4 / F8 / FC` | Four words of the IP at 0xF0–0xFF, the boot information the next step needs |
| `STORAGE BLOCKS READ` | SD blocks read by the stage |
| `PLACEMENT CHECKED - CE WAS NOT STARTED` | The test finished |

Expected values, from the kernel files the owner supplied:

| Game | Prefix CRC32 | Body CRC32 | Bytes | Sectors | ROM header | Physlast | RAM start | RAM end |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ARMADA | `6200729C` | `B9F755BC` | `00131800` | `00000263` | `8C141420` | `8C141778` | `8C142000` | `8CFD0000` |
| Bust-A-Move 4 | `70513A3C` | `F6902800` | `001BE800` | `0000037D` | `8C1CE19C` | `8C1CE5B4` | `8C1CF000` | `8CFD0000` |
| Worms Armageddon | `EFFD7677` | `CD686951` | `0010E800` | `0000021D` | none | | | |

All three have body and entry at `8C010000`, and physfirst `8C010000` where a
ROM header exists.

## Console result: ARMADA, build 79f065b4ea88 (2026-10-03)

Passed on the owner's console over SCI microSD. Every value matched the
table above: prefix `6200729C`, body `B9F755BC`, prefix at `8CE01000`, body at
`8C010000` (`00131800` bytes, `00000263` sectors, entry `8C010000`), ROM header
`8C141420` with physfirst `8C010000`, physlast `8C141778`, RAM `8C142000` to
`8CFD0000`. `STORAGE BLOCKS READ 00000B45` (2885: 74 for the IP, 2811 for
the file's 612 raw sectors). The IP words at 0xF0–0xFF read `20202020`: that
range is the end of the IP's 128-byte title field (0x80–0xFF), padding spaces,
not boot information.

Found while planning the next step: ARMADA's CD driver (`wsegacd.dll`) calls
the BIOS GD entry `0x8c0010f0` directly, after `SetKMode(1)`, rather than
through the `0x8c0000bc` vector. The resident already redirects that entry
(and `0x8c001000`) to itself, so those calls reach K-UI.

## What it tells us

- That the prefix/body split, the higher stage and the sector checks work on
  the console with a real CE disc image.
- The RAM CE will claim. ARMADA's kernel uses `0x8c142000`–`0x8cfd0000` plus
  its driver globals up to `0x8d000000`, which covers the CE stage. The stage
  must therefore hand over before CE's kernel starts, and keep nothing there
  afterwards. The low resident (`0x8c008300`) sits below the kernel image;
  whether CE leaves it alone is the next step's question.

## Step 2: the boot test

After placement the stage carries on as for a native game:

1. It puts the relay's trampoline at the body start (`0x8c010000`), installs
   the CE-safe SCI reader in the low IP area, clears IP byte `0xFC` bit
   `0x20` exactly as native launches do, and enters the IP's bootstrap 2.
   Only the trampoline's first 64 bytes (all of its code) are placed, so the
   body keeps its `ECEC` signature and ROM header pointer at offset `0x40`
   while bootstrap 2 runs.
2. Bootstrap 2 runs under a K-UI exception table with SR.BL clear, which
   otherwise matches the native entry (SR.BL set): any exception, which would
   have reset the console, stops with `EXCEPTION WHILE BOOTSTRAP 2 RAN`, its
   vector, EXPEVT, TEA, SPC, SSR, R15, PR and the code at SPC; an interrupt
   (only if bootstrap 2 lowers IMASK) is left pending, as SR.BL would have
   left it, without changing any of bootstrap 2's registers.
3. Bootstrap 2 jumps to the body start and reaches the relay, which restores
   the boot VBR and SR.BL, checks the boot stack and CPU state, restores the
   body's first 64 bytes, checks the body's CRC32 and the reader's bytes,
   shows `ENTERING WINDOWS CE` (held two seconds, as is the screen before
   bootstrap 2) and jumps to `0x8c010000`.
4. Windows CE starts. Its CD driver calls the BIOS GD entry `0x8c0010f0`
   directly; the reader has redirected that entry (and the others) to itself.

The CE-safe reader (`KUI_RETAIL_CE`, built only into `ce-probe.kui`):

- **Stack:** CE calls with its MMU on and its stack at a virtual address. A
  TLB miss while exceptions are blocked (SR.BL) resets the console, so this
  reader writes and reads the caller's stack only with the caller's own SR:
  it masks after saving registers and unmasks before restoring them. Its
  private stack and all its data are physical addresses.
- **Addresses:** every buffer and parameter address goes through the existing
  check, which refuses anything that is not main RAM (`0x0c`/`0x8c`/`0xac`)
  without touching it. A CE virtual address therefore makes a request fail
  cleanly instead of crashing.
- **Reads:** a fixed step per EXEC, and a step on each CHECK while a read is
  pending (CE's driver polls CHECK between short sleeps). No pacing.
- **Trace:** it records the last four calls and, when one fails, stops with:

| Line | Meaning |
| --- | --- |
| reason | `GD REQUEST REJECTED`, `GD FUNCTION UNSUPPORTED` or `IMAGE READ FAILED` |
| `CALLER / STACK / SR / MMUCR / VBR` | CE's return address (PR), its stack, its SR at the call, the MMU control register and CE's vector base |
| `R7 / R4 / R5 / R6 / CALLS` | The failing call: function, first and second arguments, R6, and how many calls came before |
| `EARLIER` (three rows) | The three calls before it, newest first, as R7 R4 R5 R6 |

## Console result: first boot test, build 64f30e3f8ce4 (2026-10-03)

ARMADA reached `ENTERING OWNER BOOTSTRAP 2`, then the console reset to the
BIOS. That build entered bootstrap 2 with SR.BL set (as for native games), so
any exception there resets the console. Two differences from native launches
were found and removed in the next build:

- It left IP byte `0xFC` unchanged. Native launches clear its bit `0x20`
  before entering bootstrap 2, which skips bootstrap 1; every working launch
  has had it cleared.
- Its 128-byte trampoline covered body offset `0x40`, where the CE image
  keeps `ECEC` and its ROM header pointer, while bootstrap 2 ran.

ARMADA's kernel itself (`nk.exe` StartUp at `0x8c0120c0`, reached through a
jump at the body start) sets its own SR, stack and VBR at once and does not
read the bootstrap's state, so the reset is most likely inside bootstrap 2.

## Console result: second boot test, build 15b912ccfff5 (2026-10-03)

The same reset, but now clearly after `BOOTSTRAP 2 REACHED GAME ENTRY` and
the relay's screen: bootstrap 2 completed and the reset happened inside
Windows CE. The owner supplied ARMADA's IP.BIN (analysis only, not
committed). Its bootstrap 2 is SEGA's standard one ("IP Ver 1.03", January
1999): it disables the caches, clears 0x8c00fc00–0x8c010000, sets SR
`0x700000f0`, R15 and VBR `0x8c00f400`, FPSCR `0x40001`, and jumps to the
address stored at `0x8c00e004` (`0xac010000`). It never reads IP byte `0xFC`.

Reading ARMADA's kernel image for what CE expects of the BIOS:

- `wsegacd.dll` (the CD driver) reads IP bytes `0xF0`–`0xFB` (a 12-character
  string) and `0xFD` (4–99) only when byte `0xFC` is 1, so the real BIOS
  writes boot information there; with `0xFC` not 1 (as on disc, or cleared)
  the driver takes its "not available" path. It copies 256 bytes from IP
  `0x100`, the high-density TOC stored on the disc (`TOC1`, tracks 3–5).
- Its initialization loops on **BIOS system function 2** (`0x8c0000e0`,
  R4=2) until it is not negative; 0 means the disc is present and unchanged
  (it then copies the TOC), above 0 clears its TOC. That is the BIOS's
  check of the disc in the physical drive. K-UI forwarded it to the BIOS,
  which saw K-UI's own disc or none: the most likely reset.
- `platutil.dll` calls the BIOS sysinfo, font and flash vectors directly
  (unchanged by K-UI) and system function 1 (exit to menu, which K-UI already
  turns into its counters screen and restart).
- CE's debug output is enabled only on SEGA development hardware (it probes
  `0xa05f68a0` and looks for a monitor at `0xac008000`/`0xac004000`), so a
  retail console shows none.

The next build's CE reader answers function 2 itself (0: disc present and
unchanged), records it in the trace as `E0`, and stops with
`CE PASSED A VIRTUAL ADDRESS` at the first GD pointer argument (R5 of
REQUEST/CHECK, R4 of DRIVE/DATATYPE) that is not a main-RAM alias.

## Console result: disc-check build 21c31ca296bf (2026-10-03)

The first stop screen from inside Windows CE: `CE PASSED A VIRTUAL ADDRESS`
after 4 calls, from `wsegacd.dll` (caller `01DE1F8A`), SR `40008000` (kernel
mode, interrupts enabled), MMUCR `5800B801` (MMU on), VBR `8C0120F0` (CE's
table). The calls: GD INIT; REQUEST command `0x18` (drive init, no
parameters); EXEC; CHECK of token 1 with its status buffer at `080DFCF0` on
CE's stack (`080DFCDC`), a virtual address in the CD driver's process.

The driver's init (`0x8c0aaf20`) is REQUEST `0x18`, then EXEC and CHECK in a
loop: CHECK 2 (done) continues, 1 (busy) sleeps 10 ms and polls again, -1
(failed) reads the error code and runs its disc check (BIOS system function
2). The earlier builds refused the status buffer, so CHECK failed and the
disc check reached the real BIOS: that was the reset. After a command
completes it polls its disc interrupt event (SYSINTR 20) with a zero timeout,
so it does not depend on the interrupt to make progress.

ARMADA's driver uses GD functions INIT, REQUEST, EXEC, CHECK, ABORT, DRIVE,
DMA/PIO transfer and check (6, 7, 12, 13) and the PIO callback (11), and
commands 16, 17, 19, 20–23, 24, 26, 27, 30, 31, 33, 34, 36, 38 and 39. K-UI's
service lacks commands 26, 38, 39 and functions 6, 7, 12, 13 (11 only with
no callback).

Next build: the CE reader uses CE's virtual addresses directly. It masks
interrupts but leaves SR.BL clear, so CE's own TLB-miss handler maps each
page as the service reads or writes it; the service passes U0/P3 addresses
(only while the MMU is on) to the CE reader instead of refusing them.

## Console result: virtual-address build 6eb4736083b8 (2026-10-03)

ARMADA went past `ENTERING WINDOWS CE`; the screen went black (CE's display
driver taking over the video) and stayed black: no stop screen, no reset. So
CE accepted the virtual-address answers and asked for nothing K-UI refuses,
but it is waiting on something not yet visible.

Reading `wsegacd.dll` further: a request runs synchronously (EXEC and CHECK
with 5 ms sleeps) unless CHECK reports word 3 = 1, which K-UI never writes;
only then does its interrupt thread wait for the disc interrupt (SYSINTR 20,
15 s timeout, then ABORT). The platform's `ResetToFirmware` (`platutil.dll`)
calls the BIOS menu vector (`0x8c0000e0`, R4=1), which K-UI's reader catches.

Next build: two ways to see the hang.

- **Live status line** near the top of the frame CE is showing (its start
  and line pitch read from the video registers, nothing changed), redrawn on
  every disc request and every 16th call: `CALLS FUNCTION COMMAND SECTORS
  LBA` (calls so far, the last GD function, the last command, sectors read
  so far, the last sector address). If it keeps changing, CE is still
  reading; if it stops, the last values say where.
- **A+B+X+Y+Start** during the hang: if CE's controller handling still runs,
  K-UI's exit screen shows the guard, steps and sectors, CE's last caller
  and four calls, and the last command (`COMMAND LBA SECTORS DEST`), held
  for about 15 seconds before K-UI restarts.

## Console result: status-line build ec6d39cc5372 (2026-10-04)

This time ARMADA stopped with `GD REQUEST REJECTED` after 26 calls: REQUEST
(R7 0) for command `0x26` (38, DMA stream read), parameters at `0204FAF0`
on CE's stack, from `wsegacd.dll` (caller `01DE2DC2`). The earlier calls end
with CHECK, EXEC, CHECK of token 6. (The previous build's black screen was
probably the same point, its stop screen not reached or not drawn.)

How ARMADA's driver reads: it locks the buffer's pages and lists their
physical pieces (at most 4 KiB each, 32-byte aligned). One piece: DMAREAD
(17). Several: DMAREAD_STREAM_EX (38) with {FAD, sectors, 0}; when CHECK
first reports 3 it starts DMA_TRANSFER (6) {address, bytes} of the first
piece. A DMA thread waits on SYSINTR 21 (Holly ISTNRM bit 14, GD DMA end),
checks DMA_CHECK (7) for 0 and transfers the next piece. The request
completes when SYSINTR 20 (ISTEXT bit 0, the drive) wakes the interrupt
thread, whose CHECK then returns 2. A missing interrupt 20 times out after
15 s and aborts the read.

K-UI cannot make the G1 DMA or the drive raise those, so the CE reader raises
them inside CE's kernel the way CE's own interrupt dispatch does after the
platform handler returns a SYSINTR: set the pending bit, queue SYSINTR-8 in
the 32-entry ring after the ring's head index, set the reschedule flag. The
scheduler then sets the driver's events. In ARMADA's `nk.exe` these are
`0x8c145b40`, `0x8c145aa8` and `0x8c145884` (KData + 0x340, 0x2a8, 0x84). The
stage finds them by matching that dispatch code in the loaded kernel (one
match each in ARMADA and two other titles' kernels, at different addresses).

Next build:

- The CE reader serves command 38: CHECK returns 3 (STREAMING) while bytes
  remain; DMA_TRANSFER copies its piece at once (pieces may split sectors:
  `kui_retail_image_read_part`), raises SYSINTR 21, and after the last piece
  completes the command and raises SYSINTR 20; DMA_CHECK returns 0 with 0
  bytes left.
- The stage shows `CE PEND CE RING RESCHED` (the three addresses), or
  `CE KERNEL INTERRUPTS NOT FOUND`, on the placement screen.
- The CE reader may now fill up to `0x8c00c000`, with a 2 KiB stack at
  `0x8c00c000`–`0x8c00c800` (the IP's lower bootstrap area, unused once
  bootstrap 2 at `0x8c00e000` runs). Native readers are unchanged.
- Still missing for ARMADA: commands 26 and 39 (PIO stream), functions 12
  and 13, and 11 with a callback.

## Console result: stream-read build bddec88525e8 (2026-10-04)

Stopped before CE: `BOOTSTRAP 2 REACHED GAME ENTRY` (stack `8C00F400`, SR
`700000F0`, cache `00000000`, as before), then `BOOTSTRAP ALTERED RESIDENT`.
A K-UI bug, not bootstrap 2: the stage writes CE's kernel addresses into
the reader's slot after copying the reader in, then compared the whole
reader with its original copy. Next build: that comparison skips the slot
(checked against the addresses instead) and its detail is now the offset
of the first changed byte.

## Console result: resident-check build 50c37b5c2464 (2026-10-04)

Stream reads started working: after 33 calls ARMADA stopped with `IMAGE READ
FAILED` on a DMA_TRANSFER (R7 6, token 7, parameters `080AFE94`) from the
driver's DMA thread (caller `01DE1788`). Before it: DMA_CHECK of token 7
(answered 0) from that thread, which had woken on the SYSINTR 21 K-UI raised,
and CHECK of token 7. So the first piece moved, CE's kernel delivered the
injected interrupt, and the next piece's card read failed.

The byte-range read passes the host tests with the streaming transport too,
so the failure is most likely the SD transfer itself: K-UI's SCI reader
receives each 512-byte block by DMA channel 1, and once CE's display driver
runs, its channel 2 DMA (which DMAOR serves first) can hold channel 1 off
for longer than one SCI byte. The receiver then overruns, the SCI port is
marked faulted and the read fails. Earlier reads ran before CE drew anything.

Next build: the Windows CE reader reads the card by polling (the CPU paces
every byte, so nothing can overrun); and its stop screen replaces the oldest
call row with `SD IMAGE BLOCKS DMAOR DMA2 CTL`: the SD result (0 OK, 3
timeout, 4 command rejected, 5 data token, 6 CRC), the image read result,
card blocks read, and the DMA controller's operation and channel 2 control
registers.

## Console result: polled-SD build 96705505d1d6 (2026-10-04)

The same stop at the same call (DMA_TRANSFER of token 7 from the DMA thread,
`080AFE94`, after 31 calls), now with the new row: SD `0` (the card read
was fine), IMAGE `9` (`KUI_GAME_RANGE`), 114 blocks read, DMAOR `8201`,
channel 2 control `12C0`. So DMA starvation was not the cause: the image
reader refused the range. ARMADA's driver merges physically adjacent pages
into one piece, so a DMA_TRANSFER can be far larger than 4 KiB, and
`kui_retail_image_read_part` takes at most 64 sectors per call.

Next build:

- A stream's DMA transfer moves 4 KiB per driver call: DMA_TRANSFER takes
  the first step; DMA_CHECK (from the DMA thread) and EXEC (from the
  interrupt thread) each take one more and DMA_CHECK reports the bytes left
  (1 while some remain). After every step the DMA end interrupt is raised,
  and the drive's while bytes remain, so one of the driver's threads calls
  again; for the last piece only the interrupt thread does. CE runs between
  steps, however large the transfer.
- The CE reader reads the card by DMA again (polling would halve reads).
- The CE reader's limit is now `0x8c00c800`, its stack `0x8c00c800`-
  `0x8c00d000`, still below bootstrap 2.

## Console result: stepped-stream build 9ab26bd0e9ad (2026-10-04)

Large stream reads work: the status line climbed (seen at `0x190` calls,
516 sectors, last LBA `0x7DA6B`), and ARMADA ran to 711 GD calls and 4,646
card blocks before stopping with `GD REQUEST REJECTED`: a DMAREAD (R4 `0x11`)
whose parameters are at `0C3BF4EC`, on a stack (`0C3BF4B8`) in process slot
6, from `wsegacd.dll` (caller `01DE2D96`). SD 0, IMAGE 0.

The bug: with CE's MMU on, `0x0c......` is a virtual address (slot 6), but
K-UI took that area as RAM's physical alias (true with the MMU off), so it
read the parameters from the wrong memory. Physical `0x0c` addresses do
reach K-UI from CE, but only as DMA destinations (taken from the locked
pages), never as pointers the CPU uses.

Next build: in the CE reader, every CPU pointer (parameters, status, PIO and
TOC destinations) outside P1/P2 is virtual, `0x0c` included; only DMA
destinations (DMAREAD's, DMA_TRANSFER's) are physical RAM (`dma_guest`).
Host test `virtual_pointers` keeps a separate virtual window at `0C3B0000`.

## Console result: virtual-pointer build fc08d071ed7d (2026-10-04)

**ARMADA reaches gameplay under Windows CE, loading from SCI microSD.** The
intro FMV runs very slowly ("maybe 5 fps at best"); the owner has not played
ARMADA on a drive for comparison. Likely why: every read happens inside CE's
disc call with interrupts masked, so CE is frozen while the card is read
(about 1 MB/s at best), and ordinary reads (DMAREAD) advance 4 KiB per
EXEC/CHECK with the driver sleeping 5 ms between them. On real hardware the
GD DMA runs while the CPU decodes.

Bust-a-Move 4 and Worms Armageddon are Windows CE titles too; the stage's
kernel pattern matched both kernels. Bust-a-Move 4's `sh4ser.dll` drives
SCIF (`0x1fe80000`), not the SCI port the reader uses.

Speed options, smallest first: interrupt-driven ordinary reads (CHECK word 3
= 1 and SYSINTR 20 per step, no 5 ms sleeps); a throughput readout (KiB/s and
time CE spends frozen in the reader); a background reader for CE like the
native X/Y readers (card DMA and interrupts while CE runs).

## Speed build after fc08d071ed7d (2026-10-04)

CE's scheduler tick is 25 ms (its timer interrupt adds 25 to its millisecond
count, KData + 0x88, in ARMADA, Bust-a-Move 4 and Worms Armageddon alike), so
`wsegacd.dll`'s "Sleep(5)" between ordinary-read steps really waits about
25 ms: two 4 KiB steps per tick caps DMAREAD near 300 KiB/s, and an FMV
starves. The driver sleeps only when CHECK's word 3 is not 1; with 1 it
returns pending and its interrupt thread waits for the drive's interrupt.

Next build (CE reader only):

- CHECK reports word 3 = 1 while a PIOREAD/DMAREAD is pending, and every
  read step raises SYSINTR 20, so the driver continues at once instead of
  sleeping a tick.
- The live status line becomes `CALLS COMMAND KIB/S BUSY PCT SECTORS`,
  redrawn about twice a second: calls, last command (hexadecimal), then in
  **decimal** the read rate in KiB/s and the share of time CE spends inside
  K-UI's reader over the last half second or more, and total sectors read.
  The clock is CE's millisecond count; time inside the reader comes from
  TMU0's count, which is only read.

## What each outcome means

- **`EXCEPTION WHILE BOOTSTRAP 2 RAN`**: bootstrap 2 faulted; SPC and the
  code at SPC say where (an address in `0x8c00e000`–`0x8c010000` is
  bootstrap 2 itself).
- **A reset while `ENTERING OWNER BOOTSTRAP 2` is shown**: bootstrap 2 set
  SR.BL itself before faulting, or overwrote the stage.
- **`UNSUPPORTED BOOT STACK` or `UNSUPPORTED BOOT CPU STATE`** before
  `ENTERING WINDOWS CE`: ARMADA's bootstrap 2 leaves a different state from
  native games; the values shown say how.
- **`ENTERING WINDOWS CE` stays on screen** (or the screen changes to CE's
  own output and stops): CE started but never reached a failing disc call.
  It may be waiting for a disc interrupt (IDs 20/21) that K-UI does not raise
  yet, or may have overwritten the reader.
- **The console resets to the BIOS:** an exception CE could not handle,
  possibly inside the reader; note when it happened.
- **`GD REQUEST REJECTED` or `GD FUNCTION UNSUPPORTED`:** CE asked for a
  command (R4 of a REQUEST row) or function (R7) K-UI does not provide yet:
  the next thing to implement. With R7 = 6 it is a DMA_TRANSFER K-UI
  refused (its parameters at R5: wrong token, size or destination).
- **`CE KERNEL INTERRUPTS NOT FOUND`** on the reader's screen: CE started a
  stream read but the stage did not find CE's interrupt ring in this kernel.
- **Windows CE's own screens, then a stop or freeze:** CE got further; a
  freeze may be CE waiting for a disc interrupt K-UI does not raise yet.
- **The status line stays still:** CE stopped calling the disc; the values
  are its last call. **It keeps counting:** CE is still reading (sectors and
  LBA show how far).
- **No status line at all over the black screen:** CE never called the disc
  after its display came up, or its video output is blanked.
- **The reader's stop screen:** the expected result. R5 (the request's
  parameter address) and the caller's stack show whether CE passes virtual
  addresses, which the next step maps.

## Next steps

1. From the trace: map CE's virtual parameter and buffer addresses to
   physical RAM (with the MMU state shown), or whatever the first failure is.
2. Raise the completion CE's driver waits for (Holly GD interrupt, IDs 20/21)
   if it never polls to completion.
3. Then the remaining GD functions CE uses, and a working title screen.
