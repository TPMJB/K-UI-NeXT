# Retail boot correction, 2026-09-24

## Hardware report

The user tested build `d19f1e0ebaf3` with the existing DEAD OR ALIVE 2 dump.
Their photograph shows `LAUNCHER HAS SHUT DOWN` and `LOADING OWNER IP AND
EXECUTABLE`. They reported approximately four minutes to reach that stage,
then additional text flashed before the stock Dreamcast menu returned.
The unknown flashed text does not establish which subsequent stage ran.
The first retail attempt failed; gameplay is not accepted.

## Requested DreamShell comparison

The user explicitly requested reading their existing DreamShell code after
this failure. The inspected tree was `kui-1.0.1/firmware/isoldr/loader`.
Its old worktree metadata no longer resolves, so exact inspected source
hashes are recorded rather than inventing a commit identifier:

| File | SHA-256 |
| --- | --- |
| main.c | b35c58b65c0491bc21f54be5a7f2adb6d9d17e705c206213163ed91c4a6e0b38 |
| startup.s | 12bce28d2d4fe9277d7d3b66e3fd36842c127ddd5c78720f749e7d73d96aeb85 |
| utils.c | ba43a55a8cc6d65d9ea450d4cf968b70bc353973d256fa6c8f4b88222b6839df |
| syscalls.c | 650a3f1653092f632fde3f8facd43cff44236e24395a8605d9104cca72097951 |
| menu_syscall.s | 7a1774cbdbc3d3cb6935a55d7c1bb832b73d3b8b08aa608e2dc9b4748ee9bf4e |

`main.c` enters bootstrap 2, and `startup.s` establishes the retail CPU/cache
state. The old K-UI attempt entered bootstrap 1 after setting only SP, with
the virtual GD reader not yet installed. DreamShell's truncated IP path loads
only the final 8 KiB and still enters bootstrap 2; its lower-IP allocator also
supports reusing the area below that entry. Those observations support this
correction but cannot establish the selected title's behavior on hardware.

DreamShell clears Katana IP byte `0xfc` bit `0x20`; its source does not explain
the flag's meaning. This correction matches that RAM-only setup after the
original full-IP checksum has passed. No proprietary IP or executable bytes
are bundled. The CPU profile reference is attributed in `retail_stage.S`.

DreamShell additionally patches direct firmware GD entry points. This change
does not implement those patches: our firmware-forwarding path requires the
original entry, so overwriting it without redesign would introduce recursion.
Games bypassing the public vector remain a possible compatibility limit.

## Focused changes

- Install the independent resident before entering original bootstrap 2.
- Set retail SR/SSR, SP, VBR, GBR, FPSCR and CCR explicitly. Set SSR before
  changing SR.RB, so bank switching cannot change the source register.
- Enable caches before detached SD/CRC work. Publish copied resident code
  and purge dirty data before every cache invalidation at handoff.
- Keep preparation's current track open across sequential reads instead of
  reopening and walking from its first allocation cluster for every sector.
- Show load progress and briefly hold the two handoff screens. Stop on a
  reader failure/unsupported request or standard BIOS-menu return command 1,
  retaining the last request and return caller address for a photograph.
- Restore the exact executable entry, check its full CRC and verify resident
  code survived bootstrap 2 before returning to the game.

The linked instruction audit permits exactly one named `LDS r0,FPSCR` in the
high bootstrap setup. The relay and resident retain their no-FPU requirement.

## Validation scope

Only five existing preparation cases were selected: FAT32 valid, cancellation
during boot hashing and close failure; exFAT valid and cancellation during
boot hashing. They check file cleanup before disconnect and unchanged card
SHA-256. Address/undefined behavior sanitizers remain enabled; LeakSanitizer
is disabled because this execution environment rejects its ptrace operation.
The initial fixture seeding exited for that environment limitation before
the selected cases ran; it was rerun with leak detection disabled.

The broad portable/filesystem suite and accepted hardware probes are not
repeated. Native compilation, linked instruction/layout/stack checks and
packaging run in the console build. The three frozen optical source hashes
remain unchanged. Hardware timing, bootstrap completion and gameplay still
require the next console run.
