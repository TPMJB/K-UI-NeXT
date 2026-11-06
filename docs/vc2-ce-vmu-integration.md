# VC2 CE virtual VMU integration

The owner reports that the foreground reader at `71256dd80b87` runs Virtua
Cop 2 through Sega Smash Pack. Virtual VMU support is the next integration
target. The original source-audit phase records the supplied kernel's Maple
contracts and tests their rejection rules. An opt-in blank-card experiment now
implements the nested reservation, mapped worker bridge and readonly A2 path.
See [the console test](vc2-ce-vmu-readonly-test.md); persistent saving is not
enabled, and console acceptance remains separate from the host checks.

## Reproduce the source audit

The supplied `VIRTCOP2/0WINCEOS.BIN` is sufficient for this phase. Standalone
external MAPLE DLLs are not required: the ROM contains uncompressed Maple
text and bounded compressed data sections. No original game bytes are checked
into the repository or emitted by the audit.

```sh
python3 tools/vc2_ce_vmu_owner_audit.py OWNER/VIRTCOP2/0WINCEOS.BIN
python3 tests/test_vc2_ce_vmu_owner_audit.py \
  --kernel OWNER/VIRTCOP2/0WINCEOS.BIN -v
```

The full input must be 1,841,152 bytes with SHA256
`b728e8eaef03bd7cc5acbe3dc3363ec9fafcaca90916cf21781d2e72ff99b3bc`.
The audit uses `ROMInventory` after this exact identity gate, then independently
checks ROMHDR geometry, both modules' O32 layouts and stored hashes, reviewed
text windows, root/target literals and COREDLL import/export names. The output
must reproduce [the metadata record](evidence/vc2-ce-vmu-owner.json).

Tests reject changed file size, header, module text/data, worker, builder,
completion, root/SetKMode literals and padding. Separate in-memory negative
tests check local window fingerprints, allocator/layout changes and accidental
substitution of SetThreadPriority for SetKMode. Ordinary discovery runs the
public identity/CLI tests; supply the owner file to run all seven tests.

## VC2 source profile

The addresses below describe the exact original ROM. A nominal CE virtual
address or ROM storage address is not an established live-call address.

| Item | VC2 source value | Integration consequence |
| --- | --- | --- |
| MAPLE nominal base | `0x01ce0000` | Armada's `0x01dd0000` profile cannot be reused |
| MAPLE ROM text | `0x8c0b1000`, virtual `0x6c32`, stored `0x6e00` bytes | Different driver implementation and fingerprint |
| MAPLEDEV nominal base / ROM text | `0x01c10000` / `0x8c10c000` | Preserve source binding for card-side behavior too |
| Completion entry | RVA `0x6f64` | Candidate worker-completion interception point |
| Ordinary worker | RVA `0x71cc` | Calls completion after SYSINTR18 status bit `0x1000` |
| Worker completion call | RVA `0x723c` load, `0x723e` call | `InterruptDone(18)` follows at `0x724a` |
| Ordinary list builder | RVA `0x6c84` | VC2-specific packet/list ownership review required |
| Root global | RVA `0x82b8`, literal at RVA `0x1840` | Within writable virtual data `0x8000..0x82c0` |
| SetKMode | Thunk RVA `0x7808`, IAT RVA `0x8044`, ordinal 630 | Must establish and restore worker mode through mapped code |
| SetThreadPriority | IAT RVA `0x8068`, ordinal 514 | Worker startup priority call does not establish kernel mode |
| Aligned text padding | RVA `0x7c34..0x7e00`, 460 zero bytes | Candidate only; reference/relocation exclusion remains open |

The completion entry has an incoming-T-dead prologue, but its notification
handling is different from Armada: extension `+0x15c` state is consumed before
the extension `+0x160` event is signaled. The worker also writes list-ready `1`
to mapped controller base `+0x88`. A completion-spanning Maple DMA fence is
therefore still required; the native software-start hook is not interchangeable
with this VBlank-triggered driver.

## Reusable implementation and remaining boundaries

The packet/card backend in `include/kui/vmu_emulation.h`, persistent session
and journal modules, and `src/loader/armada_ce_vmu_runtime.c` provide reusable
card behavior. `armada_ce_vmu_a2_publish.c` provides the existing A2 publication
model. Their driver offsets, event ownership and hook continuation must be
adapted to this VC2 profile before enabling them.

The existing CE preparation in `retail_stage.c:armada_maple_hook_reserve()`
excludes its helper from CE allocation by lowering ROMHDR RAMend before NK,
copies code/state, publishes services and hands off through a checked relay.
VC2 has the same original allocator ceiling `0x8cfd0000` and OEM globals
`0x8cfd0000..0x8d000000`. The writable helper layout's 48 KiB candidate interval
`0x8cfc4000..0x8cfd0000` fits this geometry. Geometry alone does not prove that
the native launch has released it or that the helper survives the transition.

The existing writable CE service's `kui_retail_async_vmu_loan_begin/end()` in
`retail_async.c` requires `KUI_RETAIL_CE && KUI_RETAIL_ASYNC`. VC2's working
foreground route does not satisfy this contract. Reads from a card image and
save commits both need a foreground SCI ownership adapter that defers VMU
storage while GD owns the bus, runs storage only from an admitted worker
context, restores the reader state, and quarantines uncertain acquired failures.

## Keep the working launch while proving the reservation

The active route is native `1ST_READ.BIN` to `VIRTCOP2/SAMPLE.BIN`, then
SAMPLE's embedded MAINANA loader and the CE kernel. The supplied separate
`MAIGO.BIN` is not the active embedded loader version. The outer `IP.BIN`
selects native startup; `retail_stage.c:ce_load()` explicitly requires an IP
Windows CE flag, so selecting the nested kernel is not a ready-made direct
launch option.

Existing private linked boot-stream evidence initializes YAMA metadata at
`0x8ce00000` (magic `0x414d4159`, FAD 529805, 899 sectors, mode 1), starts inside
MAINANA at `0x8ce09348`, checks the original prefix/body transfers, and stops
at NK entry. It proves that boundary, not a fresh direct launch or complete CE
application startup. The kernel prefix is otherwise standard: one body at file
offset `0x800`, size `0x1c1000`, destination/entry `0x0c010000`.

GINSU.INI selects MANIFEST and GINRESET; it does not specify the complete
VC2 nested CE startup contract. The supplied kernel's `default.fdf` registry
is compressed, and current inventory does not establish the application target
and arguments from it. A direct route would need explicit native-IP handling
and verified bootstrap/application-startup equivalence. It must not be presented
as a solved shortcut around reservation lifetime.

The next compatibility increment should preserve the working menu route and:

1. Bind a pre-NK preparation boundary to the exact complete kernel transfer
   and original MAINANA caller. Prove that its helper source and high interval
   remain owned until ROMHDR exclusion and relay publication complete.
2. Audit padding references/relocations, mapped SetKMode continuation and VC2
   event/list ownership, then install a bounded observation-only completion
   hook. Exercise its actual linked code and original continuation before
   asking for another hardware run.
3. Publish an A2 card using the shared VMU backend; first check detection and
   reads from an existing disposable card image with bounded diagnostics.
4. Add the foreground storage loan, then test save, restart, reload, and
   interrupted-commit recovery through the existing durable journal rules.

These steps remain open. The source audit's admission flags deliberately stay
false, including for padding patching and runtime card reads.

## Memory and buffering

The working VC2 reader has no speculative readahead. It still uses two
`0x220`-byte receive areas inside its `0x640`-byte foreground region. The older
CE asynchronous reader's spill ring is 2 KiB; the native background reader's
larger prefetch lease is a separate design. The 48 KiB writable VMU helper
reservation pays mainly for code, state and private stacks (including a 4 KiB
worker stack and a 1.5 KiB callback stack). Reducing a playback buffer does not
establish ownership or lifetime for those objects. Buffer tuning should follow
measured workload behavior once the same reader/card route is stable.
