# SPDX-License-Identifier: GPL-3.0-only
"""A capture must not be committable by accident.

Verifying a dump on a PC means running tools/verify_dump.py on a copy of its job folder, and it is natural to
copy that folder into the repository next to the tool. `git add -A` then staged a 1.1 GB track03.bin and GitHub
rejected the push (its limit is 100 MB), after a 622 MB upload. .gitignore now covers the job folders and the
file names inside them, and this test proves that with a real git repository: it puts a fake dump where it
happened, and where else it might, and requires that git sees nothing to add."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
DUMP_FILES = ("track01.bin", "track02.raw", "track03.bin", "checkpoint-a.bin", "checkpoint-b.bin", "disc.gdi", "manifest.json")
LIMIT = 50 * 1024 * 1024   # GitHub refuses 100 MB; anything near half of that does not belong in this repository


def git(repo, *args):
    return subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", *args], cwd=repo,
                          capture_output=True, text=True, check=True).stdout


class DumpsCannotBeCommitted(unittest.TestCase):
    def test_a_dump_copied_into_the_repository_is_invisible_to_git_add(self):
        with tempfile.TemporaryDirectory() as d:
            repo = pathlib.Path(d)
            (repo / ".gitignore").write_text((ROOT / ".gitignore").read_text())
            (repo / "README").write_text("source\n")
            git(repo, "init", "-q")
            git(repo, "add", "-A")
            git(repo, "commit", "-q", "-m", "clean")
            for where in ("tools/df4db30d38d4d8926-0001", "df4db30d38d4d8926-0002", "docs/evidence/a1b2c3-0003"):
                folder = repo / where
                folder.mkdir(parents=True)
                for name in DUMP_FILES:
                    (folder / name).write_bytes(b"x")
            for name in DUMP_FILES[:6]:                       # copied out one by one, into tools/
                (repo / "tools" / name).write_bytes(b"x")
            status = git(repo, "status", "--porcelain", "--untracked-files=all")
            self.assertEqual(status.replace("?? tools/manifest.json\n", ""), "",
                             f"git would add capture files (only the generic manifest.json may show): {status!r}")

    def test_no_tracked_file_is_large(self):
        if not (ROOT / ".git").exists():
            self.skipTest("not a git checkout")
        names = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True).stdout.split(b"\0")
        big = [(n.decode(), (ROOT / n.decode()).stat().st_size) for n in names if n and (ROOT / n.decode()).is_file()
               and (ROOT / n.decode()).stat().st_size > LIMIT]
        self.assertEqual(big, [], "tracked files too large for GitHub (100 MB limit): keep dumps outside the repository")


if __name__ == "__main__":
    unittest.main()
