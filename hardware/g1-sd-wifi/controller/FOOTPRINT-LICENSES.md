The locally copied standard footprints are from KiCad's footprint libraries,
distributed under Creative Commons Attribution-ShareAlike 4.0 with the KiCad
libraries exception. See https://www.kicad.org/libraries/license/ and
https://gitlab.com/kicad/libraries/kicad-footprints/-/blob/master/LICENSE.md.

`FOOTPRINT-PROVENANCE.json` lists each original library and installed source path.
The files retain the upstream footprint geometry and metadata. New custom
footprints were drawn from manufacturer dimensional specifications; their
descriptions identify the drawings. Component pad audits cover physical pad
numbers, rather than assuming similarly named packages are interchangeable.

`RawProbe_Pad_D1.0mm_NoPaste` is project-defined fabricated copper, with the
1 mm Cu+Mask land geometry of KiCad's standard test pad and no fitted assembly,
silkscreen, paste or assembly courtyard. Its attribution and geometry record
is `design/sources/raw-g1-probe-footprint.json`; the KiCad library license and
exception above also apply to this derivative. Missing assembly courtyard is
explicitly allowed only for this bare probe footprint; copper, mask, via and
board-edge clearance checks remain enabled.

`HighDiode_SMF_SOD123FL` and `Littelfuse_467_0603_Recommended` use
manufacturer suggested/recommended lands, recorded with drawing axes and source
PDF hashes in `design/sources/input-protection-footprints.json`. Their drawing
conventions and generic model references derive from the KiCad `D_SOD-123F` and
`Fuse_0603_1608Metric` footprints; the KiCad license and exception above apply to
these derivatives. Manufacturer dimensional evidence controls the height budget;
the generic KiCad models are visual references, not exact manufacturer models.
The bidirectional High Diode part has no electrical polarity indication in its
custom footprint. These dimensional sources do not approve paste/reflow,
installed fit, or circuit fault/thermal performance.
