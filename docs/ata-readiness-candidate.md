# ATA readiness release candidate

This is a development release candidate based on K-UI 1.5.1, not a new stable
release. **ATA/IDE/CF hardware is untested, and the SCI512 hardware result is
pending.** Keep the known-working release and the existing SCI comparison
build available for rollback. The console still displays version 1.5.1;
identify this candidate by the exact source build ID in `build.json` and on
the launcher, and by `ata-readiness-candidate` in the download name.

## What changed

The ATA changes are synchronous PIO preparation and optimization:

- Check the ATA IDENTIFY feature-word validity before trusting its capabilities.
- Preserve the existing bus selection when the GD-ROM master is busy.
- Read a bounded stream of up to eight sectors, consuming exactly 512 bytes
  per sector callback. Drain any outstanding stream tail before a stop or a
  new LBA request.
- Allow a bulk PIO sector callback that uses native 16-bit MMIO, removing the
  indirect call for each data word. Keep the ordinary PIO path as a fallback.

These changes do not establish board compatibility or a measured speed gain.
The stream still performs synchronous PIO; native ATA DMA and asynchronous
ATA DMA are not implemented in this candidate. A separately gated, bounded
DMA hardware diagnostic is a future milestone before considering a background
ATA reader. It will need real hardware results and a safe PIO fallback.

The package also carries the current SCI Windows CE test payload, including
the pending 512-byte token-polling comparison. Its inclusion does not mean that
comparison passed. Windows CE remains **SCI-only**; CE boot from IDE/CF is not
supported. Native game loading and Windows CE testing remain separate paths.

## Install and preserve rollback

1. Save the current working `KUI/runtime.kui`, Games payloads and downloaded
   package before updating. Preserve any known-working `KUI/recovery.kui`.
2. Merge the supplied `KUI` directory into the storage root. Update
   `KUI/runtime.kui`, `KUI/apps/games/retail-boot.kui` and, for CE testing,
   `KUI/apps/games/ce-probe.kui` together from this candidate. Preserve existing
   preferences, covers, music selections and game dumps. No reformat is needed;
   runtime applications still require FAT32/exFAT.
3. Keep the existing working SCIF boot CD, or the existing compatible SCI
   bootstrap for SCI testing. The optional `boot-cd` image in the candidate
   bundle is separately named for this candidate; a compatible bootstrap is
   required to attempt IDE/CF boot. Do not assume an older SCIF-only CD supports
   SCI or IDE. Read `STORAGE-TRANSPORTS.md` before changing hardware.
4. Check that the launcher shows the source build ID from this package's
   `build.json` and the expected selected transport. Do not replace both the
   normal runtime and a working recovery runtime during this update.

For the pending SCI512 comparison, use the same CE title and intro segment as
the prior comparison, then record playback/audio behavior and the return
counters. `WINDOWS-CE-PLACEMENT-TEST.md` describes the SCI-only CE entry and
reader choices; its older experiment narrative is historical. This candidate
does not request another broad soak or claim that the pending comparison has
already succeeded.

ATA hardware testing remains future work until the board is available. No
ATA timing, throughput or compatibility result is supplied by host tests or a
successful native build. A DMA diagnostic is not included in this package.

## Files and source identity

The candidate installation bundle contains the normal runtime and native
Games payload, the separate CE test payload, menu music, catalogues, an
optional boot CDI, installation notes, license records and checksums. It does
not install the SD benchmark in place of normal game launch or include scan
fixtures, preferences or game dumps. The workflow's separate `sd-update`
artifact remains the development package with diagnostic fixtures and demo
music; its candidate notes and build identity describe the same source.

`build.json` classifies this build as `release-candidate`, records its exact
commit, and keeps `hardware_tested` false. `SHA256SUMS` covers the installed
bundle, and `SHA256SUMS.txt` covers the downloadable installation/source ZIPs.
The matching `-source.zip` contains corresponding source, pinned dependency
records and licenses. Follow `SOURCE.txt` for the exact source revision.
The existing v1.5.1 release and its recorded hardware evidence are unchanged.
