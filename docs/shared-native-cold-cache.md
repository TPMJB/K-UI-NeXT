# Native cold immutable verification

The operand-cache policy now follows the native cold-stage contract rather
than the VMU extent-proof option. It applies to raw native images, standalone
New, ZSO/LZO and the three native-CDDA profiles. It does not select titles,
change image checks, reserve game RAM or modify a playing-time reader.
CE and live physical-disc execution retain their separate cache contracts.

`retail_stage.S` publishes RAM, selects the independent P1 high stack and enters
the post-Bootstrap2 relay with instruction caching on and operand caching off.
The existing whole-CRC scope admits only privileged, masked, MMU-off execution
with exact CCR `0x100`, a bounded P1 source and a bounded P1 stack. Unsupported
states call the same complete-byte CRC without changing CCR. Its eight-byte
leaf stack and complete private-stack cache-line publication are checked from
the actual linked instructions. Returning from the scope restores `0x100`;
the outer relay still restores the game's original CCR and complete CPU frame.
Native Standard/Background retain their existing scope and checks.

Standalone New keeps both fresh SDK-family proofs. Initial stage entry already
enables operand caching: its first proof keeps that profile unchanged. The new
guarded finder scope principally covers the second proof after Bootstrap2,
when the relay has disabled operand caching. It forwards all four arguments
and the exact result, bounds all sixteen output bytes outside the image and
publishes private stack/output before restoring CCR. The pure finder has no
live slots, VBR, storage or hook publication; those operations remain outside.
No proof result is reused across executable restoration.

## Build and qualification coverage

The seven additional native cold roles are:

| Role | Production recipe / target | Linked CRC caller |
| --- | --- | --- |
| Ordinary VMU Off | `Makefile.dc`: `retail-physical` | `kui_retail_stage_relay` |
| Standalone New | `Makefile.native-standalone-general`: default target | `kui_retail_stage_relay` |
| ZSO | `Makefile.dc`: `build/retail-zso/entry.elf` | `kui_retail_stage_relay` |
| LZO | `Makefile.dc`: `build/retail-lzo/entry.elf` | `kui_retail_stage_relay` |
| Native CDDA, raw and cooked data | `tools/build_native_cdda_compat.py`: `toy-raw`, `toy-cooked` | `kui_toy_pilot_base_relay` |
| Native CDDA, independent reader | Same producer: `mdk2` | `kui_toy_pilot_base_relay` |

The separate five-service cohort remains Standard SCI, Background A1/A2 and
physical-disc VMU A1/A2. Their snapshot policy and live-worker preservation
require their own qualification. All CE and accepted Smash installed bytes
are retained; rebuilding these seven roles does not qualify a CE replacement.

Run `tests/test_native_stage_cached_crc.py --elf STAGE --relay _CALLER
--old-elf OLD_STAGE --report REPORT` for every additional role. It binds the
actual complete-image call, executes the linked cache/CRC instructions and
rejects changed caller, guard, leaf and publication controls. Retained old
artifacts must still pass full CRC equivalence and fail the new scope gate.

For standalone also run `tests/test_native_standalone_sdk_cache.py STAGE
--old-stage OLD_STAGE --report REPORT`. It checks the actual four-argument
caller, both emitted fresh calls, closed finder stack graph, complete output
bound and cache/publication controls. The original-backed early IRQ test runs
that independent linked audit before composing the wrapper with its pure
production-C finder oracle, then executes the original installer and linked
late binder. The EGG, HOTD2 and Time Stalkers originals cover both handler modes;
the Sonic Adventure original remains unavailable.

Exploratory qualification is under `build/native-cold-cache-audit`. Final
delivery must repeat these checks against clean source and the exact packaged
modules, including GUI admission, all aliases, live-worker preservation and
unchanged CE/Smash files. Model counts and host execution do not measure the
reported twenty-second bootstrap interval or establish console acceptance.
