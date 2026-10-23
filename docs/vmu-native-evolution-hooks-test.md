# Evolution: active hooks without a virtual card

On `5b653775af85`, normal mode again failed before the first FMV, while
`--lease-only` worked. That comparison makes the active hooks/runtime the
stronger suspect. It does not yet identify which hook, virtual response or
storage interaction causes the failure. Evolution compatibility remains open.

## Test this mode

Extract this package into a fresh folder. Run from that folder:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1 --hooks-only
```

Use the same Evolution GDI, SCI/Standard, Auto encoding and controller setup.
Keep physical A1 unplugged, and keep CDDA, background loading and RAM patches
off. The startup banner should say:

```text
ALL HOOKS ACTIVE / VIRTUAL VMU OFF
```

Check whether the opening movie plays and the title/menu appears. Photograph
any diagnostic page. There is no virtual card in this comparison; do not test
virtual saves. A normal missing-VMU message is expected if the game checks for
a card. The prepared save files remain on the SD card.

| Mode | Same worker reservation | Active game hooks | Virtual card |
| --- | --- | --- | --- |
| Normal | Yes | Yes | Yes |
| `--lease-only` | Yes | No | No |
| `--hooks-only` | Yes | Yes | No |

The new mode retains the submit/completion gateways, free/reinitializer/exit
protections, copied physical DMA chain and the existing ownership/context
checks. It hides A1 and calls no virtual-card backend or in-game VMU storage
transaction. Filtered A1 receive headers explicitly become `FFFFFFFF`, the
no-response marker, so the SDK cannot consume an old virtual reply. Parent
controller responses, receive payloads and unrelated ports remain unchanged.

Normal prepared-card validation/recovery still runs before the game starts;
this is not a promise that startup never repairs a journal. The personalizer
itself changes only the loader module, not the prepared card files.

If this mode works, virtual visibility/replies and their storage path become
the next focus. If it also fails, the shared active hook/runtime path remains
the focus. Neither result alone establishes a complete fix.

To restore ordinary virtual-VMU mode, run the same command without
`--hooks-only`. This package also retains `--control`, `--profile-runtime`
and `--lease-only`; older packages refuse a new mode they cannot execute.

## Validation and scope

The package includes the exact source checkpoint and compiled-code audit in
the full archive. Host tests cover stale A1 headers, unchanged controller and
payload bytes, zero backend/storage activity, refusal before publication,
alternating banks, and normal save/load behavior. Linked-instruction checks
cover immutable mode selection, worker policy, hook return paths and the
no-card completion route. The release packager repeats relocation, stack,
mutation and source-provenance checks.

This is a diagnostic comparison. It adds no game SDK family, virtual slot,
transport, CDDA coexistence or physical-disc support.
