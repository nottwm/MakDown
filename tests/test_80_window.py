# SPDX-License-Identifier: MIT
"""The whole MainWindow (editor + live preview + tabs) constructed in-process, offscreen, via the harness."""
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lib.common import FIXTURES, Harness


class WindowCase(unittest.TestCase):
    def setUp(self):
        self.h = Harness(timeout=90)
        self.tmp = tempfile.mkdtemp(prefix="mdwin-")

    def tearDown(self):
        self.h.close()
        import shutil
        shutil.rmtree(self.tmp, ignore_errors=True)

    def call(self, cmd, **kw):
        r = self.h.call(cmd, **kw)
        self.dialogs = r.get("dialogs", [])
        bad = [w for w in r.get("warnings", []) if "propagateSizeHints" not in w and "window opacity" not in w]
        self.assertEqual(bad, [], f"Qt warnings during {cmd}: {bad}")
        return r

    def write(self, name, text):
        p = Path(self.tmp) / name
        p.write_text(text, encoding="utf-8")
        return str(p)


class TestOpenFiles(WindowCase):
    def test_empty_window(self):
        r = self.call("win_open", file="")
        self.assertTrue(r["visible"] and r["hasEditor"])
        self.assertIn("MakDown", r["title"])

    def test_opens_file_shows_text_and_renders_preview(self):
        f = self.write("a.md", "# Hello\n\nSome **bold** text\n")
        r = self.call("win_open", file=f)
        self.assertEqual(r["text"], "# Hello\n\nSome **bold** text\n")
        self.assertIn("a.md", r["title"])
        self.assertTrue(any("Hello" in p and "bold" in p for p in r["previews"]), "preview pane did not render the file")
        self.assertFalse(any("**" in p for p in r["previews"]), "preview must show rendered text, not raw markers")

    def test_kitchen_sink_renders_every_section(self):
        r = self.call("win_open", file=str(FIXTURES / "kitchen-sink.md"), wait=1000)
        joined = "\n".join(r["previews"])
        for word in ("kitchen sink", "nested bullet", "open task", "nested quote", "std::cout", "Center", "[missing image: missing image]",
                     "[image: remote]", "Duplicate"):
            self.assertIn(word, joined)

    def test_unicode_file_roundtrip(self):
        f = self.write("u.md", "# Zażółć 日本語 😀\n")
        r = self.call("win_open", file=f)
        self.assertEqual(r["text"], "# Zażółć 日本語 😀\n")
        self.assertTrue(any("Zażółć 日本語 😀" in p for p in r["previews"]))

    def test_crlf_file(self):
        f = self.write("c.md", "# a\r\n\r\ntext\r\n")
        r = self.call("win_open", file=f)
        self.assertTrue(any("text" in p for p in r["previews"]))

    def test_non_existing_file_still_opens_window(self):
        r = self.call("win_open", file=str(Path(self.tmp) / "new.md"))
        self.assertTrue(r["visible"])

    def test_empty_file(self):
        r = self.call("win_open", file=self.write("e.md", ""))
        self.assertEqual(r["text"], "")

    def test_big_file_opens(self):
        f = self.write("big.md", "# T\n\n" + ("A line with **bold** and `code`.\n\n" * 20000))
        r = self.call("win_open", file=f, wait=2500)
        self.assertTrue(r["visible"])
        self.assertEqual(len(r["text"]), len("# T\n\n" + "A line with **bold** and `code`.\n\n" * 20000))

    def test_binary_file_asks_before_opening(self):
        """A file with NUL / control bytes must trigger the 'Binary file' prompt instead of being dumped into the editor."""
        p = Path(self.tmp) / "bin.md"
        p.write_bytes(bytes(range(256)) * 1000)
        r = self.call("win_open", file=str(p))
        self.assertTrue(r["visible"])
        self.assertIn("Binary file", r["dialogs"])
        self.assertTrue(r["text"].startswith("# Welcome"), "cancelling must leave the normal welcome document, not binary junk")
        self.assertNotIn("\x00", r["text"])

    def test_text_with_unusual_but_valid_bytes_does_not_prompt(self):
        r = self.call("win_open", file=self.write("ok.md", "tab\there \x1b[0m form\x0cfeed zażółć\n"))
        self.assertEqual(r["dialogs"], [])

    def test_directory_path_does_not_crash(self):
        self.assertTrue(self.call("win_open", file=self.tmp)["visible"])

    def test_native_frame_variant(self):
        r = self.call("win_open", file=self.write("n.md", "# x"), nativeFrame=True)
        self.assertTrue(r["visible"])

    def test_window_renders_pixels(self):
        self.call("win_open", file=str(FIXTURES / "kitchen-sink.md"), wait=800)
        g = self.call("win_grab")
        self.assertGreater(g["w"], 600)
        self.assertGreater(g["distinctColors"], 8, "window looks blank / un-themed")

    def test_open_close_cycle_many_times(self):
        f = self.write("loop.md", "# loop\n- a\n")
        for _ in range(12):
            self.call("win_open", file=f, wait=150)
        self.call("win_close")


class TestLivePreview(WindowCase):
    def test_typing_updates_preview(self):
        self.call("win_open", file=self.write("l.md", "# one\n"))
        r = self.call("win_type", text="# two\n\nparagraph **x**\n")
        self.assertTrue(any("two" in p and "paragraph x" in p.replace("**", "") for p in r["previews"]))
        self.assertFalse(any("one" in p for p in r["previews"]))

    def test_preview_follows_rapid_edits(self):
        self.call("win_open", file=self.write("l.md", "x"))
        for i in range(30):
            r = self.call("win_type", text=f"# rev {i}\n", wait=30)
        r = self.call("win_type", text="# final\n", wait=900)
        self.assertTrue(any("final" in p for p in r["previews"]))

    def test_modified_marker_in_title(self):
        r0 = self.call("win_open", file=self.write("m.md", "a"))
        r = self.call("win_type", text="a changed")
        self.assertNotEqual(r["title"], r0["title"], "title should flag unsaved changes")
        self.assertIn(" *", r["title"], "no unsaved-changes indicator in window title")

    def test_hostile_markdown_in_live_window(self):
        self.call("win_open", file="")
        for md in ("*" * 30000, "[" * 5000, "`" * 9999, "> " * 12 + "x", "| a |\n|--|\n" + "| b |\n" * 5000):
            self.call("win_type", text=md, wait=500)

    @unittest.expectedFailure
    def test_KNOWN_ISSUE_deeply_nested_blockquotes_freeze_the_preview(self):
        """Each blockquote level becomes a nested <table>; QTextDocument layout time roughly quadruples per level:
        depth 14 = instant, 16 = ~2 s, 20 = never finishes (UI thread frozen). A pasted e-mail thread or hostile .md can
        trigger it. Fix idea: cap nesting (render deeper levels with a flat left rule instead of a table)."""
        self.h.timeout = 12
        self.call("win_open", file="", wait=100)
        self.call("win_type", text="> " * 20 + "x", wait=800)


class TestWindowWithVim(WindowCase):
    def test_vim_setting_is_honoured_on_startup(self):
        data = Path(self.h.home) / ".local/share/MakDown"
        data.mkdir(parents=True)
        (data / "settings.jsonc").write_text('{"vim": true}')
        self.call("win_open", file=self.write("v.md", "abc"))
        r = self.call("win_keys", keys=["x"])
        self.assertEqual(r["text"], "bc", "vim mode from settings should make 'x' delete, not type")

    def test_typing_without_vim(self):
        self.call("win_open", file=self.write("v.md", "abc"))
        r = self.call("win_keys", keys=["x"])
        self.assertEqual(r["text"], "xabc")


class TestSettingsAppliedAtStartup(WindowCase):
    def test_every_theme_and_view_mode(self):
        data = Path(self.h.home) / ".local/share/MakDown"
        data.mkdir(parents=True)
        for theme in ("GitHub", "TTY", "Glass Dark", "Warm Light", "Windows 11 Dark"):
            for view in (0, 1, 2, 3):
                (data / "settings.jsonc").write_text(json.dumps({"theme": theme, "viewMode": view, "animations": 0}))
                r = self.call("win_open", file=str(FIXTURES / "kitchen-sink.md"), wait=250)
                self.assertTrue(r["visible"], f"{theme}/view {view}")

    def test_unknown_theme_falls_back(self):
        data = Path(self.h.home) / ".local/share/MakDown"
        data.mkdir(parents=True)
        (data / "settings.jsonc").write_text('{"theme": "Does Not Exist"}')
        self.assertTrue(self.call("win_open", file="")["visible"])

    def test_last_file_that_vanished(self):
        data = Path(self.h.home) / ".local/share/MakDown"
        data.mkdir(parents=True)
        (data / "settings.jsonc").write_text(json.dumps({"lastFile": "/gone/away.md", "folder": "/gone"}))
        self.assertTrue(self.call("win_open", file="")["visible"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
