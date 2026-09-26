#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Promote a successful exact-commit native build to the explicitly requested release."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

from package import ROOT, release_metadata


def gh(*args):
    return subprocess.check_output(["gh", *args], text=True, cwd=ROOT)


def api(path):
    return json.loads(gh("api", path))


def require(condition, message):
    if not condition:
        raise SystemExit(message)


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
                for entry in ("KUI/runtime.kui", "KUI/apps/games/retail-boot.kui",
                              "boot-cd/kui-v1.5.1.cdi", "START-HERE.md", "RELEASE-NOTES.md"):
                    require(entry in archive.namelist(), "Incomplete release: " + entry)
            else:
                for entry in ("source/kui-source.tar.gz", "source/kos-source.tar.gz", "LICENSE"):
                    require(entry in archive.namelist(), "Incomplete corresponding source: " + entry)
    return sorted(directory / name for name in names) + [directory / "SHA256SUMS.txt"]


def main():
    repository = os.environ["GITHUB_REPOSITORY"]
    commit = os.environ["GITHUB_SHA"]
    require(repository == "TPMJB/K-UI-NeXT" and os.environ["GITHUB_REF"] == "refs/heads/main",
            "Release publication requires the upstream main branch")
    release = release_metadata()
    require(release["version"] == "1.5.1", "This promotion is explicitly scoped to 1.5.1")
    tag = "v" + release["version"]
    message = subprocess.check_output(["git", "show", "-s", "--format=%B", "HEAD"],
                                      cwd=ROOT, text=True)
    require("Release: " + tag in message.splitlines(), "Missing explicit release request")
    endpoint = "repos/" + repository
    require(api(endpoint + "/git/ref/heads/main")["object"]["sha"] == commit,
            "Main advanced after this promotion; refusing to publish stale artifacts")
    runs = api(endpoint + "/actions/workflows/diagnostic.yml/runs?event=pull_request&status=success&head_sha=" + commit)["workflow_runs"]
    runs = [run for run in runs if run["head_sha"] == commit and
            run["head_repository"]["full_name"] == repository and run["conclusion"] == "success"]
    require(runs, "No successful native PR build exists for this exact commit")
    run = max(runs, key=lambda item: item["id"])
    run_id = str(run["id"])
    jobs = api(endpoint + "/actions/runs/" + run_id + "/jobs")["jobs"]
    require(any(job["name"] == "dreamcast" and job["conclusion"] == "success" for job in jobs),
            "Native compilation and packaging did not succeed")
    artifact_name = release["artifact_prefix"] + "-release-assets"
    artifacts = api(endpoint + "/actions/runs/" + run_id + "/artifacts")["artifacts"]
    artifacts = [item for item in artifacts if item["name"] == artifact_name and not item["expired"]]
    require(len(artifacts) == 1, "Missing or ambiguous release assets")
    with tempfile.TemporaryDirectory(prefix="kui-release-") as temporary:
        directory = Path(temporary)
        gh("run", "download", run_id, "--repo", repository, "--name", artifact_name, "--dir", temporary)
        assets = verify_assets(directory, release, commit)
        notes = (ROOT / "docs/release-v1.5.1-notes.md").read_text(encoding="utf-8")
        notes = notes.replace("(release-v1.5.1.md)",
                              f"(https://github.com/{repository}/blob/{tag}/docs/release-v1.5.1.md)")
        notes += f"\nSource commit: `{commit}`. [Native build]({run['html_url']}).\n"
        notes_file = directory / "release-notes.md"
        notes_file.write_text(notes, encoding="utf-8")
        # gh creates the release as a draft while uploading assets, then publishes it.
        # An existing tag/release is never moved or overwritten by this script.
        tags = api(endpoint + "/git/matching-refs/tags/" + tag)
        require(not any(item["ref"] == "refs/tags/" + tag for item in tags),
                "Release tag already exists; refusing to overwrite it")
        print(gh("release", "create", tag, *(str(path) for path in assets), "--repo", repository,
                 "--target", commit, "--title", release["name"], "--notes-file", str(notes_file), "--latest"))


if __name__ == "__main__":
    main()
