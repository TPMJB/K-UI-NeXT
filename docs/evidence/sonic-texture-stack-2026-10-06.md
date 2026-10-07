# Sonic second scoped-stack experiment

## Trigger and scope

The owner supplied Sonic Adventure and Grandia II photographs of build `c942c682187d` on 2026-10-06. Sonic's three captured return words match the statically identified chain in `sonic-parameter-stack-2026-10-06.md`. Its enclosing routine at `0x8c09b3f0` allocates a large local frame; the combined chain places the SDK request at `0x8c00b9f4`, within the resident reader's manifest.

The earlier one-shot scope at `0x8c094c88` addresses a different startup frame and remains necessary. This experiment adds a separate one-shot scope around the first invocation of `0x8c09b3f0`. It is gated by the same exact executable size, IP CRC and executable CRC. Hooks save the verified owner's original windows from RAM; no game instructions, executable bytes or disassembly are distributed.

## Lifetime and ABI constraints

The new routine takes register arguments R4/R5 and does not read stack-passed arguments above its entry SP. Its caller frames remain on the original stack. A separate 32 KiB owner stack preserves the first scope's backing and state, including SDK scratch pointers that may remain after the earlier routine returns. The scope preserves the actual returned integer CPU frame, original SP/PR and cache mode without touching floating-point state.

This routine has multiple runtime callers. Permanently hooking it into temporary stage code would be unsafe: compressed input grows downward from `0x8cf00000`, and larger inputs can reach the high stage. The experiment therefore protects only its first invocation. It removes its entry hook before delegation and removes every remaining temporary guard before return. Later invocations use the original routine and may encounter the same low-stack conflict. Advancing the first invocation is evidence of progress, not proof of gameplay compatibility.

The routine builds temporary descriptor pointers into its local workspace. The original implementation also leaves these pointers after its stack frame returns. Separate retained backing preserves the experiment's temporary descriptors; the experiment does not introduce a global persistent stack reservation or claim that every later reuse is safe.

The new hooks remain armed between the earlier scope and the first texture invocation. Static analysis of the direct initializer return chain finds fixed global, registry and hardware writes rather than an explicit stage-buffer write. However, later resource/event startup uses dynamic callbacks, and the supplied photograph does not identify the texture routine's immediate parent. Every alternate path through that interval has not been proven to preserve the high stage. The safeguards below cover writes inside the identified texture call; they do not establish an unconditional stage lifetime across all startup paths. This is an exact-owner hardware experiment.

## Guards before writes

The compressed-input checkpoint runs after the owner obtains the byte size and before it computes or submits its read destination. The decompression checkpoint runs after the input read/close and before output writes. These checkpoints must validate arithmetic, complete input/output ranges and stage separation before delegating the original instructions. A bounded dry run of the input stream establishes its output size without writing decompressed bytes. Malformed or out-of-range input must stop before the corresponding write.

Private-stack bounds and canary checks, plus the existing immutable IP/code and pinned-manifest checks, remain required. Mutable reader counters and caches may change normally. Successful scopes restore all installed owner windows; terminal reports do not resume game I/O.

## Grandia II report

Grandia's captured SP `0x8c00b338` also lies inside the resident reservation. Rejection branch `3` establishes that its submitted parameter array could not be mapped; the prior Sonic-only capture did not reveal its actual pointer. The generic native caller report now carries the original dispatch R5 and captures three bounded raw stack words before reusing the cache or restoring video. The words are observations at fixed offsets, not an automatic backtrace or an inferred Grandia call chain. Grandia's enclosing routine remains unpatched.

## Shared layout lead

DreamShell's source provides a concrete candidate for a later general remedy. At commit `4a2b898cbc244b2fb9bd1698b45e5325056232fb`, [loader main.h](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/firmware/isoldr/loader/include/main.h) defines the syscall-reserved boundary at RAM plus `0x4000` and IP at RAM plus `0x8000`; [isoldr.h](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/include/isoldr.h) defines the low loader address as `0x8c004000`. Its [main.c](https://github.com/DC-SWAT/DreamShell/blob/4a2b898cbc244b2fb9bd1698b45e5325056232fb/firmware/isoldr/loader/main.c) selects full syscall emulation when its loader or heap is below that low address.

K-UI's standard resident plus private-stack reservation spans `0x3d00` bytes. A reservation at `0x8c004000..0x8c007d00` would fit below IP and clear both photographed game stacks. This is an architectural candidate, not a verified K-UI placement. Physical firmware scratch and BIOS variants, menu/GINSU/full-IP reload ownership, package/linker variants and guest-memory protections need a separate audit. The present experiment retains K-UI's existing layout.

## Console test

Replace the entire `KUI` folder on the SD card, restart using the existing compatible boot disc, and launch the same original Sonic GDI with the standard reader. Report whether startup advances or photograph the full stop screen. Test Grandia II separately with the standard reader and photograph any `NATIVE CALLER STACK` report.

This development build does not claim a launch-speed improvement, a general stack-layout fix or confirmed gameplay compatibility. Physical-console validation is pending.

## Validation

The scoped-stack fixture passes 105 synthetic cases and 1,000 generated compressed streams with UBSan. Cases cover the exact photographed entry depth, P1/P2 original stack aliases, separate retained backing/state, all 21 returned CPU words, cross-scope exclusion, early removal of unused guards, allocation alignment/overlap, input-size arithmetic and source boundaries, malformed streams/output limits, and immutable-reader/canary/cache/frame failures.

An independent instruction-level emulator of the privately supplied owner's decoder validates the corrected dry-run parser across 750 mixed streams and their truncations and output boundaries: 150,996 checks and 17,833,992 interpreted instructions. The parser's control bits are literal `1`, short reference `00`, long reference `01`, and the zero-word terminator uses the long-reference prefix. Independent verification caught and corrected an initially reversed second control bit before hardware testing. No private executable or decoded data is included in source or deliverables.

The production-derived native caller fixture passes 23,766 checks with UBSan, optimization, LTO and strict aliasing. It verifies actual R5, both photographed stack depths, pointer values including null and aliases without dereferencing them, aligned P1 bounds, snapshot overlap and omitted captures for other functions/rejections. All 11 workflow-hygiene checks and whitespace validation pass.

Native and CE linked builds pass layout, embedded-payload, instruction, stack and unresolved-symbol checks. The SCI resident still ends at `0x8c00baf4` with a conservative private-stack bound of 1,184 of 1,232 bytes. All private allocations are aligned and disjoint. Ordinary native trace modes 0–3 and CE modes 0/3 retain the baseline allocated sections and relocations. Native background and CE SCI standard/background resident payloads remain byte-identical. The wrappers preserve the actual 21-word CPU frames and publish restored hooks on the texture return path.

The diagnostic workflow runs both focused host fixtures for the exact commit. Final native audit and exact-commit GitHub build results are recorded with the delivered archive; physical-console validation remains pending.

## Hardware outcome: build d6438f7022ac

The owner supplied a clear Sonic photograph and Grandia II photograph from this build. Both stop on GD function 0, command 0x11, rejection 3 (parameter mapping). Sonic reports SP `0x8c00b9f0`, actual R5 parameter pointer `0x8c00b9f4`, PR `0x8c648d7a`, and raw stack words `0x8c604e50`, `0x8c604c98`, `0x8c09b4de`. This is the same failing chain as the previous build. The failed invocation is outside the active protected scope; this experiment has not solved the collision. The photograph cannot distinguish an earlier invocation consuming the one-shot scope from a skipped or overwritten hook.

Grandia II reports SP `0x8c00b338`, actual R5 parameter pointer `0x8c00b33c`, PR `0x8c07cb22`, and raw stack words `0x8c08529c`, `0x8c0850e4`, `0x8c01138a`. This confirms that its actual parameter array overlaps the legacy reservation, rather than merely showing a nearby caller stack. Its enclosing routine has not been identified.

The next isolated experiment moves the native resident below the complete owner IP image. It disables both Sonic scopes and changes no owner instructions. See `native-low-resident-2026-10-06.md` for the placement contract and firmware admission checks.
