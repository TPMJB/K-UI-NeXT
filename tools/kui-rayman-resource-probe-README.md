# Optional Rayman 2 FIX.DAT metadata

Test the new Dreamcast candidate first. Use this computer-side collector only if Rayman 2 still fails and this report is requested. It does not install anything on the Dreamcast or change the tracks.

From the extracted folder:

```sh
python3 kui-rayman-resource-probe.py /run/media/apollonius/Dreamcast
```

The output is **Rayman2-FIX-layout-metadata.zip**. Existing reports are never overwritten. Upload that small ZIP if the hardware test still fails.

If multiple exact copies are found, the tool prints a quoted command for each actual `.gdi` file. Run the command for the intended copy. `--gdi` requires the complete file path, including its filename:

```sh
python3 kui-rayman-resource-probe.py --gdi '/run/media/apollonius/Dreamcast/Games/Rayman 2/disc.gdi' --output Rayman2-FIX-layout-metadata-2.zip
```

To scan the SD root, use the first command and omit `--gdi`. Keep the output outside the source SD/game folder.

These three scripts must stay beside one another:

- `kui-rayman-resource-probe.py`
- `kui-doa2-filesystem-probe.py` — shared read-only GDI/ISO reader.
- `kui-native-startup-bundle.py` — shared ISO parser and atomic ZIP writer.

The collector verifies the exact reviewed Rayman IP and startup SHA256, records ISO file sizes/LBAs, and observes only the first **256 bytes of DCDATA/FIX.DAT**. The ZIP contains JSON with 64 little-endian integer words and a prefix hash. Fields at `0xa0`, `0xa4`, and `0xc0` are labeled from their original source consumers. No pointer target is followed and no complete asset, executable, IP, logo image, audio, or video is exported.

The live game allocation base and descriptor-table contents remain unavailable in this host-only report. Header pointers alone cannot establish whether the old heap reservation moved an asset or whether any runtime pointer is valid. This is analysis metadata, not a compatibility result.

Original Mode1 2352-byte tracks and cooked 2048-byte tracks are supported. The collector selects at most one asset header, bounds selected-disc metadata to 2 MiB logical, bounds all selected-disc sector reads to 4 MiB, and bounds the complete automatic scan to 32 MiB of sector reads. Unknown identity, malformed geometry, ambiguous paths, existing output, or exceeded limits cause a refusal without an output ZIP.
