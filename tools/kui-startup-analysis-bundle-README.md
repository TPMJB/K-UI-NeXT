# Startup analysis helper

Keep `kui-startup-analysis-bundle.py`, `kui-native-startup-bundle.py` and
`kui-wince-startup-bundle.py` together, then run Python 3 from their folder:

```sh
python3 kui-startup-analysis-bundle.py '/path/to/Silver/disc.gdi' --output Silver-startup.zip
```

Use the original, unconverted raw2352 GDI for the same region/revision used in
the imported-save console test. The helper reads the IP header to choose the
native or Windows CE extractor; Silver's family is not assumed.

The native path copies only `IP.BIN` and the unique IP-selected boot file.
The Windows CE path copies `IP.BIN`, the IP-selected boot file and external
EXEs, with DLL/DRV/LIB candidates included within its byte budget. Its report
lists omitted candidates; extensions alone do not establish runtime code.
Both include the original `disc.gdi` and `startup-info.json` with file hashes.
Other game assets and audio payloads are not copied.

The image is read without modifying its tracks. Existing output paths are
refused; without `--output`, the output is `Startup-analysis.zip`. Only an
explicit valid Windows CE IP flag selects the CE path, whose reader validates
the image again. Every other native extraction error stops without a CE retry.

This prepares evidence for analysis. It is not a game patch, complete disc
verification, or a compatibility claim. Keep the same original image version
for analysis and the console retest.
