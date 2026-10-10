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
