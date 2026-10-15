# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the publication boundary without contacting GitHub or publishing."""
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import publish_release

REPOSITORY = "TPMJB/K-UI-NeXT"
COMMIT = "1234567890ab" + "c" * 28
RELEASE = {"version": "1.8.5", "name": 'K-UI V1.8.5 "Dáinsleif"',
           "short_name": "K-UI V1.8.5", "artifact_prefix": "kui-1.8.5-dainsleif"}


def build(run_id, **changes):
    run = {"id": run_id, "head_sha": COMMIT, "head_repository": {"full_name": REPOSITORY},
           "conclusion": "success", "event": "push", "head_branch": "milestone/2048-variants-test"}
    return {**run, **changes}


class ExactBuildPublication(unittest.TestCase):
    def select(self, runs, jobs=None, artifacts=None):
        jobs = jobs or {}
        artifacts = artifacts or {}
        requests = []
        def fake_api(path):
            requests.append(path)
            if "/actions/workflows/diagnostic.yml/runs?" in path:
                self.assertIn("head_sha=" + COMMIT, path)
                return {"workflow_runs": runs}
            run_id = int(path.split("/actions/runs/", 1)[1].split("/", 1)[0])
            if "/jobs?" in path:
                return {"jobs": jobs.get(run_id, [{"name": name, "conclusion": "success"}
                                                  for name in ("host", "dreamcast")])}
            return {"artifacts": artifacts.get(run_id, [{"name": RELEASE["artifact_prefix"] + "-release-assets",
                                                         "expired": False}])}
        with patch.object(publish_release, "api", side_effect=fake_api):
            result = publish_release.successful_build(REPOSITORY, COMMIT, RELEASE)
        return result, requests

    def test_prebuilt_milestone_push_can_be_promoted_without_rebuilding(self):
        (run, artifact), _ = self.select([build(10)])
        self.assertEqual(run["id"], 10)
        self.assertEqual(artifact, "kui-1.8.5-dainsleif-release-assets")

    def test_exact_upstream_pr_remains_supported(self):
        (run, _), _ = self.select([build(11, event="pull_request", head_branch="feature")])
        self.assertEqual(run["id"], 11)

    def test_wrong_commit_fork_untrusted_branch_and_manual_diagnostics_are_rejected(self):
        for changes in ({"head_sha": "f" * 40}, {"head_repository": {"full_name": "other/fork"}},
                        {"head_repository": None}, {"head_branch": "feature"},
                        {"event": "workflow_dispatch"}, {"conclusion": "failure"}):
            with self.subTest(changes=changes), self.assertRaisesRegex(SystemExit, "this exact commit"):
                self.select([build(12, **changes)])

    def test_newer_skipped_or_expired_run_does_not_hide_the_verified_exact_build(self):
        jobs = {20: [{"name": "host", "conclusion": "skipped"},
                     {"name": "dreamcast", "conclusion": "skipped"}]}
        artifacts = {19: [{"name": "kui-1.8.5-dainsleif-release-assets", "expired": True}]}
        (run, _), _ = self.select([build(18), build(20), build(19)], jobs, artifacts)
        self.assertEqual(run["id"], 18)

    def test_both_host_and_native_jobs_must_succeed(self):
        for failed in ("host", "dreamcast"):
            jobs = {21: [{"name": name, "conclusion": "failure" if name == failed else "success"}
                         for name in ("host", "dreamcast")]}
            with self.subTest(failed=failed), self.assertRaisesRegex(SystemExit, "fully successful"):
                self.select([build(21)], jobs)

    def test_ambiguous_candidate_or_diagnostic_assets_are_rejected(self):
        ordinary = {"name": "kui-1.8.5-dainsleif-release-assets", "expired": False}
        for entries in ([ordinary, ordinary],
                        [{"name": "kui-1.8.5-dainsleif-release-assets-low-resident", "expired": False}],
                        [{"name": "kui-1.8.5-dainsleif-ata-readiness-candidate-release-assets", "expired": False}]):
            with self.subTest(entries=entries), self.assertRaisesRegex(SystemExit, "release assets"):
                self.select([build(22)], artifacts={22: entries})


class ExplicitReleaseSelection(unittest.TestCase):
    def test_known_versions_and_post_paths_are_explicitly_scoped(self):
        for version in ("1.7", "1.8.5"):
            with self.subTest(version=version):
                self.assertEqual(publish_release.release_tag({"version": version}), "v" + version)
                allowed = publish_release.post_allowed_paths(version)
                self.assertIn(f"docs/release-v{version}-notes.md", allowed)
                other = "1.7" if version == "1.8.5" else "1.8.5"
                self.assertNotIn(f"docs/release-v{other}-notes.md", allowed)
                self.assertNotIn("src/loader/retail_stage.c", allowed)
        for version in ("1.8.6", "1.8.5-formats-test", "../1.8.5"):
            with self.subTest(version=version), self.assertRaises(SystemExit):
                publish_release.release_tag({"version": version})

    def test_stale_main_prevents_artifact_download_or_publication(self):
        env = {"GITHUB_REPOSITORY": REPOSITORY, "GITHUB_REF": "refs/heads/main", "GITHUB_SHA": COMMIT}
        with patch.dict(os.environ, env), patch.object(sys, "argv", ["publish_release.py"]), \
                patch.object(publish_release, "release_metadata", return_value=RELEASE), \
                patch.object(publish_release.subprocess, "check_output", return_value="Release: v1.8.5\n"), \
                patch.object(publish_release, "api", return_value={"object": {"sha": "d" * 40}}), \
                patch.object(publish_release, "gh") as gh:
            with self.assertRaisesRegex(SystemExit, "stale artifacts"):
                publish_release.main()
            gh.assert_not_called()

    def test_no_explicit_version_line_prevents_publication(self):
        env = {"GITHUB_REPOSITORY": REPOSITORY, "GITHUB_REF": "refs/heads/main", "GITHUB_SHA": COMMIT}
        for message in ("Prepare Release: v1.8.5\n", "Release: v1.7\n"):
            with self.subTest(message=message), patch.dict(os.environ, env), \
                    patch.object(sys, "argv", ["publish_release.py"]), \
                    patch.object(publish_release, "release_metadata", return_value=RELEASE), \
                    patch.object(publish_release.subprocess, "check_output", return_value=message), \
                    patch.object(publish_release, "api") as api, patch.object(publish_release, "gh") as gh:
                with self.assertRaisesRegex(SystemExit, "explicit release request"):
                    publish_release.main()
                api.assert_not_called()
                gh.assert_not_called()


if __name__ == "__main__":
    unittest.main()
