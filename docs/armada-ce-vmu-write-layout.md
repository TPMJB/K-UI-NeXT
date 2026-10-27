# Experimental CE VMU memory layout

`ARMADA_A2_WRITE_LAYOUT=1` selects a separate 48 KiB pre-NK reservation at
`0x8cfc4000..0x8cfd0000`. Its helper moves to that range's beginning; the
Maple stack, reports and OEM globals retain their existing addresses. This
milestone still uses the readonly backend. It adds no worker, SD provider,
write acknowledgement or deferred IRP completion.

The default 24 KiB reservation and linker remain unchanged. A source-profile
header supplies the experimental reservation/install, reader destination,
sampler source, RX-store, publisher-protection and P2 cache-reader bounds.
Each profile has a complete pinned bridge/store/cache-snapshot contract.
Absolute ELF markers distinguish the expanded helper, stage and reader;
mixed profiles fail native admission. Changing layout/diagnostic parameters
in one build directory invalidates its objects through `layout-config`.

The current helper contains 18,816 immutable bytes and 3,284 workspace bytes.
In the expanded profile its workspace ends at `0xacfc9654`, leaving 25,004
bytes before the unchanged stack bottom `0xacfcf800`. The stage BSS ends at
`0x8ce365e4`. These are linked bounds, not a hardware acceptance result.

The 48 KiB choice budgets the existing 22,100 bytes, 128-byte input growth,
up to 4,608-byte durable state, 580-byte mailbox, 4 KiB worker-stack allowance,
12 KiB new code allowance, existing 1,536-byte Maple stack and 512-byte report
tail: 45,848 bytes. Future code/state must pass actual link and stack audits;
this allowance is not an implemented storage/thread allocation.

Reproduce the two native profiles with the pinned toolchain environment:

```sh
make -f Makefile.armada-vmu-a2 ARMADA_A2_DIR=build/layout-default ARMADA_A2_AUTO_STOP=6 ARMADA_A2_AUTO_PHASE=2 BUILD_ID=af18576dba18 build/layout-default/entry.elf
make -f Makefile.armada-vmu-a2 ARMADA_A2_DIR=build/write-layout ARMADA_A2_WRITE_LAYOUT=1 ARMADA_A2_AUTO_STOP=6 ARMADA_A2_AUTO_PHASE=2 BUILD_ID=af18576dba18 build/write-layout/entry.elf
make -f Makefile.armada-maple-test write-layout-tests a2-tests
python3 tests/test_armada_ce_vmu_layout_native.py --readonly build/layout-default/hook.elf --expanded build/write-layout/hook.elf
python3 tests/test_armada_ce_cache_snapshot_native.py --write-layout
```

Native WRITE staging additionally requires the expanded layout, but this
milestone does not admit a writable runtime package. The next integration
must preserve the real CE pending request until a verified worker/provider
finishes; the owner's finalizer does not transparently retry AGAIN. A queued
or RAM-only final success remains invalid. No hardware package was created.
