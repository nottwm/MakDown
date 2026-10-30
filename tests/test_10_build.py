# SPDX-License-Identifier: MIT
"""Build-system tests, always performed in a throw-away COPY of the tree so your build/ directory is untouched.

* a from-scratch build has no unexpected compiler warnings (allow-list: tests/known_warnings.txt)
* incremental builds behave: nothing to do when nothing changed; touching a header rebuilds its users
* build variants from the README (WEBENGINE/MULTIMEDIA/Qt5) compile when their Qt modules are installed
* `make clean`, `make help`
"""
import os
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lib.common import ROOT, TESTS, pkg_exists, read, run, which

JOBS = f"-j{os.cpu_count() or 2}"


def copy_tree(dst: Path):
    shutil.copytree(ROOT, dst, ignore=shutil.ignore_patterns("build", ".git", "__pycache__", "*.pyc"))
    return dst


def known_warning_patterns():
    return [re.compile(l.strip()) for l in read(TESTS / "known_warnings.txt").splitlines() if l.strip() and not l.startswith("#")]


def warnings_in(output: str):
    return [l for l in output.splitlines() if re.search(r"warning:", l)]


class Copy(unittest.TestCase):
    """Mixin: self.tree is a fresh copy of the repo for each test class."""
    tree: Path

    @classmethod
    def setUpClass(cls):
        if not (which("g++") and which("make") and pkg_exists("Qt6Widgets")):
            raise unittest.SkipTest("toolchain / Qt6 not available")
        cls.tree = copy_tree(Path(tempfile.mkdtemp(prefix="makdown-build-")) / "src")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tree.parent, ignore_errors=True)

    def make(self, *args, env=None, timeout=1800):
        e = dict(os.environ, **(env or {}))
        return run(["make", *args], cwd=self.tree, env=e, timeout=timeout)


class TestCleanBuild(Copy):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.p = run(["make", "unx", JOBS], cwd=cls.tree, timeout=1800)

    def test_build_succeeds(self):
        self.assertEqual(self.p.returncode, 0, self.p.stdout[-3000:] + self.p.stderr[-3000:])
        exe = self.tree / "build/unx/MakDown"
        self.assertTrue(exe.exists() and os.access(exe, os.X_OK))
        self.assertGreater(exe.stat().st_size, 500_000)

    def test_no_unexpected_compiler_warnings(self):
        allowed = known_warning_patterns()
        found = [w for w in warnings_in(self.p.stdout + self.p.stderr) if not any(a.search(w) for a in allowed)]
        self.assertEqual(found, [], "new compiler warnings (fix them, or justify them in tests/known_warnings.txt)")

    def test_allow_list_has_no_stale_entries(self):
        text = self.p.stdout + self.p.stderr
        stale = [a.pattern for a in known_warning_patterns() if not any(a.search(w) for w in warnings_in(text))]
        self.assertEqual(stale, [], "tests/known_warnings.txt lists warnings that no longer occur - delete them")

    def test_no_error_or_note_noise(self):
        self.assertNotRegex(self.p.stderr, r"\berror\b")

    def test_nothing_to_do_on_second_build(self):
        q = self.make("unx")
        self.assertEqual(q.returncode, 0)
        self.assertNotIn("Compiling", q.stdout, "second build recompiled something: dependency tracking is broken")
        self.assertNotIn("Linking", q.stdout)

    def test_touching_a_header_rebuilds_its_users_only(self):
        h = self.tree / "include/makdown/core/search.h"
        os.utime(h, None)
        q = self.make("unx", JOBS)
        self.assertEqual(q.returncode, 0, q.stderr[-1500:])
        compiled = set(re.findall(r"Compiling\s+\S*?(\S+\.cpp)", q.stdout)) or set(re.findall(r"(\S+\.cpp)", q.stdout))
        self.assertTrue(any("search" in c for c in compiled), f"search.cpp not rebuilt after touching search.h: {compiled}")
        self.assertFalse(any("theme.cpp" in c or "markdown.cpp" in c for c in compiled), f"unrelated files rebuilt: {compiled}")

    def test_changing_a_source_relinks(self):
        os.utime(self.tree / "core/anim.cpp", None)
        q = self.make("unx", JOBS)
        self.assertIn("Linking", q.stdout)

    def test_make_clean_removes_build_dir(self):
        t = copy_tree(Path(tempfile.mkdtemp(prefix="makdown-clean-")) / "src")
        try:
            (t / "build/unx").mkdir(parents=True)
            (t / "build/unx/x.o").write_text("x")
            self.assertEqual(run(["make", "clean"], cwd=t).returncode, 0)
            self.assertFalse((t / "build/unx").exists())
            (t / "build/win").mkdir(parents=True)
            self.assertEqual(run(["make", "mrproper"], cwd=t).returncode, 0)
            self.assertFalse((t / "build").exists())
        finally:
            shutil.rmtree(t.parent, ignore_errors=True)


class TestMakeHelp(unittest.TestCase):
    def test_help_prints_usage(self):
        p = run(["make", "help"])
        self.assertEqual(p.returncode, 0)
        for t in ("make unx", "make win", "make deploy", "make clean", "make help"):
            self.assertIn(t, p.stdout)

    @unittest.expectedFailure
    def test_KNOWN_ISSUE_help_text_is_cut_off(self):
        """`make help` runs `sed -n '4,18p' Makefile`, but the header comment now ends at line 21, so the MULTIMEDIA= and
        WEBENGINE= variable descriptions (and the Documentation/building.md pointer) are not shown."""
        p = run(["make", "help"])
        for t in ("MULTIMEDIA", "WEBENGINE", "building.md"):
            self.assertIn(t, p.stdout)


class TestVariants(unittest.TestCase):
    """Variants that README / building.md advertise. Each is skipped (with the reason) if its Qt modules are missing."""

    def build_variant(self, *make_args, env=None):
        d = copy_tree(Path(tempfile.mkdtemp(prefix="makdown-variant-")) / "src")
        try:
            p = run(["make", "unx", JOBS, *make_args], cwd=d, env=dict(os.environ, **(env or {})), timeout=2400)
            return d, p
        except Exception:
            shutil.rmtree(d.parent, ignore_errors=True)
            raise

    def check(self, d, p):
        try:
            self.assertEqual(p.returncode, 0, p.stdout[-2500:] + p.stderr[-2500:])
            self.assertTrue((d / "build/unx/MakDown").exists())
            run_p = run([str(d / "build/unx/MakDown"), "--version"], env=dict(os.environ, QT_QPA_PLATFORM="offscreen"))
            self.assertEqual(run_p.returncode, 0, run_p.stderr)
            self.assertRegex(run_p.stdout, r"^MakDown \d")
        finally:
            shutil.rmtree(d.parent, ignore_errors=True)

    def test_explicit_no_webengine_no_multimedia(self):
        if not pkg_exists("Qt6Widgets"):
            self.skipTest("Qt6 not installed")
        self.check(*self.build_variant("WEBENGINE=no", "MULTIMEDIA=no"))

    def test_multimedia_variant(self):
        if not pkg_exists("Qt6Multimedia"):
            self.skipTest("Qt6Multimedia dev package not installed (apt install qt6-multimedia-dev)")
        self.check(*self.build_variant("MULTIMEDIA=yes", "WEBENGINE=no"))

    def test_webengine_variant(self):
        if not pkg_exists("Qt6WebEngineWidgets"):
            self.skipTest("Qt6 WebEngine dev package not installed")
        self.check(*self.build_variant("WEBENGINE=yes", "MULTIMEDIA=no"))

    def test_qt5_variant(self):
        if not (pkg_exists("Qt5Widgets") and which("moc") or pkg_exists("Qt5Widgets")):
            self.skipTest("Qt5 dev package not installed (apt install qtbase5-dev)")
        self.check(*self.build_variant("QT=Qt5", "MULTIMEDIA=no", "WEBENGINE=no"))

    def test_clang_build(self):
        if not which("clang++"):
            self.skipTest("clang++ not installed")
        d, p = self.build_variant(env={"CXX": "clang++"})
        self.check(d, p)

    def test_windows_cross_compile(self):
        if not which("x86_64-w64-mingw32-g++"):
            self.skipTest("MinGW-w64 cross compiler not installed")
        if not pkg_exists("Qt6Widgets"):
            self.skipTest("no Qt for the Windows target")
        self.skipTest("needs a MinGW Qt6 on PKG_CONFIG_PATH; run `make win` on a Windows/MSYS2 builder")


if __name__ == "__main__":
    unittest.main(verbosity=2)
