# SPDX-License-Identifier: MIT
"""Shared helpers for the MakDown test-suite: paths, building, and the C++ harness client.

Everything here uses only the Python standard library so the suite runs anywhere Python 3.8+ exists.
"""
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path

# The suite can be run against a copy of the tree (sanitizer stage): MAKDOWN_ROOT overrides the location.
ROOT = Path(os.environ.get("MAKDOWN_ROOT") or Path(__file__).resolve().parents[2]).resolve()
TESTS = ROOT / "tests"
BUILD = ROOT / "build" / "unx"
APP = BUILD / "MakDown"
HARNESS = ROOT / "build" / "tests" / "harness"
HARNESS_SRC = TESTS / "harness" / "harness.cpp"
FIXTURES = TESTS / "fixtures"

SOURCE_DIRS = ["init", "core", "ui", "arch/unx", "arch/win"]


# ------------------------------------------------------------------------------------------------
# environment
# ------------------------------------------------------------------------------------------------
def gui_env(home: str | None = None, extra: dict | None = None) -> dict:
    """Environment for running Qt code headless with an isolated $HOME (never touches the real one)."""
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["QT_LOGGING_RULES"] = "qt.qpa.fonts.warning=false"
    env.pop("MAKDOWN_DEBUG", None)
    if home:
        env["HOME"] = home
        env["XDG_CONFIG_HOME"] = os.path.join(home, ".config")
        env["XDG_DATA_HOME"] = os.path.join(home, ".local", "share")
    env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    if extra:
        env.update(extra)
    return env


def run(cmd, cwd=None, env=None, timeout=900, check=False, input=None):
    """subprocess.run with text output and a sane default timeout."""
    p = subprocess.run(cmd, cwd=str(cwd or ROOT), env=env, input=input, text=True, errors="replace",
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    if check and p.returncode != 0:
        raise RuntimeError(f"command failed ({p.returncode}): {cmd}\n{p.stdout}\n{p.stderr}")
    return p


def which(*names):
    for n in names:
        p = shutil.which(n)
        if p:
            return p
    return None


def pkg_exists(pkg: str) -> bool:
    return bool(which("pkg-config")) and run(["pkg-config", "--exists", pkg]).returncode == 0


def qt_flags(pkgs: str):
    cflags = run(["pkg-config", "--cflags", *pkgs.split()]).stdout.split()
    libs = run(["pkg-config", "--libs", *pkgs.split()]).stdout.split()
    return cflags, libs


def tracked_files() -> list[str]:
    """Files under version control (falls back to a directory walk when this is not a git checkout)."""
    p = run(["git", "ls-files"], cwd=ROOT)
    if p.returncode == 0 and p.stdout.strip():
        return [l for l in p.stdout.splitlines() if (ROOT / l).exists()]
    out = []
    for dp, dn, fn in os.walk(ROOT):
        dn[:] = [d for d in dn if d not in (".git", "build", "__pycache__")]
        out += [os.path.relpath(os.path.join(dp, f), ROOT) for f in fn]
    return out


def read(path) -> str:
    return Path(path).read_text(encoding="utf-8", errors="replace")


# ------------------------------------------------------------------------------------------------
# building the application + the harness
# ------------------------------------------------------------------------------------------------
_build_lock = threading.Lock()
_built = {"app": False, "harness": False}


def ensure_app_built() -> Path:
    """Builds build/unx/MakDown if it is missing or stale (make decides). Raises when the build fails."""
    with _build_lock:
        if not _built["app"]:
            p = run(["make", "unx", f"-j{os.cpu_count() or 2}"], timeout=1800)
            if p.returncode != 0:
                raise RuntimeError("make unx failed:\n" + p.stdout[-4000:] + p.stderr[-4000:])
            _built["app"] = True
    return APP


def ensure_harness_built() -> Path:
    """Compiles tests/harness/harness.cpp against the application's own object files."""
    ensure_app_built()
    with _build_lock:
        if _built["harness"]:
            return HARNESS
        objs = sorted(str(p) for p in BUILD.rglob("*.o") if not str(p).endswith("init/main.o"))
        newest = max([Path(o).stat().st_mtime for o in objs] + [HARNESS_SRC.stat().st_mtime])
        if HARNESS.exists() and HARNESS.stat().st_mtime >= newest:
            _built["harness"] = True
            return HARNESS
        HARNESS.parent.mkdir(parents=True, exist_ok=True)
        defs = ["-DMAKDOWN_UNX"]
        pkgs = "Qt6Widgets Qt6Test"
        if "-DMAKDOWN_WEBENGINE" in read(BUILD / ".cfg"):
            defs.append("-DMAKDOWN_WEBENGINE")
            pkgs += " Qt6WebEngineWidgets"
        if "-DMAKDOWN_MULTIMEDIA" in read(BUILD / ".cfg"):
            defs.append("-DMAKDOWN_MULTIMEDIA")
            pkgs += " Qt6Multimedia"
        cflags, libs = qt_flags(pkgs)
        cxx = os.environ.get("CXX", "g++").split()
        cmd = [*cxx, "-std=c++17", "-fPIC", "-O1", "-g", *defs, *cflags, f"-I{ROOT/'include'}", str(HARNESS_SRC), *objs,
               "-o", str(HARNESS), *libs]
        p = run(cmd, timeout=900)
        if p.returncode != 0:
            raise RuntimeError("harness build failed:\n" + p.stderr[-4000:])
        _built["harness"] = True
    return HARNESS


# ------------------------------------------------------------------------------------------------
# harness client
# ------------------------------------------------------------------------------------------------
class Harness:
    """Talks JSON-lines to the C++ harness. One process = one isolated $HOME unless told otherwise."""

    def __init__(self, home: str | None = None, timeout: float = 60):
        self.tmp = None
        if home is None:
            self.tmp = tempfile.mkdtemp(prefix="makdown-home-")
            home = self.tmp
        self.home = home
        self.timeout = timeout
        self.proc = subprocess.Popen([str(ensure_harness_built())], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True, encoding="utf-8", env=gui_env(home), bufsize=1)
        self._stderr = []
        threading.Thread(target=lambda: [self._stderr.append(l) for l in self.proc.stderr], daemon=True).start()

    def call(self, cmd: str, **kw) -> dict:
        if self.proc.poll() is not None:
            raise RuntimeError(f"harness died (exit {self.proc.returncode}); stderr:\n{''.join(self._stderr)[-3000:]}")
        kw["cmd"] = cmd
        self.proc.stdin.write(json.dumps(kw) + "\n")
        self.proc.stdin.flush()
        box = {}
        t = threading.Thread(target=lambda: box.setdefault("line", self.proc.stdout.readline()), daemon=True)
        t.start()
        t.join(self.timeout)
        if t.is_alive() or not box.get("line"):
            self.proc.kill()
            raise RuntimeError(f"harness did not answer '{cmd}' within {self.timeout}s (or crashed); stderr:\n{''.join(self._stderr)[-3000:]}")
        r = json.loads(box["line"])
        if not r.get("ok", True):
            raise RuntimeError(f"harness error for {cmd}: {r.get('error')}")
        return r

    def close(self):
        try:
            if self.proc.poll() is None:
                self.proc.stdin.write("quit\n")
                self.proc.stdin.flush()
                self.proc.wait(10)
        except Exception:
            self.proc.kill()
        for f in (self.proc.stdin, self.proc.stdout, self.proc.stderr):
            try:
                f.close()
            except Exception:
                pass
        if self.tmp:
            shutil.rmtree(self.tmp, ignore_errors=True)

    @property
    def stderr(self) -> str:
        return "".join(self._stderr)


class HarnessCase(unittest.TestCase):
    """Base class: one shared harness per test class (fast), warnings are collected and asserted on."""
    h: Harness

    @classmethod
    def setUpClass(cls):
        cls.h = Harness()

    @classmethod
    def tearDownClass(cls):
        cls.h.close()

    # convenience -----------------------------------------------------------
    def call(self, cmd, **kw):
        r = self.h.call(cmd, **kw)
        self.assertEqual(r.get("warnings", []), [], f"Qt emitted warnings during '{cmd}': {r.get('warnings')}")
        return r

    def render(self, md, **kw):
        return self.call("render", md=md, **kw)["html"]

    def plain(self, md, **kw):
        return self.call("render_plain", md=md, **kw)["plain"]


# ------------------------------------------------------------------------------------------------
# running the real binary
# ------------------------------------------------------------------------------------------------
def run_app(args=(), home=None, timeout=15, env_extra=None, platform="offscreen"):
    """Runs the real MakDown binary to completion (for flags that exit by themselves)."""
    ensure_app_built()
    own = home is None
    home = home or tempfile.mkdtemp(prefix="makdown-home-")
    env = gui_env(home, env_extra)
    env["QT_QPA_PLATFORM"] = platform
    try:
        return run([str(APP), *args], env=env, timeout=timeout)
    finally:
        if own:
            shutil.rmtree(home, ignore_errors=True)


class AppProcess:
    """Starts the GUI binary, lets it run, and shuts it down; collects output and the exit code."""

    def __init__(self, args=(), home=None, platform="offscreen", env_extra=None):
        ensure_app_built()
        self.own_home = home is None
        self.home = home or tempfile.mkdtemp(prefix="makdown-home-")
        env = gui_env(self.home, env_extra)
        env["QT_QPA_PLATFORM"] = platform
        self.proc = subprocess.Popen([str(APP), *args], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                     errors="replace")

    def alive(self) -> bool:
        return self.proc.poll() is None

    def cpu_busy(self, seconds=1.5) -> float:
        """Fraction of one core used over the next `seconds` (a frozen UI thread pegs ~1.0, an idle one ~0)."""
        import time

        def ticks():
            try:
                f = Path(f"/proc/{self.proc.pid}/stat").read_text().rsplit(")", 1)[1].split()
                return int(f[11]) + int(f[12])
            except Exception:
                return None
        a = ticks()
        time.sleep(seconds)
        b = ticks()
        if a is None or b is None:
            return 0.0
        return (b - a) / os.sysconf("SC_CLK_TCK") / seconds

    def stop(self, grace=5):
        """SIGTERM (Qt apps die immediately), returns (returncode, stdout, stderr)."""
        if self.alive():
            self.proc.terminate()
            try:
                self.proc.wait(grace)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        out, err = self.proc.communicate()
        if self.own_home:
            shutil.rmtree(self.home, ignore_errors=True)
        return self.proc.returncode, out, err


# ------------------------------------------------------------------------------------------------
# misc parsing helpers used by the static tests
# ------------------------------------------------------------------------------------------------
def make_fragment_objs(fragment: Path) -> tuple[list[str], list[str]]:
    """Parses `obj-y += a.o b.o` / `moc-y += x y` lines of a kbuild-style Makefile fragment."""
    objs, mocs = [], []
    for line in read(fragment).splitlines():
        m = re.match(r"\s*(obj-y|moc-y)\s*\+?=\s*(.*)", line)
        if m:
            (objs if m.group(1) == "obj-y" else mocs).extend(m.group(2).split())
    return objs, mocs


def github_slug(heading: str, used: dict) -> str:
    t = re.sub(r"[`*_~]", "", heading).strip().lower()
    t = re.sub(r"[^\w\- ]", "", t, flags=re.UNICODE).replace(" ", "-")
    n = used.get(t, 0)
    used[t] = n + 1
    return t if n == 0 else f"{t}-{n}"


def markdown_headings(text: str) -> list[str]:
    out, fenced = [], False
    for line in text.splitlines():
        if re.match(r"\s*(```|~~~)", line):
            fenced = not fenced
            continue
        if not fenced:
            m = re.match(r"#{1,6}\s+(.*?)\s*#*\s*$", line)
            if m:
                out.append(m.group(1))
    return out


def strip_jsonc(text: str) -> str:
    """Python twin of ThemeStore::stripJsonComments (used to read repo files without the C++ code)."""
    out, i, in_str = [], 0, False
    while i < len(text):
        c = text[i]
        if in_str:
            out.append(c)
            if c == "\\" and i + 1 < len(text):
                i += 1
                out.append(text[i])
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
            out.append(c)
        elif text.startswith("//", i):
            while i < len(text) and text[i] != "\n":
                i += 1
            out.append("\n")
            continue
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = len(text) if j < 0 else j + 2
            continue
        else:
            out.append(c)
        i += 1
    return re.sub(r",(\s*[}\]])", r"\1", "".join(out))
