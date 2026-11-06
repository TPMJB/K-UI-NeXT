# Terminal cache writeback and the VC2 warm-launch report

The reproduced software defect is in `kui_native_return_cache_off`, called
after game retirement and hardware quiescence but before CE translation ends.
Its512-entry operand-cache walk used address `F4000008 +32*i`. Address bit3
selects associative access; it does not select a harmless alternate alias.

The SH7750 hardware manual, revision7.02, section4.5.3, printed pages133–134,
specifies that associative writes translate the supplied address through the
UTLB when translation is enabled. A missing or different translation can
suppress the write without raising an exception. The loop reads physical tags,
clears their dirty bit and writes them back, so it must use the direct entry
interface at `F4000000`. That interface writes back dirty valid lines without
depending on the previous game's translations.

[Renesas SH7750 hardware manual](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware)

The subsequent CCR value808 invalidates the cache. With the old address,
dirty data can be discarded instead of published. The low firmware/IP area
survives return by design; the bounded reader publication covers only
004000..007EA0, and the full CE main-RAM clear starts at010000. Therefore
the global cache walk must actually publish the other retained dirty lines.
Both terminal controllers use their private P2 stack at this boundary.

The fix changes exactly one bit in one literal. It adds no instructions,
RAM reservation, game hook, read command or reset. Helpers and workers retain
their current implementations. Existing CRC checks bind the corrected bytes.

`tests/test_native_return_cache.py` executes the actual linked SH loop against
a manual-derived cache/UTLB model. Its old-address control demonstrates data
loss with translation enabled; the direct-entry version must preserve RAM
across missing, remapped and identity translations. The test also checks all
512 entries, the unchanged CPU contract and the CCR synchronization interval.
This model proves the cleanup correction, not a physical Dreamcast run.

The reported failing sequence remains Swirl → Shining Force → Phantasy Star II
→ VC2, black before title. No live cache-tag or failing-PC capture exists, so
the patch is a narrowly supported candidate for that failure. Neither a fresh
VC2 boot nor software tests establish acceptance of the multi-game sequence.
