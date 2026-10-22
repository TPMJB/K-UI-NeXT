# Native startup analysis helper

For automatic native/Windows CE selection, use
[`kui-startup-analysis-bundle.py`](kui-startup-analysis-bundle-README.md).
This native-only helper requires an IP header with the Windows CE flag clear.
The Silver command below is an example only if its header admits the native
path; its family has not been established. From the `tools` folder, run Python 3
against the original, unconverted raw2352 GDI for the same region/revision used
in the imported-save test:

```sh
python3 kui-native-startup-bundle.py '/path/to/Silver/disc.gdi' --output Silver-startup.zip
```

The self-contained helper reads the image without modifying its tracks. It
copies only `IP.BIN` and the unique IP-selected boot file, plus the original
`disc.gdi` and `startup-info.json` with file and ISO metadata hashes. Other game
assets and audio payloads are omitted. Windows CE images, unsupported or ambiguous
startup metadata, and existing output paths are refused. Without `--output`, the name is
`Native-startup.zip`.

Use the same original image version for analysis and the console retest. This
prepares evidence for analysis; it is not a game patch or a compatibility claim.
