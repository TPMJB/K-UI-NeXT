# Shared CE GD completion lifecycle audit

Two production state-machine defects were reproduced from source91fa7cc6922b.
They concern completion ownership and terminal wakes in the shared CE reader.
They are separate from the VC2-specific GD loan arbitration fix in that build.

| Finding | Trigger and old result | Correction | Affected source configurations |
| --- | --- | --- | --- |
| P1: removed PIO callback permanently blocks an idle VMU loan | Complete a PIO39 read, acknowledge its handle, then unregister the callback before its next EXEC. Registration becomes zero while `callback_due` remains1. EXEC cannot consume a callback with no function, and the real CE VMU loan keeps returning BUSY despite no pending command, active reader or unfilled piece. | Clear the undelivered callback when its registration is removed. The native PIO implementation already does this. | Shared core compiled with `KUI_RETAIL_CE`; persistent loan blockage requires the CE VMU runtime. |
| P2: failed active DMA piece lacks its terminal DMA wake | Accept a DMA38 piece, consume its initial data-ready wake, then encounter an unrecoverable card error before that piece completes. The reader clears `xfer_left` and ends the command, but raises only the drive interrupt; the DMA owner receives no terminal DMA-end event. | Raise DMA-end when an error retires a piece that still has bytes outstanding, before clearing its remaining count. A failure during prefetch with no accepted piece retains its existing drive-only wake. | Shared core compiled with both `KUI_RETAIL_CE` and `KUI_RETAIL_GD_ASYNC`; includes CE background-reader and dependent audio/VMU builds. |

The first sequence produces `callback_due=1, loan_result=BUSY` on the old
production source and `callback_due=0, loan_result=OK` after the correction.
The second produces drive/DMA wake counts1/0 on the old source and1/1 after
the correction. The no-piece failure control remains1/0.

## Verification

Run the focused production-code test:

```sh
python3 tests/test_ce_gd_completion_lifecycle.py --report build/ce-gd-completion-lifecycle.json
```

It compiles the actual GD core, CE async reader, SCI stream, image cursor and
VMU loan under AddressSanitizer and UndefinedBehaviorSanitizer. The existing
SCI/card fixture supplies hardware behavior and CPU mappings. Each of the two
defect cases must fail against the complete historical core and pass against
the current core. A separate control checks that failures without an active
DMA piece do not gain a DMA-end event.

The old core is bound to full SHA256
`475de755ce2c8540dc271c6a5d1edaa2fcff67a7099795966c767a6cc2e50839`.
The default test reverses only the two exact source hunks and verifies that
full hash, so a fresh clone does not require an unpublished local commit.
`--baseline-ref` can instead name any historical Git ref with those same
complete source bytes.

The runner also requires byte-identical preprocessed GD core output for
native synchronous, native background, native session-controls and native
MMU/stream/VRAM configurations. This includes the source configuration used
by Smash Pack's VC2 and Swirl foreground readers. Neither fix changes those
native paths, the72KiB VMU worker, graphics copy policy or collection returns.

Retained native synchronous/async and CE synchronous GD tests, the CE async
reader fixture and the actual CE VMU reader/audio loan suites pass after these
corrections. These checks exercise software state and modeled peripherals;
they do not execute a complete CE kernel or establish console acceptance.

## Delivery boundary

This is a source follow-up. The91fa VC2 test package remains unchanged. No
installed CE reader cohort has been rebuilt or delivered as part of this
audit. Rolling out these shared corrections requires rebuilding the affected
CE resident roles and checking their real GUI selection and package inventory;
copying a core-source fix alone does not update any installed module. Neither
finding is claimed as the cause of the photographed VC2 freeze.
