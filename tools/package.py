#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the diagnostic and source/license records without publishing a release."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from runtime_package import envelope, flatten_elf, rejection_cases, verify

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run(args, cwd=ROOT, check=True)


def guide(source):
    text = (ROOT / "docs" / source).read_text()
    for name in ("sd-bootstrap", "hardware-test", "hardware-evidence", "capture-test", "capture-format", "memory-stats", "optical-test", "performance-test-plan", "m15-shell-test", "prior-work-reuse", "ripper-controls", "salvage-plan", "apps-test", "app-architecture", "resume-and-retries", "independent-app-parity", "apps-round-two"):
        text = text.replace(f"({name}.md)", f"({name.upper()}.md)")
    return text


def main():
    dirty = subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)
    if dirty:
        raise SystemExit("Commit source changes before packaging; source and binary must agree")
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    elf = ROOT / "build/kui-diagnostic.elf"
    if not elf.is_file():
        raise SystemExit("Build the Dreamcast diagnostic first")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    runtime = ROOT / "build/kui-runtime.elf"
    for image in (elf, runtime):
        compiled = json.loads(image.with_suffix(".compile.json").read_text())
        if (compiled["source_dirty"] or compiled["commit"] != commit or
                compiled["elf_sha256"] != hashlib.sha256(image.read_bytes()).hexdigest()):
            raise SystemExit("Rebuild both programs from the current clean commit before packaging")
    mkdcdisc = ROOT / ".deps/mkdcdisc"
    if not (mkdcdisc / "builddir/build.ninja").exists():
        run("meson", "setup", str(mkdcdisc / "builddir"), str(mkdcdisc), "-Dpng=disabled")
    run("meson", "compile", "-C", str(mkdcdisc / "builddir"))
    # Padding the flattened binary to a full CD data sector avoids bootloader
    # partial-sector tails. The packager then scrambles it for the MIL-CD image.
    run("sh-elf-objcopy", "-O", "binary", str(elf), "build/kui-diagnostic.bin")
    binary = ROOT / "build/kui-diagnostic.bin"
    data = binary.read_bytes()
    binary.write_bytes(data + b"\0" * (-len(data) % 2048))
    run(str(mkdcdisc / "builddir/mkdcdisc"), "-b", str(binary),
        "-n", "K-UI NeXT Bootstrap", "-a", "K-UI Team", "-r", "20260916",
        "-m", "-N", "--allow-overwrite", "-o", "dist/kui-diagnostic.cdi")
    for name in ("kui-diagnostic.elf", "kui-diagnostic.bin", "kui-diagnostic.map"):
        shutil.copyfile(ROOT / "build" / name, dist / name)
    for name in ("kui-runtime.elf", "kui-runtime.map"):
        shutil.copyfile(ROOT / "build" / name, dist / name)
    payload, memory = flatten_elf(runtime.read_bytes())
    package = envelope(payload, memory, commit[:12])
    sd = dist / "sd/KUI"
    sd.mkdir(parents=True, exist_ok=True)
    (sd / "runtime.kui").write_bytes(package)
    run("python3", "tools/generate_menu_music.py", "--directory", str(sd / "apps/music"))
    shutil.copyfile(ROOT / "resources/music/README.md", dist / "MUSIC.md")
    shutil.copyfile(ROOT / "resources/music/manifest.json", dist / "music-manifest.json")
    # Reference catalogues for the end-of-capture check (optional on the card).
    for name in ("redump.db", "tosec.db"):
        shutil.copyfile(ROOT / "data/known-dumps" / name, sd / name)
    cases = dist / "loader-tests"
    cases.mkdir(exist_ok=True)
    for name, data in rejection_cases(package).items():
        (cases / name).write_bytes(data)
    shutil.copyfile(ROOT / "LICENSE", dist / "LICENSE")
    shutil.copyfile(ROOT / "THIRD_PARTY.md", dist / "THIRD_PARTY.md")
    (dist / "HARDWARE-TEST.md").write_text(guide("hardware-test.md"))
    (dist / "SD-BOOTSTRAP.md").write_text(guide("sd-bootstrap.md"))
    (dist / "HARDWARE-EVIDENCE.md").write_text(guide("hardware-evidence.md"))
    (dist / "M15-SHELL-TEST.md").write_text(guide("m15-shell-test.md"))
    (dist / "APPS-TEST.md").write_text(guide("apps-test.md"))
    for name in ("apps-round-two", "resume-and-retries", "independent-app-parity"):
        (dist / (name.upper()+".md")).write_text(guide(name+".md"))
    run("make", "build/render-shell")
    run("python3", "tools/render_app_previews.py", "--output", str(dist / "ui-previews"))
    (dist / "APP-ARCHITECTURE.md").write_text(guide("app-architecture.md"))
    (dist / "PRIOR-WORK-REUSE.md").write_text(guide("prior-work-reuse.md"))
    (dist / "RIPPER-CONTROLS.md").write_text(guide("ripper-controls.md"))
    (dist / "SALVAGE-PLAN.md").write_text(guide("salvage-plan.md"))
    shutil.copyfile(ROOT / "tools/verify_probe.py", dist / "verify_probe.py")
    shutil.copyfile(ROOT / "tools/verify_dump.py", dist / "verify_dump.py")
    for src, name in (("capture-test.md", "CAPTURE-TEST.md"), ("capture-format.md", "CAPTURE-FORMAT.md"), ("memory-stats.md", "MEMORY-STATS.md"), ("optical-test.md", "OPTICAL-TEST.md"), ("performance-test-plan.md", "PERFORMANCE-TEST-PLAN.md")):
        (dist / name).write_text(guide(src))
    shutil.copyfile(ROOT / "tools/runtime_package.py", dist / "runtime_package.py")
    shutil.copytree(ROOT / "LICENSES", dist / "LICENSES", dirs_exist_ok=True)
    shutil.copyfile(ROOT / "data/known-dumps/README.txt", dist / "LICENSES/known-dumps-README.txt")
    kos = ROOT / ".deps/kos"
    shutil.copytree(kos / "doc/license", dist / "LICENSES/KOS", dirs_exist_ok=True)
    for name in ("AUTHORS", "doc/LICENSE.md"):
        src = kos / name
        if src.exists():
            shutil.copyfile(src, dist / "LICENSES" / ("KOS-" + src.name))
    shutil.copyfile(mkdcdisc / "THIRD-PARTY-NOTICES.md", dist / "LICENSES/mkdcdisc-NOTICES.md")
    shutil.copyfile(mkdcdisc / "LICENSE", dist / "LICENSES/mkdcdisc-LICENSE")
    # Include the actual third-party source inputs for these test artifacts,
    # including build scripts, local adaptations, and toolchain license texts.
    lock = json.loads((ROOT / "dependencies.json").read_text())
    source = dist / "source"
    source.mkdir(exist_ok=True)
    run("git", "archive", "--format=tar.gz", "--prefix=K-UI-NeXT/", "-o", str(source / "kui-source.tar.gz"), "HEAD")
    subprocess.run(["git", "archive", "--format=tar.gz", "--prefix=KallistiOS/", "-o",
                    str(source / "kos-source.tar.gz"), lock["kos"]["commit"]], cwd=kos, check=True)
    shutil.copytree(ROOT / ".deps/fatfs", source / "fatfs", dirs_exist_ok=True)
    shutil.copytree(ROOT / ".deps/downloads", source / "fatfs-originals", dirs_exist_ok=True)
    chain = kos / "utils/kos-chain"
    # GNU runtime sources may remain as either expanded inputs or tarballs.
    for pattern in ("gcc-*.tar.xz", "newlib-*.tar.gz"):
        for path in chain.glob(pattern):
            shutil.copyfile(path, source / path.name)
    compiler = subprocess.check_output(["sh-elf-gcc", "--version"], text=True).splitlines()[0]
    record = {"commit": commit, "compiler": compiler, "dependencies": lock,
              "runtime": verify(package),
              "hardware_tested": False,
              "host_os": Path("/etc/os-release").read_text() if Path("/etc/os-release").exists() else os.name}
    (dist / "build.json").write_text(json.dumps(record, indent=2) + "\n")
    # Existing boot discs need only the small runtime update. Full source and
    # dependency archives remain available in this run's diagnostic artifact.
    update = dist / "sd-update"
    (update / "KUI").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(sd / "runtime.kui", update / "KUI/runtime.kui")
    shutil.copytree(sd / "apps", update / "KUI/apps", dirs_exist_ok=True)
    for name in ("redump.db", "tosec.db"):
        shutil.copyfile(sd / name, update / "KUI" / name)
    for name in ("APPS-ROUND-TWO.md", "RESUME-AND-RETRIES.md", "INDEPENDENT-APP-PARITY.md", "APPS-TEST.md", "APP-ARCHITECTURE.md", "MUSIC.md", "music-manifest.json", "M15-SHELL-TEST.md", "PRIOR-WORK-REUSE.md", "RIPPER-CONTROLS.md", "SALVAGE-PLAN.md", "CAPTURE-TEST.md", "CAPTURE-FORMAT.md", "MEMORY-STATS.md", "OPTICAL-TEST.md", "PERFORMANCE-TEST-PLAN.md", "verify_dump.py", "build.json", "LICENSE", "THIRD_PARTY.md"):
        shutil.copyfile(dist / name, update / name)
    shutil.copytree(dist / "LICENSES", update / "LICENSES", dirs_exist_ok=True)
    (update / "SOURCE.txt").write_text(
        f"K-UI NeXT source commit: {commit}\n"
        f"https://github.com/TPMJB/K-UI-NeXT/tree/{commit}\n\n"
        "The diagnostic artifact from this same workflow run contains exact K-UI, KOS,\n"
        "FatFs and compiler runtime source records under source/. Dependency pins and\n"
        "original notices are also included in build.json and LICENSES/.\n\n"
        "Install KUI/runtime.kui on the SD card. Keep your existing boot CD.\n"
        "Copy KUI/apps/music too for optional menu music; enable it in System Settings.\n"
        "Start with APPS-ROUND-TWO.md for the new controls. See APPS-TEST.md for this app round and RIPPER-CONTROLS.md for destinations, named dumps and CRC results.\n"
        "Also copy KUI/redump.db and KUI/tosec.db if you want each finished capture\n"
        "compared with the known-good Redump/TOSEC track CRCs; without them the capture\n"
        "works as before and reports that nothing was compared. Attribution and licence\n"
        "for both catalogues: LICENSES/known-dumps-README.txt.\n")
    update_hashes=[]
    for path in sorted(update.rglob("*")):
        if path.is_file() and path.name != "SHA256SUMS":
            update_hashes.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(update)}")
    (update / "SHA256SUMS").write_text("\n".join(update_hashes) + "\n")
    hashes = []
    for path in sorted(dist.rglob("*")):
        if path.is_file() and path != dist / "SHA256SUMS":
            with path.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            hashes.append(f"{digest}  {path.relative_to(dist)}")
    (dist / "SHA256SUMS").write_text("\n".join(hashes) + "\n")


if __name__ == "__main__":
    main()
