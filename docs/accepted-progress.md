# Updater-only integration and preserved baseline

The source baseline is published main
`0045dd96937ee7485ecd4fe0432b28babea8646a`. This integration ports the updater
without merging the recovered ab1 loader/VMU/Smash development. The owner's
2026-11-04 instruction explicitly limits this merge to updater code.

| Area | Included change | Preservation boundary |
| --- | --- | --- |
| Installed versions | Checked banks, selection records, restart and rollback | Existing root readers and their absence remain shared |
| Package admission | Bounded namespace, sparse inheritance, old26/old34 compatibility | No exact-count gate and no new optional modules |
| Test selection | Folder browsing and one embedded .kup per ZIP | Legacy manual-copy ZIPs require conversion |
| GUI restart | Fully verify/reload selected GUI directly | Updates action120; restart action126 |
| Module paths | Three bank-aware existence checks in the existing launcher | No title-specific routing or new backend selection |
| Reader admission | Exact verified Standard/CDDA identities in the GUI | No reader code, build flags, memory geometry or binary changes |
| Root migration | Preserve system/optional boot override, then copy runtime | No timing-sensitive button required; all user files retained |

`docs/evidence/updater-preserved-main-source.json` records main's protected
source blobs and the four deliberately reviewed GUI glue paths. All other
protected sources and reader recipes must match main byte for byte. The four
binding files under resources/updater-bindings retain exact admission for the
already installed 233,964-byte Standard reader and native CDDA aliases.
These manifests are not permission to replace readers or infer an allowlist
from arbitrary SD files.

The owner confirmed that the earlier root repair GUI06cbd036bf0d booted and
resolved the reported cohort error. That binary remains their working repair;
this source-only main integration is a separate narrow port. No replacement
reader or new console test matrix is required. The owner reported a longer
than desired installed-version selection screen; this merge does not claim
to resolve its latency. A completed console CDDA-package round trip and
universal in-game return are not established by the root-boot confirmation.

The failed full-cohort updater substituted a wrong Standard reader and added
presence-sensitive modules. Its release/build recipes are not included here.
GUI-only packages and the manual root repair contain no KUI/apps files.
Readers, VMUs, journals, game metadata and settings are left in place.

The accepted compact 72-KiB VMU and combined284-KiB/cache192-KiB installations
remain distinct from VMU-only operation. This updater does not change any
service reservation, promote session-controls variants or add a universal
cacheless profile. Native/disc/CDDA/WinCE support remains route-specific.
