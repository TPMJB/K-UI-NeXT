# Original executable video-wait comparison

Read-only static analysis, 2026-10-23. Original code was read only. Disassembler: K-UI-recovery/.deps/sh-elf/bin/sh-elf-objdump -D -b binary -m sh4 -EL --adjust-vma=0x8c010000. Addresses below use the 8c alias; Power Stone 2 stores many linked function pointers using the equivalent 0c alias.

## Exact inputs

| Input | Bytes | SHA-256 |
|---|---:|---|
| Evolution 1 | 2143652 | ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35 |
| Power Stone 2 | 3097497 | a3dcd09a96045839dbbc35697eb4336e4eba79b52203a46bb2f8ee9a4ba2eed8 |
| Skies of Arcadia | 5460080 | 4218c07829d63080be2da86413ec023e6d77001c13a1494648db6e2e5cb73611 |
| Sonic Adventure 2 | 1578116 | 6919ed8bc96782d041be41ba86f90bf86d99632b8b7787391969721bc0991736 |
| Grandia II | 1279172 | dd583571969d275b479e33edfd98ed5e2097d16c37ff6aaba59a7abaf057dc45 |

Evolution input: /workspace/scratch/45de2288a575/native-extra-corpus/evolution1/1ST_READ.BIN. Other inputs: /workspace/scratch/45de2288a575/native-forensics-corpus/{ps2,skies,sa2,grandia2}/1ST_READ.BIN. Hashes match the repository's pinned input metadata.

## Exact-zero polling is shared

| Input | Mode setter | PVR getter | Nonzero loop | Zero loop | R13=3ff load |
|---|---|---|---|---|---|
| Evolution | 8c1f3440 | 8c1f1f36 | 8c1f3620 | 8c1f3628 | 8c1f3518 |
| Grandia II | 8c0fac80 | 8c0f9656 | 8c0fae66 | 8c0fae6e | 8c0fad58 |
| Skies | 8c326840 | 8c2de196 | 8c326a26 | 8c326a2e | 8c326918 |
| SA2 | 8c152ec0 | 8c14dad6 | 8c1530a6 | 8c1530ae | 8c152f98 |

All four getters read a05f8000 + R4. The loops use R4=10c, R13=3ff. Each zero-loop instruction sequence is exactly `jsr @r12; mov r14,r4` (delay slot); `tst r13,r0; bf loop`. Evolution's sampled SPC 8c1f362c is the TST. Timing writes follow the wait, so this waits on the active old timing. The presence of this loop cannot explain Evolution's uniqueness by itself.

Power Stone 2's comparable timing setter begins 8c2cb300. After assembling shadow timing values, 8c2cb468 flows directly to a PVR getter call at 8c2cb46e (offset 44), then PVR writes at 8c2cb476 onward. It has no matching zero-wait at this stage. This is a comparison of the identified setter, not a proof that no other code anywhere polls scanline zero.

## A real suppression difference

Grandia/Skies/SA2 have an additional global-controlled bypass absent from Evolution:

- Grandia: 8c0fade8 loads pointer 8c267dc4; 8c0fadea reads it; 8c0fadec compares with 1; 8c0fadee branches to 8c0fae76, skipping both loops. Setter 8c0b7138 writes R4 to this global.
- Skies: same check at 8c3269a8..9ae, global 8c5d7f7c, bypass target 8c326a36; global setter 8c2de7f8.
- SA2: same check at 8c153028..302e, global 8c29c950, bypass target 8c1530b6; global setter 8c14e138.
- Evolution: 8c1f35a8..35ae checks only flags 8c3589b8 & 2. If set, it enters the loops; no corresponding extra suppression check exists here.

The second graphics setup routines in those working executables actively use the bypass:

| Input | Second setup entry | Set suppression=1 | Call mode selector | Restore suppression=0 |
|---|---|---|---|---|
| Grandia | 8c03b074 | 8c03b096, delay R4=1 | 8c03b2be -> 8c0b5e9c | R4=0 at 8c03b2f8; tailcall 8c03b310 |
| Skies | 8c259e24 | 8c259e46, delay R4=1 | 8c25a06e -> 8c2dcf3c | R4=0 at 8c25a0a8; tailcall 8c25a0c0 |
| SA2 | 8c111f6c | 8c111f8e, delay R4=1 | 8c1121b6 -> 8c14d35c | R4=0 at 8c1121f0; tailcall 8c112208 |

Evolution's analogous second setup is 8c165d0c and calls selector 8c1cb59c at 8c165e66, with no such suppression pair. This is a concrete binary difference; it does not alone establish starvation or authorize a patch.

## Call order and bounded reachability

The initial graphics setup is before the reviewed Maple root initialization call in all four games:

| Input | Initial graphics call | Internal mode-selector call | Maple root call (pinned cache initializer_call) |
|---|---|---|---|
| Evolution | 8c04ed6e -> 8c165a4e | 8c165bdc | 8c04ed7e |
| Grandia | 8c010612 -> 8c03acca | 8c03af40 | 8c010622 |
| Skies | 8c015e74 -> 8c259a7a | 8c259cf0 | 8c015e86 |
| SA2 | 8c0100cc -> 8c111bc2 | 8c111e38 | 8c0100dc |

Evolution also has concrete later game paths to its second setup:

1. Game state code calls 8c0f6c60 at 8c046044 or 8c0460b2. That routine calls framepump 8c1663f8 at 8c0f6c6e, then 8c0f69d8 at 8c0f6c72 (R4=0); it calls 8c0f69d8 again at 8c0f6cd0 (R4=1), after the intervening movie/cache work.
2. 8c0f69d8 calls second graphics setup 8c165d0c at 8c0f6a02. That reaches selector 8c1cb59c at 8c165e66 -> selected preset -> mode setter 8c1f3440 -> zero wait.
3. Separate game function 8c0f6f26 calls the same second setup at 8c0f6f5e. This function is referenced from game code at 8c055f66, 8c055f88, and 8c055fa4 (loads; following JSRs invoke it).

Grandia's second setup has an actual game reference at 8c010dba (load; following JSR invokes it). No direct BSR/BRA or absolute pointer reference to the analogous Skies/SA2 second setup was found in their original executables, including all RAM aliases. This is a bounded static xref result; computed addresses or separately loaded code could evade it.

The second Evolution call path is distinct from the initial pre-Maple setup; its state-machine caller and framepump witness runtime graphics use. Static xrefs alone do not prove its temporal order against every possible Maple entry. Pair it with the actual trace's already-active submit/completion counters to establish Maple activity at the sampled stall. Saved SPC alone does not identify which of the two Evolution selector calls led to the stall; the caller stack/PR or an entry marker would disambiguate it.

## Phase comparison supplied by parallel exact audit

Grandia's normal PVR bit-3 ISR 8c0f8880 ACKs bit 3, sets pending callback bit 1, and increments counter 8c267c48. Dispatcher 8c0f9154 matches Evolution's dispatcher structure. Registered callback path 8c07dc74 -> 8c07dfd4 -> submit routine 8c0c727a reaches patch point 8c0c738c. Grandia's NTSC preset 8c0fb2a4 uses vertical max 106, compare 104 or 102, and compare2 21: the same late scanline phase family as Evolution. Thus late PVR callback phase is also not unique. Actual old hardware timing and callback duration remain unmeasured; periodic interference is plausible, every-frame coverage is unproved.
