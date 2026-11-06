# Sega Swirl nested CE VMU source profile

This separate compile-time profile uses the same outer Sega Smash Pack menu
and foreground SCI reader. It selects Swirl's original kernel, Maple driver
geometry and native MAINANA boundary; VC2 remains the default profile.
`KUI_SMASH_SWIRL_VMU=1` selects it explicitly.

The exact kernel is `SWIRL/0WINCEOS.BIN`, 1,828,864 bytes, SHA256
`a19080f2e243c04e01a3a473c7a3caa36fb85a5a8fbb2163c8f7a1b2c8ed5ea8`.
The prefix is 2,048 bytes and its body is `0x1be000` bytes at `8c010000`.
ROMHDR is `8c1cd61c`, with the original RAMend `8cfd0000`.

Swirl's Maple worker/completion/SetKMode/Wait paths retain the reviewed VC2
instruction roles at different RVAs. All decoded packet and object geometry
remains the same: extension `0x1a0`, function `0x150`, unit records 28 bytes,
self-thread HANDLE `+180`, DMA event `+178`. NK's KData is `8c1d1800`, current
thread `8c1d1894`, current process `8c1d1890`, process table `8c1da6e0`.
The source auditors bind complete file and stored module identities, reviewed
windows, import/export ordinals and independent scalar decoding.

Swirl's apparent Maple stored-text padding is occupied by other modules'
E32/O32 metadata. It must never receive the bridge. Before NK executes, the
Swirl installer reserves `8cfb8000..8cfc0000` below the existing helper state,
copies only the meaningful `0x6c62` bytes of original Maple text there and
zeros the rest of the complete 32 KiB interval. The authored bridge goes in
that private source at nominal RVA `7c64`; the copied worker is patched at
RVA `7270`. It publishes the complete code before changing the original O32
text pointer to `8cfb8000` and virtual size to `6e00`. The nominal Maple base,
original O32 real-address field and complete original text remain intact.
The only original kernel changes are RAMend, text virtual size and text source
pointer. RAMend becomes `8cfb8000`; OEM globals and helper/stack addresses keep
their existing positions. Original packed metadata remains byte-for-byte exact.

`test_swirl_nk_text_mapping.py` executes the actual original NK O32 mapper and
VirtualCopy path. Both original and relocated sources publish seven identical
virtual pages, with the expected changed physical frames. The complete mapped
page tail is explicitly zeroed. This scalar proof does not model hardware
caches, MMU exceptions, interrupts, Maple or actual game persistence.

`test_swirl_ce_vmu_prepare.c` calls the actual production installer with an
explicit independently guarded host sink. It compares every original kernel
byte, all copied meaningful text, the bridge and the complete private tail.
It checks source rejection, duplicate install rejection, pointer-last code
publication, reserve-only behavior, seven injected publication failures with
exact rollback, invalid sinks and both guard intervals under ASan/UBSan.
The existing VC2 installer tests pass with the default profile unchanged.

The source and host tests establish these narrow implementation contracts.
Swirl card detection, saving and loading still require the console run.

## Collection exit behavior

Normal VC2 and Swirl collection children use the confirmed low E0 terminal
entry: it raises BL and IMASK, switches to the existing resident stack and
calls its diagnostic renderer directly. They do not install or zero a second
return program in `8c008300..8c00c000`. The read-only CE observer remains an
explicit `SMASH_CE_RETURN_PROBE=1` diagnostic and is off in normal deliveries.

`test_smash_ce_terminal_entry.py` executes the actual linked E0 entry across
command1/reset3, caller stacks, SR modes and cache profiles. An optional
confirmed-resident ELF checks the same mask, stack and argument contract.
Normal collection qualification also requires the observer image/state to be
absent from the cold stage. These checks do not establish console rendering.

Returning from CE to the collection menu remains unimplemented. The terminal
entry does not retire the CE worker, drain storage and Maple ownership, or
cold-load the original menu. Restoring this entry is not a claim that the
reported Swirl freeze's cause has been established or that menu return works.
