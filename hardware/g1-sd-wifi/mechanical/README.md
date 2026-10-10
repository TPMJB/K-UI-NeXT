# Carrier fit sheets

[US Letter](carrier-fit-US-Letter.pdf) and [A4](carrier-fit-A4.pdf) contain the actual native carrier Edge.Cuts and component-pad positions at 1:1 scale. Use the version matching the printer's paper and print the PDF with **Actual size / 100%**, with Fit and Shrink disabled. Measure both 50 mm calibration bars before cutting the outline; do not apply the earlier image's 105% or 118% workaround to these PDFs.

The blue BIOS/CN503/module outlines are provisional references. The complete perimeter, parts beneath the board, north-south registration, shield clearance and independent support still need a fit check. This template does not establish that a tall existing capacitor clears the nominal 2.5 mm carrier underside. A paper fit also does not establish contact compression or electrical isolation. No additional exposed-motherboard measurement is requested to complete the circuit and routing work.

The C5's sixteen spring tips use the manufacturer's underside pad coordinates. A removable clamp must maintain a supported 0.90 mm gap, alignment within the contact design limits and at least 6.24 N total reaction. Its retention fixture is separate from JLC's required factory assembly fixture for Harwin S7221-45R contacts. Neither has been qualified by this drawing.

Regenerate the PDFs after layout changes with `/usr/bin/python3 hardware/g1-sd-wifi/tools/generate_carrier_template.py`. [template-provenance.json](template-provenance.json) records the native PCB hash. The drawing never rescales geometry to compensate for a printer.
