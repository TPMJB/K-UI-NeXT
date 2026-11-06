# VC2 direct video-memory read

The owner's `b4bd96a0c4aa` photo stops after the memory-card check on FN6
for command38, with these hexadecimal values:

| Field | Value |
| --- | --- |
| Token | E0 |
| Packet / caller stack | 088AFE94 / 088AFE78 |
| Original driver return PC | 01CF1788 |
| Destination / piece bytes | 04575600 / 00080000 |
| Request / delivered bytes | 00080800 / 00000020 |
| Pending / streaming status / I/O error | 1 / 3 / 0 |
| Rejection | 109: physical destination admission |

The complete512KiB piece ends at`045F5600`, inside the retail64-bit
VRAM aperture`04000000..04800000`. The separate32-bit aperture is
`05000000..05800000`. KOS's primary`dc/pvr/pvr_regs.h` defines the
physical and uncachedP2 forms. Sega's Hardware Specification Outline
section3.4 specifies16/32/64-bit access and prohibits VRAM byte access:
https://segaretro.org/images/8/8b/Dreamcast_Hardware_Specification_Outline.pdf

The previous core admitted only main RAM for DMA. Simply accepting the new
address would still be incorrect: the adapter rebased every physical
destination into same-offset main RAM, and ordinary memcpy could emit byte
stores when a cursor source was unaligned.

## Implementation

`KUI_NATIVE_GD_VRAM_DMA` enables bounded VRAM DMA only in the experimental
native OCRAM reader. Other builds default to0. The whole piece retains
32-byte admission alignment. The two retail8MiB windows accept physical or
P1/P2 aliases and normalize to uncachedA4/A5 without flattening bank views.
Holes, mirrors, MMIO, TA apertures, audio RAM and crossings remain refused.

Dedicated mapper access3/4 distinguishes DMA validation/writes from CPU
parameters, results and PIO addresses. A translated CPU pointer numerically
in the04 range retains its existing virtual meaning. VRAM payload mapping
performs no main-RAM cache maintenance, and the adapter preserves its view.

Actual cursor callbacks can split at arbitrary file offsets. The VRAM sink
packs RAM source bytes into aligned volatile16-bit stores. An odd head/tail
uses16-bit read/modify/write to preserve the adjacent byte; the compact sink
uses that same halfword operation for every source byte. No VRAM byte
access, FPU register or store queue is used. Completion accounting and IRQ
publication occur after payload writes.

The profile uses the final128 bytes of unused firmware padding for code.
Its guarded352-byte stack is`8c007ea0..8c008000`; the upper16-byte guard
ends immediately before the owner's IP.BIN. No game heap or high-RAM
reservation is added. Compiler graphs and linked guards remain checked.

## Primary driver evidence and limits

The supplied original WinCE display driver maps04000000 and05000000 through
MmMapIoSpace. Original kernel MmProbeAndLockPages and GD scatter-builder
instructions can produce the photographed packet from a three-MDL chain:
32bytes of RAM,512KiB of VRAM, then2016bytes of RAM. This is evidence of a
supported address class, not recovery of the console's exact MDLs. Do not
claim that the particular512KiB block is a texture without observing its
file/contents or caller.

Tests cover core bounds/access modes, actual SCI/cursor delivery and errors,
unaligned source fragments, separate main-RAM canaries, and original-driver
instructions through the linked SH gateway. Authored MDLs, SD arrivals,
VRAM backing and event consumption remain model boundaries. Complete VC2,
real HOLLY bus timing, CDDA, virtual VMU and background-reader integration
require hardware validation. This build fixes the demonstrated rejection;
it does not certify universal compatibility.
