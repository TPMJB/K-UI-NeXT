# Generic CE CPU payload delivery

The first universal-loader integration extends the VC2 virtual-VRAM lesson to
the separately compiled generic CE readers. It changes the final payload sink
in the asynchronous GD reader and synchronous image reader. It does not change
guest-address admission, mapping permissions, DMA destination classes, page
fault handling, completion ownership or the accepted native/Smash readers.

CPU virtual addresses can refer to main RAM or VRAM. Their numeric address is
not enough to choose a byte-capable copy. Preserve the original mapped pointer
and use aligned volatile halfword stores. Only an odd head or tail requires a
halfword read/modify/write to preserve the adjacent byte in the same page.
The steady loop packs two possibly unaligned source bytes into one halfword
store. Already normalized P1/P2 main RAM keeps the existing memcpy fast path.
The existing buffer ownership contract must cover RAM/VRAM and the neighboring
edge byte; arbitrary device mappings and concurrent adjacent-byte writers are
not newly supported.

`include/kui/retail_ce_payload_copy.h` is included only in the existing
`KUI_RETAIL_CE` configuration. Native Standard, Background and native-MMU
preprocessing is compared with the unchanged Main policy. Physical VRAM DMA
destinations in generic CE remain a separate admission gap; this CPU copy
change does not enable them.

The focused regression executes production GD, asynchronous reader, SCI,
cursor and synchronous image code. Exact old copy-policy controls fail the
ordinary, PIO-stream and synchronous cases. Source checks, guarded copy cases,
odd edges, split/page-boundary pieces and CRC refusal run under sanitizers.
The controls are reconstructed from bounded reviewed hunks and checked against
complete old-source hashes, so shallow CI does not need historical Git objects.

```sh
make test-ce-virtual-vram-copy
python3 tests/test_ce_virtual_vram_copy.py --toolchain .deps/sh-elf \
  --output build/universal-loader-tests/ce-virtual-vram-copy-linked.json
python3 tools/check_ce_virtual_vram_width.py build/retail-ce/resident-sci.elf
python3 tools/check_ce_virtual_vram_width.py build/retail-ce/resident-scia.elf
```

The emitted-instruction check executes the actual linked sink with bounded
authored source/destination memory, checks all destination bytes, neighbors,
callee-saved state and stack, and rejects byte/longword store mutations.
The cold-rollout collector applies it to ordinary and all four CDDA CE owners,
in addition to their existing embedded-byte, layout and stack gates.

## Fit within the existing resident

The complete build exposed an existing CE CDDA stack-boundary failure; adding
the safe payload sink also consumes code space. Keep the boundary and buffers
unchanged. The shared CE CRC16 update instead represents its 256-entry byte
table with 128 entries plus the polynomial's high-bit XOR. This releases
256 bytes of read-only table space and retains the exact original CRC for
every transferred byte and complete block. Native CRC code remains unchanged.

`tests/test_ce_crc16_factoring.py` compares every 65,536 accumulator states and
256 input bytes against the original production table, checks emitted SH code
and verifies native preprocessing. The smaller representation adds index/XOR
work to the CE byte loop; it is not a throughput optimization. Inspect actual
linked loops for accumulator spills and retain their stack/layout gates.
Console data/audio performance remains a qualification requirement.

These are implementation and linked-code results. They do not execute a CE
kernel, reproduce physical VRAM timing or establish game/FMVs performance.
Keep native-build and package results distinct from console acceptance.
