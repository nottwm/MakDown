#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""MakDown test runner - run this before opening a PR or publishing a release.

    python3 tests/run_tests.py                 # everything except the slow build-variant matrix
    python3 tests/run_tests.py --fast          # skip real-binary start-up and build tests (seconds)
    python3 tests/run_tests.py --full          # also compile in copies (warnings, incremental, Qt variants)
    python3 tests/run_tests.py --release --tag v1.0   # full + release gate
    python3 tests/run_tests.py --sanitize      # rebuild a copy with ASan+UBSan and run the in-process tests there
    python3 tests/run_tests.py -k markdown     # only modules whose name contains 'markdown'
    python3 tests/run_tests.py --list

Exit status 0 = safe to release, 1 = something failed. A KNOWN_ISSUE test that starts passing is reported as a
failure ("unexpected success") so the decorator gets removed and the fix is locked in.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from lib.common import ROOT, run  # noqa: E402

GROUPS = {
    "static": ["test_00_repo"],
    "unit": ["test_30_markdown", "test_40_search", "test_50_syntax", "test_60_theme", "test_65_lang", "test_70_editor"],
    "app": ["test_20_cli", "test_80_window"],
    "build": ["test_10_build"],
    "release": ["test_90_release"],
}


class Result(unittest.TextTestResult):
    def addUnexpectedSuccess(self, test):
        super().addUnexpectedSuccess(test)
        self.stream.writeln(f"  >> {test.id()} unexpectedly PASSED: the known issue is fixed, remove its @expectedFailure")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fast", action="store_true", help="static + in-process unit tests only")
    ap.add_argument("--full", action="store_true", help="include build tests (slow: compiles copies of the tree)")
    ap.add_argument("--release", action="store_true", help="full + release gate")
    ap.add_argument("--tag", default="", help="expected release tag, e.g. v1.0 (with --release)")
    ap.add_argument("--sanitize", action="store_true", help="build a copy with -fsanitize=address,undefined and run unit+app tests there")
    ap.add_argument("-k", default="", help="only modules whose name contains this text")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--failfast", action="store_true")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()

    if a.sanitize:
        return sanitize(a)

    mods = list(GROUPS["static"]) + list(GROUPS["unit"])
    if not a.fast:
        mods += GROUPS["app"]
    if a.full or a.release:
        mods += GROUPS["build"]
    if a.release:
        mods += GROUPS["release"]
        os.environ["MAKDOWN_RELEASE"] = "1"
        if a.tag:
            os.environ["MAKDOWN_TAG"] = a.tag
    if a.k:
        mods = [m for m in mods if a.k in m]
    mods = sorted(dict.fromkeys(mods))
    if a.list:
        print("\n".join(mods))
        return 0

    from lib.common import ensure_app_built, ensure_harness_built
    print(f"== MakDown test-suite ({len(mods)} modules) ==", flush=True)
    t0 = time.time()
    if any(m not in GROUPS["static"] + GROUPS["build"] + GROUPS["release"] for m in mods):
        print("building application + harness ...", flush=True)
        ensure_app_built()
        ensure_harness_built()

    suite = unittest.TestSuite()
    for m in mods:
        suite.addTests(unittest.defaultTestLoader.loadTestsFromName(m))
    runner = unittest.TextTestRunner(verbosity=2 if a.verbose else 1, resultclass=Result, failfast=a.failfast, buffer=True)
    res = runner.run(suite)
    known = len(res.expectedFailures)
    print(f"\n{res.testsRun} tests in {time.time() - t0:.0f}s: {len(res.failures)} failed, {len(res.errors)} errors, "
          f"{len(res.skipped)} skipped, {known} known issues (expected failures), {len(res.unexpectedSuccesses)} unexpectedly fixed")
    for t, _ in res.skipped:
        print("  skipped:", t.id().split('.', 1)[1], "-", _)
    if known:
        print("known issues still open:")
        for t, _ in res.expectedFailures:
            print("  -", t.id().split(".", 1)[1])
    ok = res.wasSuccessful()
    print("RESULT:", "PASS - OK to release" if ok else "FAIL - do not release")
    return 0 if ok else 1


def sanitize(a):
    tmp = Path(tempfile.mkdtemp(prefix="makdown-asan-"))
    src = tmp / "src"
    try:
        shutil.copytree(ROOT, src, ignore=shutil.ignore_patterns("build", ".git", "__pycache__"))
        cxx = "g++ -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
        env = dict(os.environ, CXX=cxx, CXXFLAGS="-O1 -g", MAKDOWN_ROOT=str(src), ASAN_OPTIONS="detect_leaks=0:abort_on_error=0",
                   UBSAN_OPTIONS="print_stacktrace=1")
        print("building with ASan+UBSan (a few minutes) ...", flush=True)
        p = run(["make", "unx", f"-j{os.cpu_count() or 2}"], cwd=src, env=env, timeout=3600)
        if p.returncode != 0:
            print(p.stdout[-3000:], p.stderr[-3000:])
            return 1
        cmd = [sys.executable, str(src / "tests/run_tests.py"), "--fast"]
        if a.k:
            cmd += ["-k", a.k]
        return subprocess.call(cmd, cwd=src, env=env)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
