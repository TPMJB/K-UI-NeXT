# World Party US sound binding contracts

Read-only static review of the exact privately supplied Worms World Party US
runtime. This records metadata and hashes; no executable bytes or disassembly
are included. These contracts support the isolated experimental `wwp-us`
binding. They do not establish console playback or a compatible foreign kernel.

| Input | Bytes | SHA256 |
|---|---:|---|
| DSOUND.DLL | 33,363 | `645e879953a9d9cec7cd0f03c77f37d3b19e5fa16da8d783af093320de3b4115` |
| SNDCORE.DLL | 20,052 | `ba3cf8f6cb5c19c37042b13568e0254f5f36f3bcd2b385b66e73f0aa7303e4ae` |
| WSEGACD.DLL | 24,657 | `3c74f32ee5fb9277a85cc486f4e4921daf6065eb780120a6db4400cdbd15c28b` |
| ARMCOM.DRV | 31,068 | `be21517e85a9df38e6f7e03e056ece2b6996739b0ae79e874a9795deb6477477` |

Kernel globals, process/module geometry and loaded-table placement are separately
reviewed in [the kernel contracts](world-party-kernel-contracts-2026-10-15.md).
The original ARM driver remains owned by CE. The adapter calls existing CE APIs
and never replaces the sound firmware or programs its ports directly.

## Exact module and method metadata

DSOUND ROM nominal base is `0x01c40000`, TOC `0x8c1cd8a8`; SNDCORE is
`0x01c50000`, TOC `0x8c1cd888`. DSOUND's singleton holder is RVA `0x8110`.
DirectSoundCreate at RVA `0x16e8` returns the existing singleton with AddRef when
present. Generic ownership requires a stable attached process, singleton,
reference count, module layouts, API targets and repeated mapping witnesses.
No application holder or game-code address substitutes for that proof.

Every vtable target below matches the external PE and embedded ROM at the same
RVA. Slots are listed in order, starting at zero.

| Vtable | RVA | Method RVAs in slot order |
|---|---|---|
| Device, 11 slots | `0x112c` | `19dc, 1a90, 1a98, 1ad4, 1b90, 1c5c, 1cc4, 1ce0, 1d10, 1d18, 1d40` |
| Secondary buffer, 21 slots | `0x1240` | `29fc, 50f8, 2b7c, 2c68, 2cb0, 2df8, 2e5c, 2e68, 2e8c, 2e98, 1d40, 2f28, 30c0, 338c, 34b8, 3510, 37e0, 393c, 39e8, 3afc, 16e4` |

Device fields: references `+8`; 16 owner pointers `+24…+84`; allocation critical
section `+88`; consumer critical section `+108`; masks `+128/+132`. Buffer fields:
byte count `+56`, software host ring `+64`, raw allocation `+68`, host-copy frontier
`+116`, public write cursor `+120`, stop request `+124`, stereo `+148`, looping
`+152`. Callback RVA `0x1d64` holds consumer CS `+108` across host reads and
frontier updates. The adapter therefore uses the existing native consumer lock.
Looping stereo sizes must be divisible by 64 bytes; mono sizes by 32 bytes.

All called method entry windows are immutable 32-byte witnesses: zero HIGHLOW
relocation overlap, and byte-identical external PE to embedded ROM. The admission
witnesses implemented in `armada_ce_live_sound.h` are:

| Module / entry | RVA | Bytes | CRC32 | SHA256 |
|---|---|---:|---|---|
| DSOUND Create | `0x16e8` | 32 | `a2f5e2c9` | `cb64107888b72a5c693bd92aa81a36bafd1a5a0483757751fb11bbe2c5bb1edc` |
| DSOUND CreateBuffer | `0x1ad4` | 32 | `34718dd9` | `71f9aa85d374d673f834919562717b6e9cca073453c7eb6224f576012d60bb2a` |
| DSOUND Play | `0x30c0` | 32 | `eee481b4` | `bae75aa0690f8658b8e2ffa4811f522f6af786a9323f17f0f9844b0e967fa747` |
| DSOUND SetPosition | `0x338c` | 32 | `4f8e5e6b` | `5c24c840ec375341491072e601548bf21b59d894213da72e516939d6c0f8726f` |
| SNDCORE DllEntry | `0x10d8` | 32 | `5ba8b52b` | `d97a239e78ffd5daa00a13ba8b7d45c3bc7aa4261e25302f96812a95033d2169` |
| SNDCORE FInit | `0x11c8` | 32 | `8303faf9` | `30e062a7d3e743081ecd8bd58f59ebf93db9d33e17b4361b1eafbf832b0cce9d` |
| SNDCORE Release | `0x12c4` | 32 | `53f228af` | `b1c8d84413ed81c33480a99e41319c59372cb11de4206f0c9971dac847bd36c5` |

DSOUND Enter/LeaveCriticalSection IAT RVAs remain `0x801c/0x8020` (COREDLL ordinals
4/5). All 13 selected COREDLL wrapper prefixes retain the reviewed Armada/BAM4
RVAs, lengths and CRCs. SNDCORE retains its own shared sound heap, command queue,
reference lifecycle and port synchronization; the adapter obtains buffers
through DSOUND rather than taking a second firmware owner.

The complete stored `.text` also matches after applying each external PE's exact
HIGHLOW relocation delta to the ROM nominal base; no bytes differ:

| Module | Bytes compared | HIGHLOW relocations | Normalized PE / ROM SHA256 |
|---|---:|---:|---|
| DSOUND | 27,648 | 767 | `a217e7b67b5e2c5d268939f32540a0a38118ea022223acbd277af0886051a312` |
| SNDCORE | 15,872 | 431 | `cc2a2672474f6b6c65bd7baf0e059490913a185a22c32386424fcf35fa9545ec` |

## Asynchronous Stop completion

Software Stop returns after setting buffer state `+180 = 3`, stop request
`+124 = 1`, and requesting a native sound interrupt. Its return value and
GetStatus's stopped status bits do not establish completed callback cleanup.

The WWP player saves owned voice bits, calls Stop once per epoch, then snapshots
ownership and active-software mask `device+132` under the native lock order
allocation CS `+88` then consumer CS `+108`. Both locks are released before any
Stop/GetStatus call or worker yield. Relevant active bits keep the model in
QUIESCING. Once acknowledged, GetStatus performs native cleanup sweeps, which
may take multiple visits for stereo. Only zero owned aliases and zero relevant
active bits allow model stop completion and ring reuse. Saved bits with zero
owners stay pending; bits proved reused by another nonzero owner are excluded.

These complete reviewed ranges are byte-identical external PE to ROM. DSOUND
ROM storage is `0x8c102000 + RVA`; that storage address is not a live-call address.

| Purpose | RVA / bytes | SHA256 |
|---|---|---|
| Stop | `0x39e8` / 236 | `d951d085d82d93ad9c7bee0266abce5d8c9bae8a1445639f1d0357c15e670b6d` |
| GetStatus | `0x2e98` / 124 | `717a88d2f35cab0188a26e1df09680a272b7354298d724ada12ff851737cf1c0` |
| Sweep | `0x1ff0` / 162 | `0adc600874dbb2d6e857929a91a5d9a1c316e24f4e256512454dbedf723e9ea7` |
| Active-mask acknowledgment | `0x1e8c` / 74 | `222b8f0e99c4bbf3d32b39112419e7d742598f1774c365f3567a84bd4d86cdd6` |
| Owner-slot clear | `0x20a4` / 46 | `5b0cbe0038aa6d918e6d2c2888ad6b8c66de7499dd4d5225343dd97c9093f609` |
| Stereo cleanup | `0x47a0` / 230 | `ff5638ac69816c2574f564f52134c391ed3be3bebf4b0dd272f268c15da6fa0c` |
| Owner release | `0x48a4` / 126 | `9b8243dbd7e7a881202c482875ab90ed06be50c6e9ef4b8a46d9f52545f2ec28` |
| Callback stop branch | `0x49c4` / 92 | `e3a07e1463be947da6dfead14fa6abfd72f824f6f0e2aba39ffc290bed96fd45` |

Focused authored fixtures pass for the WWP native lease, module/API identity,
asynchronous acknowledgment, multiple stereo sweeps, zero/reused owners and
failed reads. No private executable, firmware or PCM data is a test fixture.
