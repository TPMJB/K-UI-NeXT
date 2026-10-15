# World Party periodic status restart

The first experimental WWP build, `b57985c47dfa`, boots and starts music, but
repeatedly restarts the selection. The supplied 18-second clip `62707.mp4`
shows four accepted starts, continued source delivery, and no recorded
underrun/recovery. Read-only inspection identifies a game-driven restart caused
by the reported drive status. No executable bytes or disassembly are included.

## Exact inputs

| Input | SHA256 |
|---|---|
| GAME.EXE | `458f0df3238f31a40239f9a5507b29e4aca63f468982457f65216e0ed0d97444` |
| WINCE/WSEGACD.DLL | `3c74f32ee5fb9277a85cc486f4e4921daf6065eb780120a6db4400cdbd15c28b` |

All addresses below are module-relative RVAs in those exact private inputs.

## Observed hardware behavior

The recorded overlay reports `BIND 0`, `GAPS 0`, `WORKER 1`, attempts 4,
Play 4, refused 0, and HRESULT 0. START events occur at ticks `0x7af5`,
`0x8f9b`, `0xbd84`, and `0xd11b`; cumulative source delivery increases between
starts. The frozen underrun observation is all zero. This supports repeated
accepted PLAY requests rather than a supply failure causing the worker to
recover. The static chain below explains how those requests arise.

## Native status and restart chain

| Location | Reviewed behavior |
|---|---|
| GAME `0xcda4` | Music update invokes the periodic poll producer. When game music state is 2, it reads the cached CD state. |
| GAME `0xb3804` | Increments counter at global RVA `0x14444c`; when the counter exceeds 500, enqueues operation 7 and clears the counter. The threshold counts update visits; no wall-clock frequency is assumed. |
| GAME `0xb3970…0xb39ac` | Operation 7 issues DeviceIoControl `0x25038` with an 8-byte output. If output word 0 is 1 or 2 and the prior state is not 3, sets cached CD state at object `+44` to 0. |
| GAME `0xb37dc` | Returns cached CD state at object `+44`. |
| GAME `0xcdd6` | A zero cached CD state while music state is 2 calls Play wrapper `0xb371c` for track 4. |
| GAME Play operation 2 | Issues IOCTL `0x25020` and records cached CD state 2. |
| WSEGACD `0x2418…0x2478` | IOCTL `0x25038` invokes the GD entry with subfunction 4, then copies its two status words unchanged into the caller's 8-byte output. |

| Reviewed GAME witness | RVA / bytes | SHA256 |
|---|---|---|
| Music update | `0xcda4` / 108 | `60de656e0da5bb0edf3064b04896aecfd602cdd6e3e11e5c49b3a48253564d28` |
| Status request and interpretation | `0xb3970` / 48 | `4a2766d05bf822bc869d916b0c6c4469df218564a6a58a54254a703b707974ef` |
| Poll scheduling | `0xb3804` / 38 | `0c32da55733024f880c73e86ce16baa4339316e875ec7bb06d4a093c4e9185a8` |
| Play | `0xb38ac` / 38 | `60385c31776dde3c56326edc91e012c5c0493e871d8d7f34e7c81ad1bf21373f` |

Consequently, reporting drive status word 0 as 1 or 2 while authored music is
playing causes the next periodic poll to mark playback stopped. The music
update then issues a new PLAY for track 4. The worker can be healthy throughout.

The correction belongs in the WWP playback-status projection: report the
authored playback state truthfully through the GD DRIVE query. Suppressing
or deduplicating the game's legitimate PLAY requests would leave the false status
unchanged. This evidence does not justify a cursor, frequency, or transport-rate
retune, and does not change the working Worms Armageddon binary.

The correction is scoped to the WWP DRIVE query. The resident reads one aligned
published drive-state word, projects it for synchronous query formatting, then
restores the DATA service state before returning. GETSCD and REQ_STAT retain
their existing metadata behavior. No native API or reader pump runs in this
status bridge, and the resident retains its fixed memory and stack reservation.

## Follow-up boot regression

Build `2ed954489128` stopped at the Windows CE handoff on the console. Its photo
shows the complete controller/zero-state validation and transport attachment had
passed. A separate rebuild of the booting `b57985c47dfa` confirms identical low
initialized/BSS boundaries and every BSS object address; the fixed stack boundary
is not a new overlap. Read-only review did not identify a specific faulty
instruction in the new early status call, so the precise console failure remains
unconfirmed.

The replacement removes that early call entirely. It restores the original
controller entry and 600-byte report ABI, exports one volatile status word in
zeroed controller BSS, and reads that word through a bounded low-memory leaf.
The word remains zero until the owner reports successful native playback; queued
intent or a failed first start cannot publish "playing." STOP clears it. Later
pause/resume/end/fault states retain the game-facing status correction without
entering controller code or changing SR from a DRIVE query. Host regressions and
linked checks validate these boundaries. Console boot and sustained music were
still unverified at that stage; the later accepted build is recorded below.

## Verified linked reader corruption

The follow-up console photo also shows `26763e434331` stopped at the same
handoff display. That display persists through later initialization; it does
not establish whether audio had started or which BIOS call was reached.

The complete original `b57985c47dfa` module was rebuilt byte for byte:
134,404 bytes, SHA256
`f6b3b68fa6c96f540c1e175aae0a538134c7946fd0eb051487750f699d084eee`.
Comparison with failing `26763e434331` identifies a concrete compiled-code
defect: eleven relaxed direct branches land eight bytes inside their requested
callees, skipping required register/stack prologues. The generated LTRANS
assembly requests the real named function entries with no such addend.

| Failing linked call | Actual destination | Required entry |
|---|---|---|
| resident dispatch `0x8c00b61c` | `_deliver + 8`, `0x8c00afd8` | `0x8c00afd0` |
| drain `0x8c00af30` | `_place + 8`, `0x8c00ade0` | `0x8c00add8` |
| piece `0x8c00afc6` | `_drain + 8`, `0x8c00aefc` | `0x8c00aef4` |

The dispatch call executes before the DRIVE test and scalar load. The matching
original call at `0x8c00b610` correctly reaches `_deliver` at `0x8c00afc4`.
Thus the failing program contains a broken DATA execution path independent of
the status projection. Earlier source, stack, integer-instruction and placement
checks did not prove these final branch destinations; that omission must be
covered by a mandatory linked-call check before another candidate is delivered.

The measured repair is `-Wa,--renesas` at the WWP LTO assembly/link step. It
preserves relocations against named functions instead of optimizing them into
section-symbol addends that became stale during SH relaxation. It corrects all
eleven calls without growing the SCIA image: initialized end `0x8c00c3e4`, BSS
`0x8c00c400…0x8c00d800`, and the original guard/stack bounds remain intact.
The WWP synchronous SCI fallback also receives the option; its eight latent
malformed calls are corrected even though it was not the failing playback path.
Other title profiles and the preserved Armageddon module are unchanged.

`tools/check_retail_calls.py` now checks final executable ELF bytes for all six
embedded images during both the required linked audit and candidate packaging.
BSR must reach an exact function entry; cross-function BRA must do the same.
Intra-function loops remain valid. The only local-call exception pins the
complete existing libgcc division routine and its exact internal call edges.
Literal-pool classification cannot recursively hide later instructions. Nine
focused corruption fixtures cover prologue skips, stale function sizes, false
debug labels, misleading pool words, and the pinned library helper.

The preserved failing SCIA/SCI ELFs are rejected with eleven/eight bad edges.
The corrected images pass: SCIF 153, SCI 254, IDE 155, SCIA 322, stage 257 and
entry zero direct edges. The source/stack/placement checks remain mandatory;
none of their limits or accepted instruction bodies was weakened for this fix.

Read-only checks also found that WWP GetCurrentPosition returns interleaved byte
positions modulo the buffer length; stereo PCM16 positions advance in 128-byte
blocks. Its callback normalizes host-copy frontier `+116` before releasing
consumer CS `+108`, and 44,100 Hz produces neutral native pitch on both stereo
ports. None of those paths explains the observed new PLAY requests.

## Follow-up: DRIVE readiness hypothesis

A bounded scan of direct GD entry calls in the exact supplied WSEGACD found
one direct subfunction-4 DRIVE call: helper RVA `0x2418`, call RVA `0x2436`,
return PR RVA `0x243a`. This is the IOCTL `0x25038` passthrough above. On
success it copies both returned status words to the caller without branching
on the reported drive state or waiting for state 1/2. No separate internal
DRIVE readiness loop was established.

The hangs in later experimental builds therefore do not establish that a
playing status of 3 is incompatible with this driver. Malformed relaxed
reader-call targets were independently found in those linked builds before
status access. That concrete linked-code defect was corrected before the
accepted run below; it did not justify a caller-specific status override.

## Accepted console run: `b08802f8945f`

On 2026-10-15 the owner reported that the replacement worked and supplied
`62711.mp4`. The 18.35-second clip shows the diagnostic pages for exact build
`b08802f8945f`; it does not show live gameplay or provide an audible continuous
music check. Successful boot and playback are the owner's report. The video
corroborates the following recorded state:

| Diagnostic | Recorded result |
|---|---|
| Guard / bind / audio fault | All zero |
| Gaps / recoveries | Both zero |
| Native Play / refused / HRESULT | 2 / 0 / 0 |
| Frozen underrun observation | All zero |
| Source bytes delivered | 19,552,768 |
| Native start ticks | 25,052 ms and 58,052 ms, 33 seconds apart |
| Final session tick | 149,075 ms |
| Final control/state | Paused, then STOP command 33; state 4 is normal STOPPED |

The six attempts count applied control-mailbox operations, not six playback
starts. The two accepted native starts 33 seconds apart do not show the former
roughly five-second restart pattern. The stopped final state is consistent
with the recorded STOP rather than a playback fault. No recorded guard failure,
binding failure, underrun or audio fault is visible in this submitted evidence.

The installed `ce-cdda-wwp.kui` candidate is 134,564 bytes, SHA256
`2a51b25b710acc82ca0c34d72f013b2bbad8633c1e5c9edcbc9b3fffa1b748fe`.
This acceptance belongs to that build and the supplied runtime version. It
does not establish every track, a full playthrough, other game revisions or
all DATA formats. The clip does not establish the tested image's raw/cooked
geometry. World Party remains generally Experimental for 2.0; this evidence
does not promote unfamiliar titles sharing its kernel/sound adapter.
