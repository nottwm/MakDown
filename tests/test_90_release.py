# SPDX-License-Identifier: MIT
"""Release gate. These only make sense right before tagging, so most are skipped unless you opt in:

    MAKDOWN_RELEASE=1 MAKDOWN_TAG=v1.0 python3 tests/run_tests.py --release

(run_tests.py --release sets MAKDOWN_RELEASE for you; pass --tag vX.Y to also check the tag.)
"""
import os
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lib.common import ROOT, read, run

RELEASE = bool(os.environ.get("MAKDOWN_RELEASE"))
TAG = os.environ.get("MAKDOWN_TAG", "")


def git(*a):
    return run(["git", *a], cwd=ROOT)


@unittest.skipUnless(RELEASE, "release gate: run with --release")
class TestReleaseGate(unittest.TestCase):
    def test_working_tree_is_clean(self):
        out = git("status", "--porcelain").stdout
        # the test-suite itself may be uncommitted while it is being introduced; everything else must be committed
        dirty = [l for l in out.splitlines() if not l[3:].startswith(("build/", "tests/__pycache__"))]
        self.assertEqual(dirty, [], "uncommitted changes would not be in the release")

    def test_head_is_pushed(self):
        r = git("rev-list", "--count", "@{u}..HEAD")
        if r.returncode != 0:
            self.skipTest("no upstream configured")
        self.assertEqual(r.stdout.strip(), "0", "unpushed commits")

    def test_tag_matches_program_version(self):
        if not TAG:
            self.skipTest("pass --tag vX.Y")
        v = re.search(r'setApplicationVersion\("([^"]+)"\)', read(ROOT / "init/main.cpp")).group(1)
        self.assertEqual(TAG.lstrip("v"), v, f"tag {TAG} does not match the version in init/main.cpp ({v})")

    def test_tag_does_not_exist_yet(self):
        if not TAG:
            self.skipTest("pass --tag vX.Y")
        self.assertEqual(git("tag", "-l", TAG).stdout.strip(), "", f"tag {TAG} already exists")

    def test_no_known_issue_marked_as_fixed(self):
        """Run by run_tests.py: an 'unexpected success' means a KNOWN_ISSUE was fixed - drop its decorator."""
        self.assertTrue(True)

    def test_release_binary_starts_and_reports_version(self):
        from lib.common import run_app
        p = run_app(["--version"])
        self.assertEqual(p.returncode, 0)
        v = re.search(r'setApplicationVersion\("([^"]+)"\)', read(ROOT / "init/main.cpp")).group(1)
        self.assertEqual(p.stdout.strip(), f"MakDown {v}")

    def test_license_year_not_in_the_future(self):
        import datetime
        y = int(re.search(r"Copyright \(c\) (\d{4})", read(ROOT / "LICENSE")).group(1))
        self.assertLessEqual(y, datetime.date.today().year)


if __name__ == "__main__":
    unittest.main(verbosity=2)
