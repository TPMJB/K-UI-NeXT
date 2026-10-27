# DOA2 filesystem metadata probe

This reads the reviewed native Dead or Alive 2 GDI and produces `DOA2-filesystem-metadata.zip`. It helps identify a memory partition that could safely hold the loader and virtual VMU. It does not enable the new reader or VMU for DOA2.

For the follow-up after the filesystem ZIP was supplied, collect the bounded resource-layout fields:

```sh
python3 kui-doa2-filesystem-probe.py /run/media/apollonius/Dreamcast --resource-layouts
```

Run this from the computer-side folder containing the two scripts below. The new default output is **`DOA2-resource-layout-metadata.zip`**, so the earlier filesystem report is preserved. If multiple DOA2 copies are found, the printed commands retain `--resource-layouts`; choose the intended `.gdi` file's command.

This optional mode reads the first 20 bytes of each `DOA2/*.CHR` file, then only the in-file integer pointer/record tables whose offsets and counts pass the bounds checks. These are the fields read by the original resource consumers; file lengths alone cannot bound their derived accesses. The ZIP contains normalized integer metadata, table offsets/counts, and hashes. It contains no complete assets, models, vertices, textures, audio, IP or executable payload. The original tracks remain read only.

Unknown, overlapping, unaligned, out-of-file or oversized table layouts are reported as unavailable with a reason. Available header words remain in the report. Upload the ZIP even if some entries are unavailable; that identifies the exact format question without reading unchecked ranges. The collector caps resource observations at 128 files, 4,096 entries per table, 64 KiB of combined tables per file, 2 MiB of attempted logical resource reads, and 4 MiB of actual source-track reads. Sector prefixes can cause reads beyond the selected integer fields; the report accounts for those reads separately. Capturing these observations does not reserve RAM or enable DOA2.

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

Without `--resource-layouts`, the resulting ZIP retains the original filesystem-only behavior. It contains one `startup-info.json` with all selected-disc ISO paths, LBAs and byte lengths, track geometry, and the verified startup identity. No executable, IP, audio or asset payload is copied. This default mode reads no resource headers or tables. Its only payload read is the boot file, to check its exact SHA256; the other reads are IP and ISO metadata.

The expected startup is 1,815,864 bytes, SHA256 `105056d9ffef86bf5e470fa28dc5907443156f9ab1c6bc009f73661d9c047a85`. Another startup revision is refused rather than treated as the reviewed input. A successful metadata capture does not prove RAM ownership, complete disc integrity or console compatibility.
