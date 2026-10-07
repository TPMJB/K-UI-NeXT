# Profiles 11 and 12 preservation during profile 13 work

Independent fresh builds of the previously delivered profiles were made on
2026-10-07 from the working source tree containing the new profile 13 changes.
The older `Makefile.cdda` targets were used with separate build directories,
the pinned SH-4 compiler, and the original fixed build identifier
`a0c36063a9ed`. Both native checks and runtime-package verification completed.

| Profile | Fresh build directory | Runtime bytes | SHA-256 |
| --- | --- | ---: | --- |
| 11, generated disc | `build/cdda-preflight-preserve11` | 3,086,464 | `bbd3c0b57c09ab8d0d1f6cbb9cc162bf1d45e09d0070a8764c0a51b52f168cf3` |
| 12, selected Toy track 14 | `build/cdda-preflight-preserve12` | 3,084,000 | `514ea78a259fa06781a36be2d5b7ab50771bb4bc12d403984f30828d22a02039` |

Both SHA-256 values exactly match the previously delivered runtime hashes.
This establishes byte-for-byte preservation of those two executable packages
for the original build identifier; it does not establish new console results
or retail compatibility.

Reproduction commands, with the pinned `sh-elf/bin` directory on `PATH`:

```sh
make -f Makefile.cdda cdda CDDA_TEST_PROFILE=11 CDDA_BUILD_DIR=build/cdda-preflight-preserve11 CDDA_BUILD_ID=a0c36063a9ed -j4
make -f Makefile.cdda cdda CDDA_TEST_PROFILE=12 CDDA_BUILD_DIR=build/cdda-preflight-preserve12 CDDA_BUILD_ID=a0c36063a9ed -j4
```
