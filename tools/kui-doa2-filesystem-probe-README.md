# DOA2 filesystem metadata probe

This reads the reviewed native Dead or Alive 2 GDI and produces `DOA2-filesystem-metadata.zip`. It helps identify a memory partition that could safely hold the loader and virtual VMU. It does not enable the new reader or VMU for DOA2.

Keep these files together in a folder on your computer, outside the SD card:

- `kui-doa2-filesystem-probe.py`
- `kui-native-startup-bundle.py`

Run from that computer folder, substituting the SD drive or mount path:

```text
python kui-doa2-filesystem-probe.py E:/
```

On a system where Python is named `python3`, use that command instead. The collector searches the SD root for a unique GDI with the exact reviewed DOA2 IP title and startup SHA256. It accepts cooked 2048-byte data tracks and original Mode1 2352-byte data tracks. Tracks remain read only.

If there are multiple copies, the probe prints a ready-to-run command for each exact matching GDI, retaining your output filename. Choose one command. You can also select the intended GDI explicitly:

```text
python kui-doa2-filesystem-probe.py --gdi "E:/Games/DOA2/disc.gdi"
```

`--gdi` takes the full path to a `.gdi` file, not the SD root or a game folder. To search the whole SD card, pass its root without `--gdi` as in the first command.

Keep the output ZIP on your computer, outside the card. The default output is the current computer folder. An existing ZIP is preserved; use `--output` with a new filename for another capture. Output inside the scanned SD root or explicit game's source folder is refused.

Upload the resulting ZIP. It contains one `startup-info.json` with all selected-disc ISO paths, LBAs and byte lengths, track geometry, and the verified startup identity. No executable, IP, audio or asset payload is copied. No resource header bytes are collected because no format-specific header fields have yet been selected for review. The only payload read is the boot file, to check its exact SHA256; the other reads are IP and ISO metadata.

The expected startup is 1,815,864 bytes, SHA256 `105056d9ffef86bf5e470fa28dc5907443156f9ab1c6bc009f73661d9c047a85`. Another startup revision is refused rather than treated as the reviewed input. A successful metadata capture does not prove RAM ownership, complete disc integrity or console compatibility.
