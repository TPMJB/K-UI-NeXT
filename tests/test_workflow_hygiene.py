# SPDX-License-Identifier: GPL-3.0-only
"""The CI's compile step must leave the checkout exactly as it found it.

tools/record_build.py (run at link time) and tools/package.py both refuse a dirty tree
(`git status --porcelain` must be empty; an untracked file counts). The compile step once wrote its
log to build.log in the repository root, so every otherwise-green build died at packaging with
"Commit source changes before packaging". This test runs the ACTUAL text of that step, in a scratch
git repository with a stub `make`, on success and on failure, and checks the tree afterwards."""
import os
import pathlib
import subprocess
import tempfile
import textwrap
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
WORKFLOW = ROOT / ".github/workflows/diagnostic.yml"
STEP = "name: Compile CD bootstrap and SD runtime"


def step_script(step=STEP):
    """The `run: |` block of the compile step, taken from the workflow text (no YAML library needed)."""
    lines = WORKFLOW.read_text().splitlines()
    start = next(n for n, line in enumerate(lines) if step in line)
    run = next(n for n in range(start, len(lines)) if lines[n].strip() == "run: |")
    indent = len(lines[run]) - len(lines[run].lstrip())
    body = []
    for line in lines[run + 1:]:
        if line.strip() and len(line) - len(line.lstrip()) <= indent:
            break
        body.append(line)
    return textwrap.dedent("\n".join(body))


def git(repo, *args):
    return subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", *args], cwd=repo,
                          capture_output=True, text=True, check=True).stdout


def run_step(make_exit):
    with tempfile.TemporaryDirectory() as d:
        base = pathlib.Path(d)
        repo, runner_tmp, bindir = base / "repo", base / "runner_tmp", base / "bin"
        for p in (repo, runner_tmp, bindir):
            p.mkdir()
        (repo / ".gitignore").write_text((ROOT / ".gitignore").read_text())
        (repo / ".deps/kos").mkdir(parents=True)                       # ignored, like the real one
        (repo / ".deps/kos/environ.sh").write_text("")
        (repo / "README").write_text("source\n")
        git(repo, "init", "-q")
        git(repo, "add", "-A")
        git(repo, "commit", "-q", "-m", "clean checkout")
        make = bindir / "make"                                          # a stand-in for the real build
        make.write_text("#!/bin/sh\necho 'kos-cc -c src/x.c'\nif [ \"$FAKE_MAKE_EXIT\" != 0 ]; then\n"
                        "  echo 'src/x.c:1:1: error: something is wrong'\n"
                        "  echo 'make[1]: *** [Makefile.dc:35: build/x.o] Error 1'\nfi\nexit $FAKE_MAKE_EXIT\n")
        make.chmod(0o755)
        env = dict(os.environ, PATH=f"{bindir}:{os.environ['PATH']}", RUNNER_TEMP=str(runner_tmp),
                   GITHUB_SHA="0123456789abcdef" * 2 + "01234567", FAKE_MAKE_EXIT=str(make_exit))
        # GitHub's default shell for `shell: bash`.
        result = subprocess.run(["bash", "--noprofile", "--norc", "-e", "-o", "pipefail", "-c", step_script()],
                                cwd=repo, env=env, capture_output=True, text=True)
        return result, git(repo, "status", "--porcelain")


class CompileStepLeavesTheTreeClean(unittest.TestCase):
    def test_a_successful_build_leaves_no_untracked_file(self):
        result, dirty = run_step(0)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(dirty, "", f"the compile step dirtied the checkout: {dirty!r}; package.py would refuse it")

    def test_a_failed_build_leaves_no_untracked_file_and_prints_its_errors_last(self):
        result, dirty = run_step(2)
        self.assertEqual(result.returncode, 2, "a failed build must still fail the job with make's status")
        self.assertEqual(dirty, "", f"the compile step dirtied the checkout: {dirty!r}")
        tail = result.stdout.strip().splitlines()[-6:]
        self.assertTrue(any("BUILD FAILED" in line for line in tail) or "BUILD FAILED" in result.stdout)
        digest = result.stdout.split("BUILD FAILED", 1)[1]
        self.assertIn("error: something is wrong", digest)          # the error is repeated AFTER the banner
        self.assertIn("*** [Makefile.dc:35", digest)


class GamesVariantValidationScope(unittest.TestCase):
    def run_scope(self, changed_path, scope="games-variants"):
        with tempfile.TemporaryDirectory() as d:
            base = pathlib.Path(d)
            repo = base / "repo"
            repo.mkdir()
            (repo / "README").write_text("base\n")
            git(repo, "init", "-q")
            git(repo, "add", "-A")
            git(repo, "commit", "-q", "-m", "base")
            changed = repo / changed_path
            changed.parent.mkdir(parents=True, exist_ok=True)
            changed.write_text("changed\n")
            git(repo, "add", "-A")
            git(repo, "commit", "-q", "-m", "Test Games change\n\nValidation: " + scope)
            output = base / "scope-output"
            env = dict(os.environ, KUI_VALIDATION_HEAD=git(repo, "rev-parse", "HEAD").strip(),
                       GITHUB_OUTPUT=str(output))
            result = subprocess.run(["bash", "-e", "-o", "pipefail", "-c",
                                     step_script("name: Select requested validation scope")],
                                    cwd=repo, env=env, capture_output=True, text=True)
            return result, output.read_text() if output.exists() else ""

    def test_selector_changes_use_recorded_focused_checks(self):
        result, output = self.run_scope("include/kui/games.h")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("console_only=true", output)

    def test_reader_changes_cannot_skip_the_broad_host_suite(self):
        result, output = self.run_scope("src/loader/retail_sd.c")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("outside listing, selector UI", result.stderr)
        self.assertEqual(output, "")

    def test_navigation_correction_uses_recorded_focused_checks(self):
        result, output = self.run_scope("src/dreamcast/main.c")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("console_only=true", output)


class GamesFormatsValidationScope(unittest.TestCase):
    def test_formats_run_portable_and_relevant_filesystem_checks(self):
        for path in ("src/core/game_image.c", "src/loader/retail_resident.c", "tools/game_image_import.py"):
            with self.subTest(path=path):
                result, output = GamesVariantValidationScope().run_scope(path, "games-formats")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("console_only=false", output)
                self.assertIn("games_formats=true", output)
        script = step_script("name: Run Games format checks")
        self.assertIn("make test ", script)
        for check in ("test_games_retail.py", "test_games_images.py", "test_games_covers_images.py", "test_games_image_probe.py"):
            self.assertIn(check, script)

    def test_unrelated_transport_changes_require_the_broad_suite(self):
        result, output = GamesVariantValidationScope().run_scope("src/loader/sci_stream.c", "games-formats")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("outside image formats", result.stderr)
        self.assertEqual(output, "")


class ExperimentalBuildIsOptIn(unittest.TestCase):
    """The DMA probe must never be in an ordinary build, and must be reachable without the
    "Run workflow" button: that button only exists when the workflow file is on the repository's
    DEFAULT branch, and this repository's default branch carries no workflows at all."""

    def setUp(self):
        self.text = WORKFLOW.read_text()
        self.flag = next(l for l in self.text.splitlines() if "KUI_EXPERIMENTAL:" in l)

    def test_a_branch_name_can_turn_it_on(self):
        self.assertIn("endsWith(github.ref_name, '-experimental')", self.flag)

    def test_the_manual_button_still_works_where_it_exists(self):
        self.assertIn("inputs.experimental", self.flag)
        self.assertIn("experimental:", self.text)      # the workflow_dispatch input is declared

    def test_ordinary_builds_get_nothing(self):
        # The expression yields '' unless one of the two conditions holds, and the make line
        # passes exactly that.
        self.assertRegex(self.flag, r"&& '1' \|\| ''")
        self.assertIn("make -k diagnostic BUILD_ID=\"$(git rev-parse --short=12 HEAD)\" $KUI_EXPERIMENTAL_FLAG", self.text)

    def test_the_artifacts_are_named_apart(self):
        for name in ("diagnostic", "sd-update", "sd-benchmark", "bootstrap-cd", "release"):
            self.assertIn("name: ${{ steps.package.outputs.artifact_prefix }}-" + name +
                          "${{ env.KUI_EXPERIMENTAL != '' && '-experimental' || '' }}", self.text)


if __name__ == "__main__":
    unittest.main()
