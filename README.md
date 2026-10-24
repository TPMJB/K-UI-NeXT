# K-UI native VMU checkpoint: 74958b387827

This is a preservation checkpoint on `checkpoint/native-vmu-20261024` in
`TPMJB/K-UI-NeXT`. It preserves the exact source tree and all 36 original
source commits through `74958b3878279fc916199a7cd330f7f17b036138`, matching
compiled builds, generated test evidence, and the recovered EDF reference.
The checkpoint container is a separate Git commit; its own SHA is not the
original source SHA. This checkpoint does not merge changes into `main`.

## Compiled builds

- [Latest Background fallback limit, 74958b387827](builds/K-UI-Native-Background-Bounds-74958b387827.zip?raw=true).
- [Previous Standard and Background comparison build, 9fa65fc1cae0](builds/K-UI-VMU-Retry-9fa65fc1cae0.zip?raw=true).

The latest candidate retains background DMA and limits synchronous PIO fallback
to one SD data block per native loader call or reader interrupt. Tests
reproduced the previous whole-request masked-work defect and verified bounded
fallback, byte-exact completion, CRC recovery, guest DMA ownership and unchanged
DMA-only scheduling sequences. The linked memory and stack checks pass without
relaxing boundaries. These results do not establish a console performance fix.

The prior test reported smooth Evolution 2 FMVs with Background, with occasional
gameplay stalls. The latest report after the 74958b387827 candidate likewise
says FMVs remain fine but gameplay hangs remain. The build association follows
the delivery/test sequence; no runtime build screen was supplied with that
report. The gameplay performance problem is still open. See `HARDWARE-TESTS.json`.
The prior build is preserved for direct comparison. The user's earlier
flawless EDF FMV report used **regular Standard loading**, not Background.
The original regular-loading performance difference remains unresolved.

Follow the README inside the selected build ZIP to personalize its loader with
an existing prepared card profile. The latest package requires Background On;
it does not add a new in-game mode switch. Virtual A2 and physical A1 passthrough
remain included. This recovery script is not a device installer.

## Exact source recovery

Clone this checkpoint branch and run the following from this folder:

```sh
python3 restore.py
```

The script validates all preserved files and part hashes, reconstructs the
base history bundle and exact historical EDF source archive, imports the
retained main prerequisite and original source bundles, then checks out the
exact latest source in `restored-source`. It refuses to replace an existing
source directory. The original source tree, 36-commit count and Git object
integrity are checked after recovery.

The main prerequisite `194a94483132c8dd30b0a21d7c20887c0e1eaed3` is retained as
the checkpoint container's parent, keeping shared Git objects reachable. The
base compact bundle preserves the exact prior 35 source commits. The small
incremental bundle adds the original latest commit without recreating authors,
dates or commit IDs.

Latest source tree: `0640a3f8bf9cc53dd8701ee751af95e936491867`.
All payload identities and file hashes are recorded in `checkpoint.json`.
The large standalone Full ZIPs are not duplicated here: their unique source,
history and historical reference payloads are preserved directly instead.

## Historical reference and audits

`reference/edf20f0f11a3` preserves the exact recovered EDF module and source
archive. Its corrected `provenance.json` explicitly records regular loading.
`provenance-original.json` is an unchanged archival record whose earlier
Background interpretation is superseded; its payload identities remain valid.

`audit/edf/EDF-STANDARD-BINARY-AUDIT.md` describes the exact historical Standard
reconstruction and comparisons. The supplied corpus image's raw2352 format
does not establish which image variant was launched in either console test.
The safe reconstructed Standard ELF/bin/map are inspection evidence, not a
replacement loader package. Unsafe SCEV experiment executables are excluded.

`audit/background-bounds` records the reproduced defect, regression cases and
final linked checks. The work limit bounds PIO data blocks, not elapsed time;
legacy command/token/recovery waits remain. Preserved DMA-only scheduling does
not establish movie performance when a read itself falls back to PIO.

No user VMU image, save file, disk ownership profile, retail game executable or
game track is included. Original game files were used read-only for analysis
outside this checkpoint.
