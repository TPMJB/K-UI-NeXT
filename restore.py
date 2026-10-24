#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify the checkpoint and restore its exact original source history."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


here = Path(__file__).resolve().parent
manifest = json.loads((here / "checkpoint.json").read_text())
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source-directory", type=Path, default=here / "restored-source")
args = parser.parse_args()
destination = args.source_directory.resolve()
if destination.exists():
    raise SystemExit("Choose a source directory that does not exist")


def checkpoint_path(name):
    path = (here / name).resolve()
    if not path.is_relative_to(here):
        raise SystemExit("Checkpoint path leaves its directory: " + name)
    return path


def verified_bytes(item):
    data = checkpoint_path(item["path"]).read_bytes()
    if len(data) != item["bytes"] or hashlib.sha256(data).hexdigest() != item["sha256"]:
        raise SystemExit("Preserved file differs: " + item["path"])
    return data


for item in manifest["files"]:
    verified_bytes(item)
for payload in manifest["payloads"]:
    data = b"".join(verified_bytes(part) for part in payload["parts"])
    if len(data) != payload["bytes"] or hashlib.sha256(data).hexdigest() != payload["sha256"]:
        raise SystemExit("Reassembled payload differs: " + payload["output"])
    target = checkpoint_path(payload["output"])
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)


def run(*args):
    subprocess.run(["git", *args], cwd=destination, check=True)


def output(*args):
    return subprocess.check_output(["git", *args], cwd=destination, text=True).strip()


subprocess.run(["git", "init", "-q", str(destination)], check=True)
run("remote", "add", "origin", manifest["repository"] + ".git")
# The checkpoint commit retains this exact main parent. Shallow-fetching the
# parent supplies shared objects without depending on main's future position.
run("fetch", "--depth=1", "--no-tags", "origin", manifest["prerequisite_main"])
base_bundle = checkpoint_path(manifest["payloads"][0]["output"])
run("fetch", "--no-tags", str(base_bundle),
    manifest["base_bundle_ref"] + ":refs/heads/source-base-container")
if (output("rev-parse", manifest["base_source_commit"] + "^{tree}") != manifest["base_source_tree"]
        or int(output("rev-list", "--count", manifest["base_source_commit"])) != manifest["base_original_commits"]):
    raise SystemExit("Restored base source identity/history differs")
run("fetch", "--no-tags", str(checkpoint_path(manifest["update_bundle_path"])),
    manifest["update_bundle_ref"] + ":refs/heads/recovered-native-vmu")
run("checkout", "--detach", manifest["source_commit"])
if (output("rev-parse", "HEAD^{tree}") != manifest["source_tree"]
        or int(output("rev-list", "--count", "HEAD")) != manifest["original_commits"]):
    raise SystemExit("Restored latest source identity/history differs")
run("fsck", "--full", "--no-reflogs")
print("Verified original source:", manifest["source_commit"])
print("Original source commits retained:", manifest["original_commits"])
print("Source directory:", destination)
print("Compiled comparison builds:", here / "builds")
