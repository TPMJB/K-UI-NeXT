# Original-video return follow-up: build and recovery

This work extends the current `work/internal-smash-latency` tree. Do not recover
an older worker, rebuild a historical package, or use old Main updater decoders.
`docs/accepted-progress.md` is the hardware acceptance authority. Code alone does
not establish console selection speed or CE return support.

The latest owner test of `9f470e548ad2` reports about 18 seconds from native
selection to Start Game / Resume, then `RET B4C2CD76`, detail `00000000` on
return. The exact decoded refusal is `SMASH TIMING ORIGINAL VIDEO REFUSED` in
returned-outer preparation. The KOS-only capture rejected current game video;
KOS framebuffer zero passes while nonzero/blanked/320×240 game fixtures fail.
This replaces the broad5ca105 window with an identified9f refusal, not console
acceptance of a repair.

The follow-up transports the original validated80-byte VID1 in the diagnostic
return runtime (516→596 bytes, CRC-covered). Its blank outer destination is
immediately after the368-byte return seed. Publication happens only after the
complete outer capsule CRC; returned outer validates/uses that carried KOS
capture. Initial GUI capture is unchanged. Keep the strict video guard; do not
admit arbitrary game modes or synthesize timing. Preserve v2 allocation epochs,
accepted72KiB worker, CE helpers and readers. The existing9f markers remain;
there is no new console return or speed result yet.

## Return progress interpretation

The 9f markers below split the old 5ca `105` window and remain in this follow-up. These are progress boundaries,
not error numbers; seeing a marker does not prove the following work completed.

| RET marker | Completed boundary | Next operation |
| --- | --- | --- |
| `105` | Return setup/quiescence reached the raw probe | Read the first outer-capsule sector; detail is its LBA |
| `106` | Raw probe returned successfully | Read the remaining capsule runs |
| `107` | All outer-capsule reads returned | Compute/check the complete padded capsule CRC |
| `108` | Complete capsule CRC matched | Publish/verify return context and live receipt |
| `109` | Return context/receipt publication completed | Restore firmware words |
| `10A` | Firmware restoration completed | Enter the restored outer image |

Outer progress is enabled only for a return seed with flags3. The initial GUI
launch uses flags1 and receives no extra outer markers.

| Outer marker | Boundary |
| --- | --- |
| `110` | C main entered, before display capture |
| `111` | Before SD initialization |
| `112` | SD initialization succeeded |
| `113` | Allocation-epoch validation succeeded |
| `114` | Executable load/descramble completed, before executable CRC |
| `115` | Reader installed, before BOOT2 |
| `116` | BOOT2 reached the relay |

These markers draw on the actual current framebuffer. Outer errors first draw
there and park before the old renderer: reason is `CRC32(message) | 0x80000000`
and detail retains the original error detail. Record both values. If `10A`
remains without new outer output, the entry interval is still unresolved. The
5ca photograph cannot be retrospectively assigned to one of these boundaries.
No marker establishes a speed improvement or repair. The new VID1 copy is part
of context publication between108 and109; it must not precede capsule CRC.

The four collection builds require a single clean source commit:

| Role | Makefile | Options |
| --- | --- | --- |
| Outer | Makefile.smash-menu-loader | SMASH_CARD_SESSION=1 SMASH_PHASE_DIAGNOSTIC=2 |
| Native | Makefile.smash-native-menu | SMASH_CARD_SESSION=1 SMASH_PHASE_DIAGNOSTIC=2 SMASH_NATIVE_GIN_ADOPT=1 |
| VC2 | Makefile.smash-vc2-child | SMASH_CARD_SESSION=1 |
| Swirl | Makefile.smash-swirl-child | SMASH_CARD_SESSION=1 |

Use separate fresh BUILD directories and the pinned restored SH/KOS SDK. Build
all four, then a matching compressed four-member runtime bundle. Build the GUI
with `KUI_SMASH_LAUNCH_TIMING=1`, that bundle and both manifests under
`resources/updater-bindings/`. Keep both physical GUI aliases identical and
select ROOT through both checked activation records for a manual-copy test.

The current candidate is receipt/allocation epoch v2. Initial outer admission
mints live allocation authority; children rebind with fresh CID and current
journal/save validation. They do not replay or read a witness sidecar. The GUI
still prepares/binds that allocation for the existing cohort format. SCI is
owned separately by each adapter; this is not a continuous-port-lease design.

Required focused gates include `Makefile.smash-card-session-tests`,
`Makefile.vmu-allocation-rebind-tests`, phase TXT bounds, production actual-module
admission and actual four-module publication. Witness/collection-attach and real
session-file FAT32/exFAT tests remain regression coverage, not evidence that the
v2 native runtime uses replay. Actual linked GIN, receipt transport, direct
ABXY+Start publication, raw-DMA batching, installer and low/high stack proofs
must bind final ELF bytes. The normal clock path must not nest the public fast
clock wrapper or relax its low-stack guard.

Preserve the49408-byte native worker /73728-byte lease and12248-byte resident,
exactly apart from their single build label. Keep the165472-byte verified read
cache. Raw CE helpers remain41152 bytes; prepared helpers are49152 bytes.
Their accepted raw SHA256 values are:

- VC2: `85e864ae8d386b4b2d5cec731f788441367fdf9f2bef86f57ccd12a430e214e8`
- Swirl: `47b5687e400b25b43ffa378b2b04171e59c039c57e018e83ac4de6bf7a8719e8`

The manual follow-up ZIP contains ten software/activation files: both GUIs, both ROOT
records, four stages and two prepared helpers. Never include generated witness,
late-load sessions, cards, saves, preferences or game files. Verify embedded
bundle members equal physical module files. Current Main engine3 updater
production parity is checked against2b4b8c673b7c71a58c41e8a79252313940ea944e.

The options-restoration test needs one RPG: save within Shining Force or
Phantasy Star II, press A+B+X+Y+Start and confirm the original per-game options
appear. Complete Save Game and wait for success/SD idle, then explicitly choose
Return to Menu. Relaunch the RPG and verify its progress; a later power-cycle
check establishes durability. The former direct-combo shortcut is removed;
retirement cannot invent an unrequested cartridge save or SYNC. This changed
options/save path still needs console confirmation. Preserve the e371 CE
result recorded in accepted-progress; no unrelated CE retest is requested.

Measure internal selection time and retain `smash-native-phases.txt` after the
sequence. Receipt sequence should advance while THIS_OWNER reports
full_admissions0, attaches1, allocation_epoch1, witness sectors/runs0,
native_gin_route1 and boot2_calls0. Unobserved SD/CPU timing remains NA. Use the
same `smash-shared-card` build group with fresh directories for the final clean
source ID. Do not reuse a c375 witness candidate or claim its timings for v2.
