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
- **The reader's stop screen:** the expected result. R5 (the request's
  parameter address) and the caller's stack show whether CE passes virtual
  addresses, which the next step maps.

## Next steps

1. From the trace: map CE's virtual parameter and buffer addresses to
   physical RAM (with the MMU state shown), or whatever the first failure is.
2. Raise the completion CE's driver waits for (Holly GD interrupt, IDs 20/21)
   if it never polls to completion.
3. Then the remaining GD functions CE uses, and a working title screen.
