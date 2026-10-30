# SPDX-License-Identifier: MIT
"""The real compiled binary: command line flags and a headless start-up smoke test (QT_QPA_PLATFORM=offscreen)."""
import os
import re
import shutil
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lib.common import APP, FIXTURES, ROOT, AppProcess, ensure_app_built, read, run, run_app, which

# Messages the offscreen platform plugin always prints; they say nothing about MakDown's health.
NOISE = re.compile(r"(XDG_RUNTIME_DIR|propagateSizeHints|setting window opacity|QStandardPaths|offscreen|Could not load the Qt platform plugin"
                   r"|fontconfig|Fontconfig|libEGL|MESA|qt\.qpa\.|setGeometry|QFont::|Wayland|wayland)", re.I)


def meaningful(stderr: str):
    return [l for l in stderr.splitlines() if l.strip() and not NOISE.search(l)]


class TestCommandLine(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        ensure_app_built()

    def test_binary_exists_and_is_executable(self):
        self.assertTrue(APP.exists() and os.access(APP, os.X_OK))

    def test_version(self):
        p = run_app(["--version"])
        self.assertEqual(p.returncode, 0)
        self.assertRegex(p.stdout.strip(), r"^MakDown \d+\.\d+(\.\d+)?$")
        self.assertEqual(meaningful(p.stderr), [])

    def test_help_flags_exit_zero_and_document_every_flag(self):
        for flag in ("--help", "-h"):
            p = run_app([flag])
            self.assertEqual(p.returncode, 0, flag)
            for needle in ("usage:", "--debug", "--native-frame", "--version", "file.md"):
                self.assertIn(needle, p.stdout, f"{flag}: help text lacks {needle}")

    def test_version_matches_source(self):
        v = re.search(r'puts\("MakDown ([^"]+)"\)', read(ROOT / "init/main.cpp")).group(1)
        self.assertEqual(run_app(["--version"]).stdout.strip(), f"MakDown {v}")

    def test_info_flags_do_not_create_user_data(self):
        home = tempfile.mkdtemp()
        try:
            run_app(["--version"], home=home)
            run_app(["--help"], home=home)
            self.assertEqual(os.listdir(home), [], "--version/--help must not write anything to $HOME")
        finally:
            shutil.rmtree(home, ignore_errors=True)

    def test_info_flags_work_without_a_display(self):
        env = {"QT_QPA_PLATFORM": "xcb", "DISPLAY": "", "WAYLAND_DISPLAY": ""}
        p = run_app(["--version"], env_extra=env, platform="xcb")
        self.assertEqual(p.returncode, 0, "--version should not need a display")

    def test_debug_flag_prints_build_banner(self):
        a = AppProcess(["--debug"])
        time.sleep(2.5)
        self.assertTrue(a.alive(), "app died with --debug")
        rc, out, err = a.stop()
        text = out + err
        for needle in ("MakDown build information", "Compiler", "Qt (runtime)"):
            self.assertIn(needle, text)


class TestStartup(unittest.TestCase):
    """Start the GUI for real, headless, and make sure it stays up and says nothing alarming."""

    @classmethod
    def setUpClass(cls):
        ensure_app_built()

    def run_for(self, args, seconds=3.0, home=None):
        a = AppProcess(args, home=home)
        time.sleep(seconds)
        alive = a.alive()
        busy = a.cpu_busy(1.5) if alive else 0.0
        rc, out, err = a.stop()
        self.last_busy = busy
        return alive, rc, out, err, a.home

    def assertHealthy(self, alive, rc, err, what):
        self.assertTrue(alive, f"{what}: application exited by itself (rc={rc}); stderr:\n{err[-1500:]}")
        self.assertLess(self.last_busy, 0.6, f"{what}: still burning {self.last_busy:.0%} CPU several seconds after start - UI thread frozen or busy-looping")
        bad = [l for l in meaningful(err) if re.search(r"(Qt: |QObject::|QWidget::|QPainter::|Segmentation|terminate|assert|ASSERT|"
                                                      r"AddressSanitizer|runtime error|QSocketNotifier|Cannot|Unable|Failed|failed)", l)]
        self.assertEqual(bad, [], f"{what}: warnings on stderr")

    def test_starts_with_no_arguments_on_a_fresh_home(self):
        alive, rc, out, err, home = self.run_for([])
        self.assertHealthy(alive, rc, err, "empty start")

    def test_first_run_creates_presets_and_syntax_packs(self):
        home = tempfile.mkdtemp()
        try:
            alive, rc, out, err, _ = self.run_for([], home=home)
            self.assertTrue(alive)
            data = Path(home) / ".local/share/MakDown"
            self.assertEqual(len(list((data / "presets").glob("*.jsonc"))), 15)
            self.assertEqual(len(list((data / "syntax").glob("*.json"))), 11)
        finally:
            shutil.rmtree(home, ignore_errors=True)

    def test_opens_a_markdown_file(self):
        alive, rc, out, err, _ = self.run_for([str(FIXTURES / "kitchen-sink.md")])
        self.assertHealthy(alive, rc, err, "kitchen-sink.md")

    def test_native_frame_flag(self):
        alive, rc, out, err, _ = self.run_for(["--native-frame"])
        self.assertHealthy(alive, rc, err, "--native-frame")

    def test_missing_file_argument_does_not_crash(self):
        alive, rc, out, err, _ = self.run_for(["/nonexistent/dir/nope.md"])
        self.assertHealthy(alive, rc, err, "nonexistent file")

    def test_directory_as_argument_does_not_crash(self):
        alive, rc, out, err, _ = self.run_for([str(ROOT / "Documentation")])
        self.assertHealthy(alive, rc, err, "directory argument")

    def test_hostile_files_do_not_crash(self):
        d = Path(tempfile.mkdtemp())
        try:
            (d / "empty.md").write_bytes(b"")
            (d / "binary.md").write_bytes(bytes(range(256)) * 400)
            (d / "bom.md").write_bytes(b"\xef\xbb\xbf# BOM heading\n")
            (d / "latin1.md").write_bytes("# zażółć".encode("cp1250"))
            (d / "crlf.md").write_bytes(b"# a\r\n\r\n- b\r\n")
            (d / "long.md").write_text("word " * 400000)
            (d / "deep.md").write_text("> " * 10 + "x\n" + "".join("  " * i + "- x\n" for i in range(120)))
            (d / "space name 日本 😀.md").write_text("# ok\n")
            for f in sorted(d.iterdir()):
                alive, rc, out, err, _ = self.run_for([str(f)], seconds=12.0 if f.name == "long.md" else 3.0)   # a 2 MB single line legitimately needs a while
                self.assertHealthy(alive, rc, err, f.name)
        finally:
            shutil.rmtree(d, ignore_errors=True)

    def test_survives_corrupt_user_data(self):
        home = tempfile.mkdtemp()
        try:
            data = Path(home) / ".local/share/MakDown"
            (data / "presets").mkdir(parents=True)
            (data / "syntax").mkdir(parents=True)
            (data / "settings.jsonc").write_text("{{{ not json \x00")
            (data / "presets" / "junk.jsonc").write_text("][")
            (data / "syntax" / "junk.json").write_text("nope")
            alive, rc, out, err, _ = self.run_for([], home=home)
            self.assertHealthy(alive, rc, err, "corrupt settings/presets/packs")
        finally:
            shutil.rmtree(home, ignore_errors=True)

    def test_survives_read_only_home(self):
        if os.geteuid() == 0:
            self.skipTest("root ignores file permissions")
        home = tempfile.mkdtemp()
        try:
            os.chmod(home, 0o500)
            alive, rc, out, err, _ = self.run_for([], home=home)
            self.assertHealthy(alive, rc, err, "read-only $HOME")
        finally:
            os.chmod(home, 0o700)
            shutil.rmtree(home, ignore_errors=True)

    def test_every_stock_theme_starts_cleanly(self):
        """Pre-select each theme through settings.jsonc and boot the real app with it."""
        for theme in ("GitHub Dark", "TTY", "Glass Light", "VS Code Dark", "Catppuccin", "Windows 11"):
            home = tempfile.mkdtemp()
            try:
                data = Path(home) / ".local/share/MakDown"
                data.mkdir(parents=True)
                (data / "settings.jsonc").write_text('{"theme": "%s", "vim": true}' % theme)
                alive, rc, out, err, _ = self.run_for([str(FIXTURES / "kitchen-sink.md")], seconds=2.0, home=home)
                self.assertHealthy(alive, rc, err, f"theme {theme}")
            finally:
                shutil.rmtree(home, ignore_errors=True)

    @unittest.skipUnless(which("xvfb-run") and os.environ.get("MAKDOWN_TEST_X11"), "opt-in: set MAKDOWN_TEST_X11=1 (needs xvfb-run)")
    def test_starts_on_a_real_x_server(self):
        env = dict(os.environ, HOME=tempfile.mkdtemp())
        env.pop("QT_QPA_PLATFORM", None)
        import subprocess
        p = subprocess.Popen(["xvfb-run", "-a", str(APP), str(FIXTURES / "kitchen-sink.md")], env=env, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True)
        time.sleep(5)
        alive = p.poll() is None
        p.terminate()
        try:
            p.wait(5)
        except Exception:
            p.kill()
        out, err = p.communicate()
        if not alive and "could not connect" in err.lower() or "xcb" in err.lower() and "plugin" in err.lower():
            self.skipTest("xcb platform plugin unavailable in this environment: " + err[-200:])
        self.assertTrue(alive, f"app exited under Xvfb: {err[-1000:]}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
