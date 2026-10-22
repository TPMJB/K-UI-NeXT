# Reconstructed Armada 137 source baseline

This checkout reconstructs the Armada CE read-only A2 source from the retained
manager source snapshot and the ordered discovery, mapping, driver-state,
header, completion, traffic, DMA-gate, and batched-proof patches. It is not
claimed to be the exact original `137ca964caad` Git tree or history.

The 1,621 snapshot files matched their archive bytes before patching. All eight
patches applied without rejects. Of 109 latest patch postimages, 108 match their
retained Git blob prefixes. `src/loader/retail_stage.c` still differs because an
earlier pre-discovery change is absent from the available snapshot. Its expected
final prefix is `7741ecf`; the reconstructed file is `4361360`.

The inherited generic read-only header hardcoded A1. Reconstruction adds an
`#ifndef KUI_VMU_READONLY_ADDRESS` guard around that default, preserving A1
normally and honoring the existing A2 compilation option. The exact old header
text has not been recovered.

The complete chain, archive hashes, patch hashes, checks, and limitations are in
`docs/evidence/armada-137-source-recovery.json`. The Git branch is deliberately
an orphan recovery baseline; the original old commit object is unavailable.
The restored dependency and build directories are excluded from this commit.

Native binary equivalence against the retained 137 proof files is still required
before treating this checkout as a matched hardware comparison baseline. No new
control-mode logic is part of this baseline.
