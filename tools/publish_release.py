#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Promote a successful exact-commit native build to the explicitly requested release."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile

from package import ROOT, release_metadata
from runtime_package import verify
from retail_package import inspect_retail

SUPPORTED_RELEASES = {"1.7", "1.8.5"}


def release_tag(release):
    require(release["version"] in SUPPORTED_RELEASES,
            "This promotion is explicitly scoped to 1.7 and 1.8.5")
    return "v" + release["version"]


def post_allowed_paths(version):
    require(version in SUPPORTED_RELEASES, "Unsupported release-post version")
    return {"README.md", f"docs/release-v{version}.md",
            f"docs/release-v{version}-notes.md", f"docs/release-v{version}-announcements.md",
            "tools/publish_release.py", ".github/workflows/release.yml",
            ".github/workflows/diagnostic.yml"}


def successful_build(repository, commit, release):
    """Reuse only a fully successful trusted build of these exact source bytes."""
    endpoint = "repos/" + repository
    runs = api(endpoint + "/actions/workflows/diagnostic.yml/runs?status=success&head_sha=" + commit + "&per_page=100")["workflow_runs"]
    runs = [run for run in runs if run["head_sha"] == commit and
            (run.get("head_repository") or {}).get("full_name") == repository and
            run["conclusion"] == "success" and
            (run.get("event") == "pull_request" or
             (run.get("event") == "push" and
              (run.get("head_branch") == "main" or run.get("head_branch", "").startswith("milestone/"))))]
    artifact_name = release["artifact_prefix"] + "-release-assets"
    for run in sorted(runs, key=lambda item: item["id"], reverse=True):
        run_id = str(run["id"])
        jobs = api(endpoint + "/actions/runs/" + run_id + "/jobs?per_page=100")["jobs"]
        if not all(any(job["name"] == name and job["conclusion"] == "success"
                       for job in jobs) for name in ("host", "dreamcast")):
            continue
        artifacts = api(endpoint + "/actions/runs/" + run_id + "/artifacts?per_page=100")["artifacts"]
        artifacts = [item for item in artifacts if item["name"] == artifact_name and not item["expired"]]
        if len(artifacts) == 1:
            return run, artifact_name
    require(False, "No fully successful upstream native build and release assets exist for this exact commit")


def gh(*args):
    return subprocess.check_output(["gh", *args], text=True, cwd=ROOT)


def api(path):
    return json.loads(gh("api", path))


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def public_notes(notes, repository, tag, asset_ref=None):
    """Release pages have no docs/ base URL; retain working source links."""
    def replace(match):
        target = match.group(1)
        if re.match(r"^[a-z][a-z0-9+.-]*:|^#", target, re.IGNORECASE):
            return match.group(0)
        path, separator, anchor = target.partition("#")
        source = (ROOT / "docs" / path).resolve()
        if source.is_relative_to(ROOT) and source.is_file():
            if source.suffix.lower() in (".png", ".jpg", ".jpeg"):
                url = f"https://raw.githubusercontent.com/{repository}/{asset_ref or tag}/{source.relative_to(ROOT)}"
            else:
                url = f"https://github.com/{repository}/blob/{asset_ref or tag}/{source.relative_to(ROOT)}"
            return "(" + url + (separator + anchor if separator else "") + ")"
        return match.group(0)
    return re.sub(r"(?<=\])\(([^)\s]+)\)", replace, notes)


def verify_assets(directory, release, commit):
    prefix = release["artifact_prefix"]
    names = {prefix + "-release.zip", prefix + "-source.zip"}
    checksums = {}
    for line in (directory / "SHA256SUMS.txt").read_text().splitlines():
        digest, name = line.split("  ", 1)
        require(name in names and name not in checksums, "Unexpected release checksum entry")
        checksums[name] = digest
    require(set(checksums) == names, "Missing release asset checksum")
    for name, expected in checksums.items():
        with (directory / name).open("rb") as stream:
            require(hashlib.file_digest(stream, "sha256").hexdigest() == expected,
                    "Release asset checksum mismatch: " + name)
        with zipfile.ZipFile(directory / name) as archive:
            record = json.loads(archive.read("build.json"))
            require(record["commit"] == commit and record["release"] == release,
                    "Build source identity does not match the promoted commit")
            if name.endswith("-release.zip"):
                require(record["kind"] == "release", "Refusing a diagnostic or candidate package")
                payloads = ("KUI/runtime.kui", "KUI/apps/games/probe.kui",
                            "KUI/apps/games/image-probe.kui", "KUI/apps/games/retail-boot.kui",
                            "KUI/apps/games/ce-probe.kui")
                for entry in (*payloads, f"boot-cd/kui-v{release['version']}.cdi",
                              "START-HERE.md", "RELEASE-NOTES.md", "SHA256SUMS"):
                    require(entry in archive.namelist(), "Incomplete release: " + entry)
                for entry in payloads:
                    try:
                        info = verify(archive.read(entry))
                    except ValueError as error:
                        require(False, "Invalid release payload " + entry + ": " + str(error))
                    require(info["build"] == commit[:12], "Release payload source identity mismatch: " + entry)
                if release["version"] == "1.8.5":
                    try:
                        native = inspect_retail(archive.read("KUI/apps/games/retail-boot.kui"))
                        ce = inspect_retail(archive.read("KUI/apps/games/ce-probe.kui"), ce=True)
                    except ValueError as error:
                        require(False, "Invalid release reader: " + str(error))
                    require((native["resident_address"], native["resident_limit"]) ==
                            ("0x8c004000", "0x8c007800"), "1.8.5 requires the hardware-confirmed native placement")
                    require((ce["resident_address"], ce["resident_limit"]) ==
                            ("0x8c008300", "0x8c00bb00"), "Windows CE release placement changed")
                    for entry in ("tools/gdi_optimize.py", "tools/game_image_import.py",
                                  "GAMES-FORMATS.md", "GDI-2048-TEST.md", "RIPPER-CONTROLS.md",
                                  "CAPTURE-FORMAT.md", "LICENSES/capture-codecs.md",
                                  "LICENSES/miniz-NOTICES.txt", "LICENSES/minilzo-COPYING",
                                  "LICENSES/minilzo-README.LZO"):
                        require(entry in archive.namelist(), "Incomplete release: " + entry)
                entries = [entry for entry in archive.namelist() if not entry.endswith("/")]
                require(len(entries) == len(set(entries)), "Duplicate release archive entry")
                sums = {}
                for line in archive.read("SHA256SUMS").decode("utf-8").splitlines():
                    digest, entry = line.split("  ", 1)
                    require(entry in entries and entry not in sums and entry != "SHA256SUMS",
                            "Unexpected installed checksum entry")
                    require(hashlib.sha256(archive.read(entry)).hexdigest() == digest,
                            "Installed release checksum mismatch: " + entry)
                    sums[entry] = digest
                require(set(sums) == set(entries) - {"SHA256SUMS"}, "Incomplete installed checksums")
            else:
                for entry in ("source/kui-source.tar.gz", "source/kos-source.tar.gz", "LICENSE"):
                    require(entry in archive.namelist(), "Incomplete corresponding source: " + entry)
    return sorted(directory / name for name in names) + [directory / "SHA256SUMS.txt"]


def update_post(repository, commit, release, tag, message):
    """Update only an existing release's prose; retain its tested binaries and tag."""
    require("Release post: " + tag in message.splitlines(), "Missing explicit release-post request")
    endpoint = "repos/" + repository
    require(api(endpoint + "/git/ref/heads/main")["object"]["sha"] == commit,
            "Main advanced after the release-post request")
    published = api(endpoint + "/releases/tags/" + tag)
    require(not published["draft"] and not published["prerelease"], "Expected an existing full release")
    tag_before = api(endpoint + "/git/ref/tags/" + tag)["object"]
    require(tag_before["type"] == "commit", "Expected the original lightweight release tag")
    allowed = post_allowed_paths(release["version"])
    paths = subprocess.check_output(["git", "diff", "--name-only", tag_before["sha"], commit],
                                    cwd=ROOT, text=True).splitlines()
    require(paths and all(path in allowed or
            (path.startswith(f"resources/release-v{release['version']}/") and path.endswith(".png")) for path in paths),
            "Release-post update includes changes outside documentation and screenshots")
    old_body = published["body"]
    require("\nSource commit: " in old_body, "Missing original build attribution")
    footer = "\nSource commit: " + old_body.rsplit("\nSource commit: ", 1)[1]
    notes = (ROOT / f"docs/release-v{release['version']}-notes.md").read_text(encoding="utf-8")
    notes = public_notes(notes, repository, tag, asset_ref=commit) + footer
    assets_before = [(asset["id"], asset["name"], asset.get("digest")) for asset in published["assets"]]
    with tempfile.TemporaryDirectory(prefix="kui-release-post-") as temporary:
        notes_file = Path(temporary) / "release-notes.md"
        notes_file.write_text(notes, encoding="utf-8")
        print(gh("release", "edit", tag, "--repo", repository, "--notes-file", str(notes_file)))
    updated = api(endpoint + "/releases/tags/" + tag)
    require(updated["body"] == notes, "Published release post differs from requested text")
    require([(asset["id"], asset["name"], asset.get("digest")) for asset in updated["assets"]] == assets_before,
            "Release asset identity changed during the post update")
    require(api(endpoint + "/git/ref/tags/" + tag)["object"] == tag_before,
            "Release tag changed during the post update")
    print("Updated release post; original build attribution and assets verified")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update-post", action="store_true")
    args = parser.parse_args()
    repository = os.environ["GITHUB_REPOSITORY"]
    commit = os.environ["GITHUB_SHA"]
    require(repository == "TPMJB/K-UI-NeXT" and os.environ["GITHUB_REF"] == "refs/heads/main",
            "Release publication requires the upstream main branch")
    release = release_metadata()
    tag = release_tag(release)
    message = subprocess.check_output(["git", "show", "-s", "--format=%B", "HEAD"],
                                      cwd=ROOT, text=True)
    if args.update_post:
        update_post(repository, commit, release, tag, message)
        return
    require("Release: " + tag in message.splitlines(), "Missing explicit release request")
    endpoint = "repos/" + repository
    require(api(endpoint + "/git/ref/heads/main")["object"]["sha"] == commit,
            "Main advanced after this promotion; refusing to publish stale artifacts")
    run, artifact_name = successful_build(repository, commit, release)
    run_id = str(run["id"])
    with tempfile.TemporaryDirectory(prefix="kui-release-") as temporary:
        directory = Path(temporary)
        gh("run", "download", run_id, "--repo", repository, "--name", artifact_name, "--dir", temporary)
        assets = verify_assets(directory, release, commit)
        notes = (ROOT / f"docs/release-v{release['version']}-notes.md").read_text(encoding="utf-8")
        notes = public_notes(notes, repository, tag)
        notes += f"\nSource commit: `{commit}`. [Native build]({run['html_url']}).\n"
        notes_file = directory / "release-notes.md"
        notes_file.write_text(notes, encoding="utf-8")
        # gh creates the release as a draft while uploading assets, then publishes it.
        # An existing tag/release is never moved or overwritten by this script.
        require(api(endpoint + "/git/ref/heads/main")["object"]["sha"] == commit,
                "Main advanced while release assets were being verified")
        tags = api(endpoint + "/git/matching-refs/tags/" + tag)
        require(not any(item["ref"] == "refs/tags/" + tag for item in tags),
                "Release tag already exists; refusing to overwrite it")
        print(gh("release", "create", tag, *(str(path) for path in assets), "--repo", repository,
                 "--target", commit, "--title", release["name"], "--notes-file", str(notes_file), "--latest"))


if __name__ == "__main__":
    main()
