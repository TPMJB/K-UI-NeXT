# Worms World Party US kernel contracts — 2026-10-15

Static review of the owner's exact `0WINCEOS.BIN`: **1,828,864 bytes**, SHA256 `189719d8d3763284f74c5e66704710b9f9968118cb58aeb0a0844bd121a629ab`. No executable bytes, extracted binaries or disassembly are included in this record. Private read-only parsing preserves the original Armada audit's exact identity guard; a separate exact-size/SHA subclass admits this input for bounded metadata inspection.

This is a new reviewed kernel profile (`wwp-us`), not an additional title admitted to a previous SHA. Related NK structures and COREDLL wrappers can use the existing implementation, while DSOUND/SNDCORE identities and method layout require their separate WWP review. This static record does not claim a successful hardware run.

## Prefix, ROM and reserved memory

The load prefix has one record: physical address/entry `0x0c010000`, file offset `2048`, body bytes `0x1be000`. Prefix/body CRC32 values are `7e29bc00` / `12f68791`. The `ECEC` marker points to ROMHDR `0x8c1cd614`; its 84-byte CRC32 is `935ae972`. The bounded TOC contains 27 modules and 3 files.

| Metadata | Exact WWP value |
|---|---|
| Physical image range | `0x8c010000` through `0x8c1cda2c` |
| RAM start / free / end | `0x8c1ce000` / `0x8c1dc000` / `0x8cfd0000` |
| Copy record | `0x8c02eae8` to `0x8c1d1c00`; copied 1261 bytes, total 41096 bytes |
| OEM globals | `[0x8cfd0000, 0x8d000000)`, `0x30000` bytes |
| Reserved stream pool | `[0x8cf50000, 0x8cfd0000)`, `0x80000` bytes |
| Controller budget | `[0x8cfa7000, 0x8cfbf000)` |
| ROMHDR RAM-end patch | `0x8c1cd630`; original `0x8cfd0000`, reserved `0x8cf50000` |
| COREDLL / SNDCORE / DSOUND TOCs | `0x8c1cd688` / `0x8c1cd888` / `0x8c1cd8a8` |

The reservation precedes NK page-allocator initialization and changes only ROMHDR's RAM-end word after full-kernel admission. Exact original-header CRC, RAM start/free/end, OEM bounds, linked stage/BSS bounds, pool exclusion and unchanged bootstrap copies remain required. Its equality to the BAM4 pool geometry is supported by this ROMHDR, not inferred from similar file size. The low resident and reserved pool keep the existing destination guards.

## Independently verified globals and geometry

| Contract | Exact value |
|---|---|
| KData / current PROCESS | `0x8c1d1800` / `0x8c1d1890` |
| PROCESS base / stride / count | `0x8c1da6e0` / `0x9c` / 32 |
| Heap lower / upper bounds | `0x8c1d9430` / `0x8c1d9434` |
| MODULE list head / APIsets | `0x8c1d1b24` / `0x8c1dba80` |
| IRQ pending / ring / reschedule / handler table | `0x8c1d1b40` / `0x8c1d1aa8` / `0x8c1d1884` / `0x8c1d1c04` |
| WIN32 record / methods / callback113 sentinel | `0x8c011b20` / `0x8c0118e0` / `0x8c011aa4` |
| PROCESS slot / HANDLE fields | `+12` / `+8` |
| MODULE self / next / attachment / references | `0` / `4` / `12` / `16` |
| MODULE base / flags / reader / kind | `80` / `84` / `96` / `100` |
| MODULE count / span / section table | `112` / `124` / `176` |
| Live section stride | 28 bytes |
| Software page walk | slot shift 25; block shift 16 / mask `0x1ff`; page shift 12 / mask 15; MEMBLOCK page entries `+12`; valid mask `1` |

The original source's masked IRQ matcher has exactly one match in the actual body, at `0x8c01270e`, preceded by the exact dispatch entry at `0x8c0126f0`. Its loaded literals independently produce the four globals above. The stage compares all four against this exact profile. Reader ownership remains limited to the existing SCI ERI/RXI handler slots; CE retains timer, sound and ASIC dispatch.

Native callback admission consumes the existing three-word descriptor, validates the handle and process, supports the shared high/P1 target, and performs/restores the process/access-key/VM/ASID switch itself. The adapter preserves that route, its ABI and stable-owner checks; it does not install its own process switch.

## Reviewed instruction witnesses

Ranges are nominal NK addresses, also file-backed ROM storage addresses for these uncompressed sections. Hashes identify reviewed ranges without distributing their contents. All ranges were read only after exact whole-file SHA validation.

| Contract | Start / end exclusive | Bytes | CRC32 | SHA256 |
|---|---|---:|---|---|
| native_process_switch | `0x8c012118` / `0x8c012150` |56| `d663d7e2` | `88b7b2e14f7058663b8c43a8dda6153e5cd848211caeeec929af2d8109bb64c9` |
| odd_PC_PSL_dispatch | `0x8c0121f0` / `0x8c0122bc` |204| `0e626189` | `67ef0bba9cf2be41f882b0af660de7cc8b6f3dac59948a4395018ee29a30687b` |
| software_page_walk | `0x8c01259c` / `0x8c01263c` |160| `93bf7ce2` | `20f57397ea5ceab580e7fd4cd4a84cccd0a7536bad369c57e35763132253ed34` |
| IRQ_dispatch_and_literals | `0x8c0126f0` / `0x8c012790` |160| `f31f8a81` | `da29c7a9b854c6c9085cc3354ea320775b08a54623125a9d377af771d4cf9b57` |
| WIN32_record | `0x8c011b20` / `0x8c011b2c` |12| `182145a7` | `c2d925abd58f30314237a352a379f5c02dc2291973ac71b5fb5f7edc97106423` |
| callback113_sentinel | `0x8c011aa4` / `0x8c011aa8` |4| `ffffffff` | `ad95131bc0b799c0b1af477fb14fcf26a6a9f76079e48bf090acb7e8367bfd0e` |
| callback_admission_and_owner_switch | `0x8c015530` / `0x8c01591a` |1002| `1f1b1dcd` | `7f435690cc633860b070befdcce2ae69454d0b3fa71277c4c4b3ff675bb0fa11` |
| callback_return | `0x8c015950` / `0x8c015a6c` |284| `1dbee697` | `e1d5da4001602cd319dc8c4a2588db4252328ca765236a3effd5e8c215e274af` |
| process_geometry | `0x8c015c5e` / `0x8c015c98` |58| `649065e7` | `6f617a33b7f9d871dcb35401abca2d575afdacf10539e25aaf515b55286268a8` |
| heap_bound_validation | `0x8c014a74` / `0x8c014af4` |128| `87b8bfb4` | `f2d70c28862a6c1bbc8452fd677b0f2ac37731c5f53de0a49116cee2ebb5be1a` |
| APIsets_initialization | `0x8c01dac8` / `0x8c01db68` |160| `bed0e415` | `3881d6aed47d7c7d47daf64e21b2524be138ae2478213e9cb17d239faca57768` |
| MODULE_constructor | `0x8c022450` / `0x8c0224ae` |94| `60746780` | `74c65c9a7ee3fa7af1fac9f820c6f02bfd66165bf67db954835aa1d175d892aa` |
| MODULE_count_and_references | `0x8c02255e` / `0x8c022570` |18| `a7ec275b` | `79317bdccae160dba1f347119403ea6b934020c0714e38aacdd5496257b5c901` |
| MODULE_disk_base | `0x8c0225de` / `0x8c022602` |36| `26915a74` | `0d5626b2d40db43e3d9983ef89fdb5310c13ff3f5a615f30f222c0c038b57432` |
| MODULE_ROM_base_and_sections | `0x8c02266a` / `0x8c02269e` |52| `9e70cc54` | `fdee6fd60e790470b40adda22d6ddd90284a3422dc6884016972eb51927f7f13` |
| MODULE_head_link | `0x8c0226ee` / `0x8c022704` |22| `e0cb4e91` | `66e7d46b48c0420f42289691f1b80cdb968a2a3ef63f9c5bd0fb258878c82435` |
| disk_section_records | `0x8c021512` / `0x8c021546` |52| `9d93e7c0` | `85d6f832e166cfeafc4a9dd3d34a4f999e41eaedcd66e01cb9f8af728a518db4` |
| ROM_section_records | `0x8c0215f4` / `0x8c02162a` |54| `b2380d7d` | `c19445805465abf28ccfeac101f023832e00c569b5f2383bd0d03d1333c8dccc` |
| MODULE_attach_and_reference | `0x8c022ad2` / `0x8c022b04` |50| `60b88cfb` | `e32770e71002642090a2c36dffa084edcd97c795b43a027845c55a554a5ce91e` |
| MODULE_span | `0x8c022cd4` / `0x8c022d18` |68| `4374564d` | `1e1cc562fa245aa5980115cde422983fb8854e2f32019481ab8b37b61460ef07` |

- **native_process_switch:** THREAD+12 selects PROCESS; native switch publishes current PROCESS at KData+0x90, access key, software VM table and MMU ASID.
- **odd_PC_PSL_dispatch:** Native odd-PC PSL decoding preserves the method113 callback route at sign-extended 0xfd1f.
- **software_page_walk:** Slot shift 25, block shift 16 / mask `0x1ff`, page shift 12 / mask 15; MEMBLOCK pages start +12; normal committed pages require valid mask `1`.
- **IRQ_dispatch_and_literals:** Unique bounded stage pattern match; actual literals resolve pending, ring, reschedule and handler table.
- **WIN32_record:** WIN32 identity 0x32336e57, type/count 0x00900003, method table 0x8c0118e0.
- **callback113_sentinel:** WIN32 method113 remains the special 0xffffffff sentinel.
- **callback_admission_and_owner_switch:** Native callback validates HANDLE/API class 2/PROCESS, keeps high/P1 callback PC, and switches through the existing native process switch.
- **callback_return:** CALLSTACK return restores native previous PROCESS/access key and returnPC; adapter does not change TTB/KData itself.
- **process_geometry:** Explicit slot VA>>25−1, 32 entries, stride 78*2=0x9c, base 0x8c1da6e0, slot+12 and HANDLE+8.
- **heap_bound_validation:** Native object validator loads heap lower/upper bounds at 0x8c1d9430/+4 and checks object HANDLE+8.
- **APIsets_initialization:** Initialization stores exact WIN32 record at APIsets 0x8c1dba80.
- **MODULE_constructor:** Constructor establishes self 0, attach 12, base 80, flags 84, reader 96, kind 100 and section table 176 holders.
- **MODULE_count_and_references:** Header/count holder at 112; per-process reference array at 16 is cleared for 64 bytes.
- **MODULE_disk_base:** Filesystem-backed module reserves the native PROCESS0 image span and stores base at MODULE+80.
- **MODULE_ROM_base_and_sections:** ROM base derives from exact e32 preferred base plus PROCESS0 slot; section count 112 and table 176 use 28-byte records.
- **MODULE_head_link:** Singly linked MODULE list at KData+0x324; next+4 stores previous head before publishing new head.
- **disk_section_records:** Native external 28-byte records retain size 0, RVA 4, adjusted address 8, protection 12, flags 16, stored bytes 20 and file offset 24.
- **ROM_section_records:** Native ROM records use same 28-byte geometry, copying size/RVA/adjusted address/flags from o32.
- **MODULE_attach_and_reference:** Flags+84, base+80, process attachment mask+12 and 16-bit process references+16.
- **MODULE_span:** Native free/validation path reads the image base at MODULE+80 and virtual span at MODULE+124.

## Reuse and verification limits

COREDLL's nominal span and all section geometry match BAM4. Only 7 of 183808 uncompressed text storage bytes differ; its other three sections are byte-identical. All 13 existing COREDLL wrapper prefixes were compared directly at the admitted RVAs and match. NK contains moved code and changed ROM metadata, so unrelated addresses were never accepted through a blanket rebase.

WWP DSOUND text virtual size is `0x6bd6`; SNDCORE is `0x3db0`, exports 72 entries and adds two APIs. Its firmware and WSEGACD also differ from Armada/BAM4. Those differences prevent using any previous sound-runtime identity. The separate sound contract must admit the exact WWP ROM and filesystem DLL representations and actual method layout before callable addresses are published.

Validation performed for the memory/IRQ change: bounded ROM parsing; exact SHA before private code review; unique dispatch pattern and literal resolution; actual WIN32 metadata/sentinel checks; reviewed process/module/page-walk/callback ranges above; exact owner-ROMHDR host check and every single-bit header mutation rejected; existing Armada pool regression. Final link/layout/stack and complete profile tests are recorded by the release audit. Hardware boot, loaded-module ownership and playback remain console validation requirements.
