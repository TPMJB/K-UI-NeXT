# Smash Pack native menu capsules

The collection loader retains the proven low foreground reader while the
original menu runs. The launcher prepares a native child capsule and an outer
menu capsule. Each completed GIN transition loads a verified fresh module;
selected arguments and return context cross the transition in sealed seeds.

The Games launcher selects the separate installed `retail-smash-menu.kui`
only for the exact original release, SCI/Standard, virtual VMU enabled and
unchanged launch options. Its unique capability, blank native loader seed and
blank return seed must be present. A missing adapter retains the existing VC2
collection route. A changed adapter refuses launch. A source mismatch returns
to the ordinary route without hashing the full menu a second time. Direct
PSII/Shining Force launches and the separate WinCE profiles retain their
existing preparation paths. This native menu profile does not install a CE
virtual card for VC2 or Sega Swirl.

`smash_menu_capsule_prepare()` reuses the launcher's existing track manifest.
It performs a bounded root lookup for `MDE_US.BIN`, then one complete MDE read
for SHA-256, CRC32 and native SDK cache preparation. It does not call
`prepare_launch()` or remap the track files. The native child and outer module
must carry the same exact 12-byte build stamp. It selects the existing
`KUIVC2TEST` card through the same shared-card boundary as direct PSII/Shining
Force launches, without creating a replacement binding.

Both generated files contain prepared native module payloads without their
64-byte `.kui` envelopes. Their final sectors are padded with zeroes, and each
plan's CRC32 covers every padded byte. The child contains the encoded MDE
manifest, current shared-card profile, complete native cache seed, a blank
304-byte native selection seed and a blank 368-byte return seed.

| Preparation order | File or allocation | Contents of the return seed |
| --- | --- | --- |
| 1 | `smash-menu-native.cap` | Blank |
| 2 | `smash-menu-outer.cap`, including the sealed native child plan | Blank |
| 3 | Initial outer module in memory | Configured plan naming the verified outer capsule |

The outer capsule's return plan points to its own 368-byte blank seed. That
plan is installed only in the initial launch allocation after the outer file
has passed sync, physical map comparison and complete readback. Neither disk
image contains a checksum dependency on its own return plan. The runtime
patches carried arguments/context only after checking the complete capsule
CRC, then enters the ordinary module entry at `0x8c010000`.

| Payload offset | Content |
| --- | --- |
| `0x0000` | Ordinary native module entry and retail package header |
| `0x1000` | Prepared 4096-byte original-menu or MDE manifest |
| `0x2000` | Cold stage, embedded worker or reader, and prepared seeds/profile |
| Native child plan's `menu_seed_offset` | Unique blank 304-byte native selection seed |
| Outer return plan's `menu_seed_offset` | Unique blank 368-byte return seed |
| `payload_bytes` | Zero padding through the next 512-byte boundary |

Each capsule has at most two physical runs. Existing outer allocations are
inspected before the child can be changed. Every existing capsule allocation
is checked before truncate/free; every new allocation is checked before its
first payload write. Checks reject every disc extent, every allocated sector
of the three card backing files, and the other capsule's complete allocated
clusters, including slack. After writing, each capsule is synced and reopened;
both its useful and allocated maps must remain identical and its complete
contents must read back exactly. Identical prepared capsules are reused
without a rewrite. Cancellation, short I/O, map changes or failed readback
cannot publish a return plan.

Run `make -f Makefile.smash-menu-capsule-tests` for the portable map, two-file
FatFs and production pair-publisher tests under AddressSanitizer and
UndefinedBehaviorSanitizer. They cover cross-capsule aliases before truncate
or write, allocation slack, reuse, changes to the child snapshot, short I/O,
cancellation, sync/close failures, map/readback changes and the absence of a
CRC cycle. `make -f Makefile.smash-menu-ui-tests MENU_LOADER=<outer.kui>` also
checks production source-bound routing against an actual packaged loader.
Console boot, repeated selection/return and save/reload remain hardware checks.
