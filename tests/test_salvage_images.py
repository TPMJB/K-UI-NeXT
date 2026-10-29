#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Separate salvage producer/repair worker on real FAT32/exFAT fault images."""
import argparse
from pathlib import Path
import shutil
import importlib.util
import zlib
import tempfile
from test_images import run
from make_recovery_vectors import sector, mode2_sector

ROOT = Path(__file__).resolve().parents[1]
CASES = (
    "trial-balanced", "trial-single", "trial-invalid-selection", "trial-partial", "trial-all-bad", "trial-invalid-partial", "trial-unsupported", "trial-two-profiles", "trial-profile-lost",
    "trial-qualify-fail", "trial-qualify-skip", "trial-identity-mismatch", "trial-select-fatal", "trial-fatal", "trial-conflict",
    "trial-stop-read", "trial-stop-qualification", "trial-select-stop", "trial-stop-before-read", "trial-budget",
    "trial-unfinished", "trial-audio-only", "trial-invalid-action", "trial-missing-callback", "trial-missing-clock",
    "burst-policy-yield-reset", "burst-policy-incomplete-resume", "burst-policy-ready-resume", "burst-policy-ready-recover", "burst-policy-ready-hard", "burst-policy-ready-stop", "burst-policy-ready-fatal",
    "burst-legacy-index", "burst-legacy-pending", "burst-legacy-missing", "burst-legacy-corrupt", "burst-legacy-noise", "burst-legacy-candidate-dir", "burst-legacy-scan-stop", "burst-legacy-scan-open-error", "burst-legacy-scan-read-error", "burst-legacy-scan-close-error", "burst-legacy-scan-sync-error", "burst-legacy-scan-index-error", "burst-legacy-scan-duplicate",
    "burst-sd-media-error", "burst-sd-window",
    "burst-sd-batched", "burst-sd-placeholder", "burst-sd-candidate", "burst-sd-staged-mutation", "burst-sd-short-read", "burst-sd-read-error", "burst-sd-after-read-stop", "burst-sd-boundary-stop", "burst-sd-pending",
    "burst-sd-progress", "burst-sd-journal-stop", "burst-sd-check-start-stop", "burst-sd-check-mid-stop",
    "burst-good", "burst-mixed", "burst-prefix", "burst-gaps", "burst-audio", "burst-boundaries", "burst-reverse", "burst-interior", "burst-all-bad", "burst-held-recovery", "burst-fatal", "burst-cancel-read", "burst-stop-before-read", "burst-stop-mid", "burst-stop-candidate", "burst-torn-repaired", "burst-staged-mutation",
    "partial-prefix", "partial-mixed", "partial-fill", "partial-audio", "partial-repair", "partial-repair-invalid", "partial-repair-streak", "partial-cd-mode1", "partial-cd-mode2",
    "health-recover", "health-recover-fail", "health-verify-fail", "health-no-recover", "health-fatal", "health-cancel", "health-different", "health-second-different", "health-one-anchor", "health-partial-anchors", "health-stop-before-probe", "health-region", "health-invalid", "health-partial-reset", "health-manual-skip",
    "defer-range", "defer-tail", "defer-default", "defer-new", "defer-recover", "defer-ready", "defer-complete", "defer-no-ready", "defer-wrong-disc", "defer-anchor-retry", "defer-anchor-fatal", "defer-stop-before", "defer-stop-after", "defer-queue", "defer-torn", "defer-index", "defer-boundary", "defer-quarantine",
    "anchor-discovery-budget", "anchor-discovery-fatal", "anchor-discovery-stop",
    "cd-mixed", "cd-mode2", "cd-audio", "cd-offset", "cd-mode-switch", "cd-audio-anchor-mismatch", "cd-audio-silence", "cd-profile-mismatch", "anchor-resume-retry", "anchor-latest-retry", "anchor-resume-fatal", "anchor-resume-exhausted", "drive-pass", "drive-buffer", "drive-stop", "drive-fail", "drive-media", "drive-fatal",
    "fast-dense", "fast-invalid", "legacy-narrow-v1-resume", "legacy-narrow-v2-resume", "healthy", "status-deferred-recovery", "status-collection-rate", "zero-off", "fatal", "fatal-committed-resume", "cancel", "repair", "bad-parity",
    "auto-repair", "auto-late", "auto-exhausted", "auto-resume", "auto-legacy-resume",
    "auto-until-late", "auto-until-stop", "auto-read-stop",
    "audio-mismatch", "wrong-fad", "bad-ecc", "repair-fatal", "repair-stop",
    "queue-write", "queue-sync", "commit-write", "torn-commit",
    "baseline-write", "baseline-sync", "ready-write", "backup-write",
    "backup-sync", "backup-publish", "patch-write", "readback-fail",
    "readback-mismatch", "repaired-write", "torn-repaired", "complete-write",
    "complete-sync", "final-publish", "corrupt-header", "corrupt-journal",
    "wrong-disc", "placeholder-changed", "header-readback", "journal-readback",
    "backup-substitute", "queue-error-cancel", "five-passes", "invalid-limit",
    "latest-wrong-disc", "corrupt-patch", "adaptive", "anchor-interior",
    "legacy-repair", "large-map", "torn-candidate", "candidate-sync", "candidate-readback",
    "index-corrupt", "index-delete", "source-buffer-mutation", "event-buffer-mutation",
    "target-buffer-mutation", "candidate-journal-forgery", "quarantine-resume", "finish-stop", "identity-stop",
)


def check_verifier(exported, targets=36):
    spec = importlib.util.spec_from_file_location("salvage_verifier", ROOT / "tools/verify_salvage.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    result = module.verify(exported)
    assert result["saved_files_verified"] and result["original_targets"] == targets
    packed = (exported / "patches.bin").exists()
    patch_name, patch_bytes = ("patches.bin", 5120) if packed else ("patch-0000.bin", 8192)
    mutations = [("track03.bin" if (exported/"track03.bin").exists() else "track03.raw", 17, False), ("track01.bin" if (exported/"track01.bin").exists() else "track01.raw", 2 * 2352 + 17, False), ("journal.bin", -1, False)]
    if targets:
        mutations.append((patch_name, 64 + 2352 + 17, True))
    for filename, offset, checksum in mutations:
        path = exported / filename
        original = path.read_bytes()
        damaged = bytearray(original)
        damaged[offset] ^= 1
        if checksum:
            damaged[28:32] = zlib.crc32(damaged[64 + 2352:64 + 2 * 2352]).to_bytes(4, "little")
            damaged[patch_bytes - 4:patch_bytes] = zlib.crc32(damaged[:patch_bytes - 4]).to_bytes(4, "little")
        path.write_bytes(damaged)
        try:
            module.verify(exported)
        except ValueError:
            pass
        else:
            raise AssertionError(f"Verifier accepted changed {filename}")
        path.write_bytes(original)
    path = exported / "journal.bin"
    original = path.read_bytes()
    path.write_bytes(original[:-512])
    try:
        module.verify(exported)
    except ValueError:
        pass
    else:
        raise AssertionError("Verifier accepted a missing COMPLETE event")
    path.write_bytes(original)

    if packed and targets:
        # Recomputed outer checksums must not authorize a different candidate.
        # The immutable patch payload, candidate publication and repaired event
        # must all agree, not merely carry individually valid CRC fields.
        path = exported / "journal.bin"
        original = path.read_bytes()
        for field in (24, 32):
            forged = bytearray(original)
            at = next(at for at in range(0, len(forged), 512)
                      if int.from_bytes(forged[at + 16:at + 20], "little") == 7)
            value = int.from_bytes(forged[at + field:at + field + 4], "little")
            forged[at + field:at + field + 4] = (value ^ 1).to_bytes(4, "little")
            forged[at + 508:at + 512] = zlib.crc32(forged[at:at + 508]).to_bytes(4, "little")
            path.write_bytes(forged)
            try:
                module.verify(exported)
            except ValueError:
                pass
            else:
                raise AssertionError("Verifier accepted forged candidate publication")
            path.write_bytes(original)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", action="append", choices=CASES, dest="cases")
    parser.add_argument("--filesystem", action="append", choices=("fat32", "exfat"), dest="filesystems")
    parser.add_argument("--binary", type=Path, default=ROOT / "build/salvage-image")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="kui-salvage-") as temporary:
        base = Path(temporary)
        fixture = base / "fixture"
        fixture.mkdir()
        (fixture / "track01.bin").write_bytes(b"".join(sector(fad, fad) for fad in range(150, 190)))
        (fixture / "track02.raw").write_bytes(bytes((i * 31 + 5) & 255 for i in range(4 * 2352)))
        (fixture / "track03.bin").write_bytes(b"".join(sector(fad, fad) for fad in range(45150, 45190)))
        trial_fixture=base/"trial";trial_fixture.mkdir()
        for name in ("track01.bin","track02.raw","track03.bin"):shutil.copyfile(fixture/name,trial_fixture/name)
        (trial_fixture/"alternate-anchor.raw").write_bytes(sector(45150,999))
        (trial_fixture/"alternate-target.raw").write_bytes(sector(45158,999))
        large_fixture = base / "large-fixture"
        large_fixture.mkdir()
        for name in ("track01.bin", "track03.bin"):
            shutil.copyfile(fixture / name, large_fixture / name)
        large_bytes = 8208 * 2352
        pattern = bytes((i * 31 + 5) & 255 for i in range(256))
        (large_fixture / "track02.raw").write_bytes((pattern * ((large_bytes + 255) // 256))[:large_bytes])
        fast_fixture=base/"fast-dense";fast_fixture.mkdir()
        for name in ("track01.bin","track02.raw"):shutil.copyfile(fixture/name,fast_fixture/name)
        (fast_fixture/"track03.bin").write_bytes(b"".join(sector(fad,fad) for fad in range(45150,45150+320)))
        defer_fixture=base/"defer";defer_fixture.mkdir()
        for name in ("track01.bin","track02.raw"):shutil.copyfile(fixture/name,defer_fixture/name)
        (defer_fixture/"track03.bin").write_bytes(b"".join(sector(fad,fad) for fad in range(45150,45150+104)))
        (defer_fixture/"alternate.raw").write_bytes(sector(45150,999))
        health_fixture=base/"health";health_fixture.mkdir()
        for name in ("track01.bin","track02.raw"):shutil.copyfile(fixture/name,health_fixture/name)
        (health_fixture/"track03.bin").write_bytes(b"".join(sector(fad,fad) for fad in range(45150,45150+80*32)))
        (health_fixture/"alternate.raw").write_bytes(sector(45150,999))
        (health_fixture/"alternate-end.raw").write_bytes(sector(45150+80*32-1,999))
        burst_fixture=base/"burst";burst_fixture.mkdir()
        for name in ("track01.bin","track02.raw"):shutil.copyfile(fixture/name,burst_fixture/name)
        (burst_fixture/"track03.bin").write_bytes(b"".join(sector(fad,fad) for fad in range(45150,45150+512)))
        (burst_fixture/"alternate-staged.raw").write_bytes(sector(45157,999))
        legacy_fixture=base/"legacy-index";legacy_fixture.mkdir()
        for name in ("track01.bin","track02.raw"):shutil.copyfile(fixture/name,legacy_fixture/name)
        (legacy_fixture/"track03.bin").write_bytes(b"".join(sector(fad,fad) for fad in range(45150,45150+1024)))
        cd_fixtures={}
        for cd in ("cd-mixed","cd-mode2","cd-audio","cd-offset","cd-mode-switch","cd-audio-anchor-mismatch","cd-audio-silence","cd-profile-mismatch"):
            folder=base/cd;folder.mkdir();cd_fixtures[cd]=folder
            for i,(control,start,end) in enumerate(((0 if cd.startswith("cd-audio") else 4,150,190),(0,190,194),(0 if cd.startswith("cd-audio") else 4,194,234)),1):
                if cd=="cd-offset":start+=75;end+=75
                data=b"".join((mode2_sector if cd=="cd-mode2" or (cd=="cd-mode-switch" and fad==end-1) else sector)(fad,fad) for fad in range(start,end)) if control else bytes((n*31+5)&255 for n in range((end-start)*2352))
                if cd=="cd-audio-silence":data=bytes(len(data))
                (folder/f"track{i:02d}.{'bin' if control else 'raw'}").write_bytes(data)
        cd_fixtures["partial-cd-mode1"]=cd_fixtures["cd-mixed"]
        cd_fixtures["partial-cd-mode2"]=cd_fixtures["cd-mode2"]
        for kind in args.filesystems or ("fat32", "exfat"):
            seed = base / f"{kind}-seed.img"
            with seed.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(seed)) if kind == "fat32" else run("mkfs.exfat", str(seed))
            for case in args.cases or CASES:
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(seed, image)
                exported = base / f"{kind}-{case}-saved"
                extra = []
                if case in ("trial-balanced","trial-single","trial-partial","trial-all-bad","trial-invalid-partial","trial-unsupported","trial-two-profiles","trial-profile-lost","trial-qualify-skip","trial-budget","trial-fatal","trial-conflict","trial-stop-read") or case.startswith("burst-") or (case.startswith("partial-") or (case.startswith("health-") and case!="health-manual-skip")) or (case.startswith("cd-") and case not in ("cd-mode-switch","cd-audio-anchor-mismatch","cd-audio-silence")) or case=="drive-pass" or case=="anchor-resume-retry" or case in ("repair", "legacy-repair", "anchor-interior", "large-map", "finish-stop", "fatal-committed-resume", "fast-dense", "fast-invalid", "legacy-narrow-v1-resume", "legacy-narrow-v2-resume") or case.startswith("auto-") or case in ("defer-range","defer-tail","defer-stop-before","defer-stop-after","defer-queue","defer-torn","defer-index","defer-boundary"):
                    exported.mkdir()
                    extra = [str(exported)]
                selected_fixture = cd_fixtures.get(case,trial_fixture if case.startswith("trial-") else legacy_fixture if case.startswith("burst-legacy-") else burst_fixture if case.startswith("burst-") else health_fixture if case.startswith("health-") else defer_fixture if case.startswith("defer-") and case!="defer-tail" else fast_fixture if case=="fast-dense" else large_fixture if case == "large-map" else fixture)
                output = run(str(args.binary), str(image), str(selected_fixture), case, *extra)
                if case in ("partial-prefix","partial-cd-mode1","partial-cd-mode2"):
                    assert "transport=partial accepted=11 unverified=21" in output
                if case=="partial-mixed":
                    assert "transport=partial accepted=28 unverified=4" in output
                    assert "first invalid FAD=152" in output
                if case in ("health-region","health-invalid"):
                    assert output.count("health check after ")==2
                    assert "health check after 8 wholly unverified ranges" in output
                    assert "health check after 72 wholly unverified ranges" in output
                    assert output.count("saved anchors verified; continuing scan without reinitialization")==2
                if case in ("health-partial-reset","health-manual-skip"):
                    assert "health check after " not in output
                for line in output.splitlines():
                    if line.startswith(("PASS bounded worker allocations:","PASS repair optical reduction:","PASS saved check batching:","PASS legacy candidate index:")):
                        print(f"{kind}: {line}", flush=True)
                if extra:
                    targets = {"cd-mixed":32,"cd-mode2":32,"cd-audio":32,"cd-profile-mismatch":32,"cd-offset":0,"auto-legacy-resume": 4, "anchor-interior": 40, "large-map": 8208, "finish-stop": 0,"fast-dense":288,"fast-invalid":2,"legacy-narrow-v1-resume":2,"legacy-narrow-v2-resume":2,"defer-range":34,"defer-tail":10,"defer-stop-before":34,"defer-stop-after":34,"defer-queue":34,"defer-torn":34,"defer-index":34,"defer-boundary":5}.get(case, 36)
                    if case.startswith("trial-"):targets=1 if case=="trial-single" else 16
                    if case.startswith("partial-"):
                        targets={"partial-mixed":4,"partial-fill":32,"partial-audio":4,"partial-repair":32,"partial-repair-invalid":32,"partial-repair-streak":32}.get(case,21)
                    if case.startswith("health-"):
                        targets=2560 if case in ("health-region","health-invalid","health-one-anchor","health-partial-anchors") else 479 if case=="health-partial-reset" else 256
                    if case.startswith("burst-"):
                        targets={"burst-gaps":34,"burst-audio":4,"burst-boundaries":20,"burst-sd-window":508}.get(case,512)
                    if case.startswith("burst-legacy-"):targets=1024
                    check_verifier(exported, targets=targets)
                assert f"PASS salvage {case}" in output
                run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                image.unlink()
                print(f"PASS {kind} salvage: {case}", flush=True)
    print(f"PASS salvage suite: {len(args.cases or CASES) * len(args.filesystems or ('fat32', 'exfat'))} filesystem cases", flush=True)


if __name__ == "__main__":
    main()
