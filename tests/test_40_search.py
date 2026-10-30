# SPDX-License-Identifier: MIT
"""core/search.cpp: regex builder, text search, fuzzy 'go to file', globs, binary sniffing, directory walker.

Behaviour is checked against what Documentation/search.md and the README promise.
"""
import base64
import os
import shutil
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lib.common import HarnessCase


class TestRegexBuilder(HarnessCase):
    def rx(self, query, subject=None, **o):
        kw = dict(query=query, **o)
        if subject is not None:
            kw["subject"] = subject
        return self.call("regex", **kw)

    def test_empty_query_is_invalid(self):
        self.assertFalse(self.rx("")["valid"])

    def test_plain_text_is_escaped(self):
        r = self.rx("a.b(c)[d]*", subject="a.b(c)[d]* axb(c)[d]", **{"case": True})
        self.assertEqual(r["matches"], ["a.b(c)[d]*"])

    def test_case_insensitive_by_default(self):
        self.assertEqual(self.rx("hello", subject="Hello HELLO hello")["matches"], ["Hello", "HELLO", "hello"])

    def test_match_case(self):
        self.assertEqual(self.rx("hello", subject="Hello HELLO hello", **{"case": True})["matches"], ["hello"])

    def test_whole_word(self):
        r = self.rx("cat", subject="cat concat cat_x cats cat.", word=True)
        self.assertEqual(r["matches"], ["cat", "cat"])

    def test_whole_word_with_regex_alternation(self):
        r = self.rx("cat|dog", subject="cat dogma dog", word=True, regex=True)
        self.assertEqual(r["matches"], ["cat", "dog"])

    def test_whole_word_unicode(self):
        r = self.rx("żółć", subject="żółć żółćx xżółć", word=True)
        self.assertEqual(r["matches"], ["żółć"])

    def test_regex_mode(self):
        r = self.rx(r"\d{2,3}", subject="1 22 333 4444", regex=True)
        self.assertEqual(r["matches"], ["22", "333", "444"])

    def test_invalid_regex_reports_error_not_crash(self):
        for bad in ("(", "[a-", "*abc", "(?<x", "\\"):
            r = self.rx(bad, regex=True)
            self.assertFalse(r["valid"], bad)
            self.assertTrue(r["error"], f"no error text for {bad!r}")

    def test_same_bad_text_is_fine_when_not_regex(self):
        self.assertTrue(self.rx("(", regex=False)["valid"])

    def test_unicode_case_folding(self):
        self.assertEqual(self.rx("ŁÓDŹ", subject="łódź")["matches"], ["łódź"])


class TestSearchText(HarnessCase):
    def search(self, text, query, **o):
        return self.call("search_text", text=text, query=query, **o)["hits"]

    def test_basic_positions(self):
        h = self.search("alpha\nbeta alpha\ngamma", "alpha")
        self.assertEqual([(x["line"], x["col"], x["len"]) for x in h], [(1, 0, 5), (2, 5, 5)])

    def test_multiple_hits_on_a_line(self):
        h = self.search("aa aa aa", "aa")
        self.assertEqual([x["col"] for x in h], [0, 3, 6])

    def test_overlapping_matches_are_not_double_counted(self):
        self.assertEqual(len(self.search("aaaa", "aa")), 2)

    def test_line_numbers_with_crlf(self):
        h = self.search("one\r\ntwo\r\nthree x\r\n", "x")
        self.assertEqual((h[0]["line"], h[0]["col"]), (3, 6))

    def test_crlf_does_not_leak_cr_into_dollar(self):
        h = self.search("abc\r\ndef\r\n", r"c$", regex=True)
        self.assertEqual(len(h), 1)

    def test_caret_and_dollar_are_per_line(self):
        h = self.search("foo\nbar\nfoo bar", r"^foo", regex=True)
        self.assertEqual([x["line"] for x in h], [1, 3])
        h = self.search("foo\nbar\nfoo bar", r"bar$", regex=True)
        self.assertEqual([x["line"] for x in h], [2, 3])

    def test_cannot_match_across_lines(self):
        self.assertEqual(self.search("ab\ncd", r"b\ncd", regex=True), [])

    def test_zero_length_matches_ignored(self):
        self.assertEqual(self.search("abc", r"x*", regex=True), [])
        self.assertEqual(self.search("abc", r"^", regex=True), [])

    def test_max_hits_respected(self):
        text = "x\n" * 100
        self.assertEqual(len(self.search(text, "x", max=7)), 7)

    def test_5000_cap_scenario(self):
        text = "hit\n" * 6000
        self.assertEqual(len(self.search(text, "hit", max=5000)), 5000)

    def test_preview_is_trimmed_and_marks_match(self):
        h = self.search("    indented match here", "match")[0]
        self.assertEqual(h["preview"][h["pcol"]:h["pcol"] + h["plen"]], "match")
        self.assertFalse(h["preview"].startswith(" "))

    def test_long_line_preview_is_shortened_around_match(self):
        line = "x" * 500 + "NEEDLE" + "y" * 500
        h = self.search(line, "NEEDLE")[0]
        self.assertLessEqual(len(h["preview"]), 225)
        self.assertEqual(h["preview"][h["pcol"]:h["pcol"] + h["plen"]], "NEEDLE")
        self.assertEqual(h["col"], 500, "col must refer to the FULL line, not the preview")
        self.assertTrue(h["preview"].startswith("\u2026"), "elided start should be marked with an ellipsis")

    def test_columns_are_utf16_units_like_the_editor(self):
        h = self.search("😀😀 target", "target")[0]
        self.assertEqual(h["col"], 5, "two emoji = 4 UTF-16 units + space")

    def test_empty_text_and_no_trailing_newline(self):
        self.assertEqual(self.search("", "a"), [])
        self.assertEqual(len(self.search("a", "a")), 1)

    def test_blank_lines_are_counted(self):
        h = self.search("\n\n\nfound", "found")
        self.assertEqual(h[0]["line"], 4)

    def test_whole_word_and_case_options(self):
        h = self.search("Foo food foo", "foo", word=True, **{"case": True})
        self.assertEqual([x["col"] for x in h], [9])

    def test_regex_catastrophic_pattern_on_long_line_terminates(self):
        t = time.time()
        self.search("a" * 28 + "b", r"(a+)+$", regex=True)
        self.assertLess(time.time() - t, 20)


class TestFuzzy(HarnessCase):
    def score(self, path, query):
        return self.call("fuzzy", path=path, query=query)["score"]

    def test_empty_query_scores_zero(self):
        self.assertEqual(self.score("a/b.cpp", ""), 0)

    def test_non_subsequence_is_rejected(self):
        self.assertEqual(self.score("src/main.cpp", "xyz"), -1)
        self.assertEqual(self.score("abc", "abcd"), -1)
        self.assertEqual(self.score("ab", "ba"), -1)

    def test_subsequence_matches(self):
        self.assertGreater(self.score("src/main.cpp", "smc"), -1)

    def test_case_insensitive(self):
        self.assertEqual(self.score("README.md", "readme"), self.score("README.md", "README"))

    def test_filename_match_beats_directory_match(self):
        self.assertGreater(self.score("docs/x/other.txt", "other"), self.score("other/x/docs.txt", "other") - 1)
        self.assertGreater(self.score("a/main.cpp", "main"), self.score("main/a.cpp", "main"))

    def test_prefix_of_filename_beats_middle(self):
        self.assertGreater(self.score("src/editor.cpp", "edit"), self.score("src/credit.cpp", "edit"))

    def test_consecutive_beats_scattered(self):
        self.assertGreater(self.score("x/find.cpp", "find"), self.score("x/f_i_n_d.cpp", "find"))

    def test_shorter_path_wins_ties(self):
        self.assertGreater(self.score("a.cpp", "a"), self.score("deeply/nested/folder/structure/a.cpp", "a"))

    def test_slash_in_query_anchors_to_directories(self):
        self.assertGreater(self.score("ui/editor.cpp", "ui/ed"), -1)
        self.assertEqual(self.score("core/editor.cpp", "ui/ed"), -1)

    def test_backslash_treated_like_slash(self):
        self.assertGreater(self.score("ui/editor.cpp", "ui\\ed"), -1)

    def test_ranking_scenario(self):
        files = ["include/makdown/ui/mainwindow.h", "ui/mainwindow.cpp", "Documentation/main-notes.md", "core/markdown.cpp",
                 "ui/mediatab.cpp"]
        ranked = sorted(files, key=lambda f: -self.score(f, "mainw"))
        self.assertIn("mainwindow", ranked[0])
        self.assertNotIn("markdown", " ".join(ranked[:2]))

    def test_unicode_paths(self):
        self.assertGreater(self.score("docs/żółć.md", "zo"), -2)  # must not crash; ascii query vs polish name
        self.assertGreater(self.score("docs/żółć.md", "żó"), -1)


class TestGlobs(HarnessCase):
    def split(self, spec):
        return self.call("globs", spec=spec, paths=[])["globs"]

    def match(self, spec, *paths):
        return self.call("globs", spec=spec, paths=list(paths))["match"]

    def test_split(self):
        self.assertEqual(self.split("*.md, src/**/*.cpp"), ["*.md", "src/**/*.cpp"])
        self.assertEqual(self.split(" ,, a ,"), ["a"])
        self.assertEqual(self.split(""), [])

    def test_trailing_slash_means_everything_below(self):
        self.assertEqual(self.split("docs/"), ["docs/**"])

    def test_name_patterns_ignore_directories(self):
        self.assertEqual(self.match("*.md", "README.md", "docs/a.md", "a/b/c/d.md", "x.txt", "docs/a.md.bak"),
                         [True, True, True, False, False])

    def test_path_patterns_are_anchored(self):
        self.assertEqual(self.match("src/*.cpp", "src/a.cpp", "src/sub/a.cpp", "x/src/a.cpp"), [True, False, False])

    def test_double_star(self):
        self.assertEqual(self.match("src/**/*.cpp", "src/a.cpp", "src/x/y/a.cpp", "other/a.cpp"), [True, True, False])
        self.assertEqual(self.match("src/**", "src/a", "src/x/y/z", "srcx/a"), [True, True, False])

    def test_directory_shorthand(self):
        self.assertEqual(self.match("docs/", "docs/a.md", "docs/x/y.md", "other/docs/a.md"), [True, True, False])

    def test_case_insensitive(self):
        self.assertEqual(self.match("*.MD", "readme.md"), [True])

    def test_question_mark_and_classes(self):
        self.assertEqual(self.match("?.c", "a.c", "ab.c"), [True, False])
        self.assertEqual(self.match("[ab].c", "a.c", "c.c"), [True, False])

    def test_multiple_patterns_are_or(self):
        self.assertEqual(self.match("*.md, *.txt", "a.md", "b.txt", "c.cpp"), [True, True, False])

    def test_special_regex_characters_are_literal(self):
        self.assertEqual(self.match("a+b(1).txt", "a+b(1).txt", "aab1.txt"), [True, False])

    def test_no_globs_never_matches(self):
        self.assertEqual(self.match("", "a"), [False])


class TestBinarySniffer(HarnessCase):
    def binary(self, data: bytes):
        return self.call("binary", b64=base64.b64encode(data).decode())["binary"]

    def test_text_is_not_binary(self):
        self.assertFalse(self.binary(b"hello\nworld\r\n\ttabbed\n"))
        self.assertFalse(self.binary("zażółć gęślą jaźń 日本語\n".encode()))
        self.assertFalse(self.binary(b""))

    def test_nul_byte_is_binary(self):
        self.assertTrue(self.binary(b"abc\x00def"))

    def test_control_heavy_is_binary(self):
        self.assertTrue(self.binary(bytes(range(1, 9)) * 20))

    def test_ansi_escape_and_formfeed_are_text(self):
        self.assertFalse(self.binary(b"\x1b[31mred\x1b[0m\n\x0c page"))

    def test_png_and_pdf_headers_are_binary(self):
        self.assertTrue(self.binary(b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR" + bytes(50)))
        self.assertTrue(self.binary(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\x00\x00"))


class TestSkippedDirs(HarnessCase):
    def test_skipped(self):
        for n in (".git", ".hidden", ".vscode", "node_modules", "__pycache__"):
            self.assertTrue(self.call("skipped_dir", name=n)["skipped"], n)
        for n in ("src", "docs", "node_modules2", "my.dir", "build"):
            self.assertFalse(self.call("skipped_dir", name=n)["skipped"], n)


class TestWalker(HarnessCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="mdwalk-")

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def mk(self, rel, data="x"):
        p = Path(self.root, rel)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(data)
        return p

    def walk(self, **kw):
        return self.call("walk", root=self.root, **kw)

    def test_lists_files_depth_first_sorted(self):
        for f in ("b.txt", "a.txt", "sub/z.txt", "sub/a.txt", "Zeta.txt"):
            self.mk(f)
        names = [f["path"] for f in self.walk()["files"]]
        self.assertEqual(names, ["a.txt", "b.txt", "Zeta.txt", "sub/a.txt", "sub/z.txt"])

    def test_skips_hidden_node_modules_pycache(self):
        for f in ("keep.txt", ".git/config", ".hidden/x", "node_modules/p/i.js", "__pycache__/m.pyc", "src/ok.c"):
            self.mk(f)
        names = {f["path"] for f in self.walk()["files"]}
        self.assertEqual(names, {"keep.txt", "src/ok.c"})

    def test_hidden_files_are_still_listed(self):
        self.mk(".gitignore")
        self.assertIn(".gitignore", {f["path"] for f in self.walk()["files"]})

    def test_symlinks_not_followed(self):
        self.mk("real/a.txt")
        os.symlink(Path(self.root, "real"), Path(self.root, "link"))
        os.symlink(self.root, Path(self.root, "real", "loop"))
        names = [f["path"] for f in self.walk()["files"]]
        self.assertEqual(names, ["real/a.txt"], "symlinked directories must be ignored (cycles!)")

    def test_max_files_cap(self):
        for i in range(30):
            self.mk(f"f{i:02}.txt")
        r = self.walk(max=10)
        self.assertEqual(r["count"], 10)
        self.assertEqual(len(r["files"]), 10)

    def test_reports_sizes(self):
        self.mk("a.txt", "12345")
        self.assertEqual(self.walk()["files"][0]["size"], 5)

    def test_empty_and_missing_root(self):
        self.assertEqual(self.walk()["count"], 0)
        self.assertEqual(self.call("walk", root=self.root + "/nope")["count"], 0)

    def test_many_files(self):
        for i in range(1500):
            self.mk(f"d{i % 30}/f{i}.txt")
        self.assertEqual(self.walk()["count"], 1500)

    def test_unicode_and_spaces_in_names(self):
        self.mk("zażółć gęślą/jaźń 日本.md")
        self.assertEqual([f["path"] for f in self.walk()["files"]], ["zażółć gęślą/jaźń 日本.md"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
