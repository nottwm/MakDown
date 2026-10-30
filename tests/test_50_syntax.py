# SPDX-License-Identifier: MIT
"""Syntax-highlighting packs: stock packs, parser, on-disk lifecycle, file matching and the highlighter itself."""
import json
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lib.common import ROOT, Harness, HarnessCase, read, strip_jsonc

SCOPES = {"keyword", "type", "constant", "builtin", "function", "variable", "string", "escape", "number", "comment",
          "preprocessor", "attribute", "macro", "label"}
EXPECTED_STOCK = {"C", "C++", "Makefile", "Linker script", "Rust", "Bash", "Batch", "HTML / XML", "Python", "CSS",
                  "JavaScript / TypeScript"}


class TestStockPacks(HarnessCase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.stock = cls.h.call("stock_syntax_json")["packs"]

    def test_all_documented_languages_ship(self):
        self.assertEqual({json.loads(strip_jsonc(p["json"]))["name"] for p in self.stock}, EXPECTED_STOCK)

    def test_every_stock_pack_is_valid_json_and_parses_cleanly(self):
        for p in self.stock:
            json.loads(strip_jsonc(p["json"]))
            r = self.call("syntax_parse", json=p["json"])
            self.assertFalse(r["null"], p["file"])
            self.assertEqual(r["error"], "", f"{p['file']} has broken regexes / problems: {r['error']}")

    def test_every_stock_pack_is_complete(self):
        for p in self.stock:
            j = json.loads(strip_jsonc(p["json"]))
            self.assertTrue(j.get("builtin"), f"{p['file']} must be builtin:true (so it can be refreshed)")
            self.assertGreaterEqual(j.get("revision", 0), 1, p["file"])
            self.assertTrue(j.get("extensions") or j.get("filenames"), p["file"])
            self.assertTrue(p["file"].endswith(".json"))
            rules = len(j.get("rules", [])) + len(j.get("regions", [])) + sum(len(v) for v in j.get("words", {}).values())
            self.assertGreater(rules, 3, f"{p['file']} highlights almost nothing")

    def test_only_documented_scopes_are_used(self):
        for p in self.stock:
            j = json.loads(strip_jsonc(p["json"]))
            used = {r["scope"] for r in j.get("rules", []) + j.get("regions", [])} | set(j.get("words", {}))
            self.assertEqual(used - SCOPES, set(), f"{p['file']} uses undocumented scopes")

    def test_no_extension_claimed_by_two_packs(self):
        owner = {}
        for p in self.stock:
            j = json.loads(strip_jsonc(p["json"]))
            for e in j.get("extensions", []):
                self.assertNotIn(e.lower(), owner, f".{e} claimed by both {owner.get(e.lower())} and {j['name']}")
                owner[e.lower()] = j["name"]

    def test_file_names_match_pack_names(self):
        for p in self.stock:
            j = json.loads(strip_jsonc(p["json"]))
            self.assertRegex(p["file"], r"^[a-z0-9_\-]+\.json$")

    def test_regions_have_terminators_or_are_single_line(self):
        for p in self.stock:
            j = json.loads(strip_jsonc(p["json"]))
            for r in j.get("regions", []):
                self.assertTrue(r.get("begin"), f"{p['file']}: region without begin")
                if r.get("multiline", True):
                    self.assertTrue(r.get("end"), f"{p['file']}: multiline region {r} has no end")

    def test_docs_and_stock_agree_on_extensions(self):
        doc = read(ROOT / "Documentation/syntax.md")
        for p in self.stock:
            j = json.loads(strip_jsonc(p["json"]))
            for e in j.get("extensions", [])[:1]:       # the docs abbreviate with "...", but the first extension must be there
                self.assertIn(f"`.{e}`", doc, f".{e} of {j['name']} should be listed in Documentation/syntax.md")


class TestParser(HarnessCase):
    def parse(self, obj):
        return self.call("syntax_parse", json=obj if isinstance(obj, str) else json.dumps(obj))

    def test_minimal_valid_pack(self):
        r = self.parse({"name": "X", "extensions": ["x"]})
        self.assertFalse(r["null"])
        self.assertEqual(r["error"], "")
        self.assertEqual(r["extensions"], ["x"])

    def test_invalid_json(self):
        for bad in ("", "{", "[]", "not json", '{"a":}', "null", "12"):
            r = self.parse(bad)
            self.assertTrue(r["null"], bad)
            self.assertIn("invalid JSON", r["error"])

    def test_comments_and_trailing_commas_tolerated(self):
        r = self.parse('{ // comment\n "name": "X", /* block */ "extensions": ["x",], }')
        self.assertFalse(r["null"])
        self.assertEqual(r["error"], "")

    def test_regex_with_comma_brace_survives_comma_stripping(self):
        r = self.parse(r'{"name":"X","extensions":["x"],"rules":[{"scope":"number","pattern":"\\d{2,}"}]}')
        self.assertEqual(r["rules"], 1)
        self.assertEqual(r["error"], "")

    def test_slashes_inside_strings_are_not_comments(self):
        r = self.parse(r'{"name":"X","extensions":["x"],"rules":[{"scope":"comment","pattern":"//.*"}]}')
        self.assertEqual(r["rules"], 1)

    def test_no_extensions_or_filenames_flagged(self):
        r = self.parse({"name": "X"})
        self.assertIn("extensions", r["error"])

    def test_extension_normalisation(self):
        r = self.parse({"name": "X", "extensions": [".PY", "Rs"], "filenames": ["Makefile", "GNUmakefile"]})
        self.assertEqual(r["extensions"], ["py", "rs"])
        self.assertEqual(r["filenames"], ["makefile", "gnumakefile"])

    def test_broken_regex_is_reported_but_pack_survives(self):
        r = self.parse({"name": "X", "extensions": ["x"],
                        "rules": [{"scope": "string", "pattern": "("}, {"scope": "number", "pattern": "\\d+"}],
                        "regions": [{"scope": "comment", "begin": "[", "end": "x"}]})
        self.assertFalse(r["null"])
        self.assertEqual(r["rules"], 1, "only the valid rule should be kept")
        self.assertEqual(r["regions"], 0)
        self.assertIn("rule string", r["error"])
        self.assertIn("region begin", r["error"])

    def test_empty_patterns_are_skipped(self):
        r = self.parse({"name": "X", "extensions": ["x"], "rules": [{"scope": "a", "pattern": ""}], "regions": [{"scope": "c", "begin": "", "end": "x"}]})
        self.assertEqual((r["rules"], r["regions"]), (0, 0))

    def test_words_become_one_rule_per_scope(self):
        r = self.parse({"name": "X", "extensions": ["x"], "words": {"keyword": ["if", "else"], "type": ["int"], "empty": []}})
        self.assertEqual(r["rules"], 2)

    def test_words_are_escaped(self):
        r = self.parse({"name": "X", "extensions": ["x"], "words": {"keyword": ["a+b", "(", "c.d"]}})
        self.assertEqual(r["error"], "", "special characters in 'words' must be escaped for the user")

    def test_wrong_types_do_not_crash(self):
        for obj in ({"name": 5, "extensions": "x"}, {"extensions": [1, None, {}], "rules": "no", "regions": 7, "words": []},
                    {"name": "X", "extensions": ["x"], "rules": [1, "a", None, {"scope": 1, "pattern": 2}]},
                    {"name": "X", "extensions": ["x"], "colors": {"keyword": 5}}):
            self.parse(obj)

    def test_revision_and_builtin_flags(self):
        r = self.parse({"name": "X", "extensions": ["x"], "revision": 7, "builtin": True})
        self.assertEqual((r["revision"], r["builtin"]), (7, True))
        r = self.parse({"name": "X", "extensions": ["x"]})
        self.assertEqual((r["revision"], r["builtin"]), (0, False))

    def test_case_insensitive_pack(self):
        r = self.call("highlight_pack", json=json.dumps({"name": "B", "extensions": ["b"], "caseSensitive": False,
                                                          "words": {"keyword": ["echo"]}}), text="ECHO echo Echo")
        self.assertEqual(len(r["lines"][0]), 3)


class TestLifecycleOnDisk(unittest.TestCase):
    def setUp(self):
        self.h = Harness()
        self.dir = Path(self.h.home) / ".local/share/MakDown/syntax"

    def tearDown(self):
        self.h.close()

    def call(self, cmd, **kw):
        r = self.h.call(cmd, **kw)
        self.assertEqual(r["warnings"], [], r["warnings"])
        return r

    def test_first_launch_writes_all_stock_packs(self):
        self.call("syntax_ensure")
        files = {p.name for p in self.dir.glob("*.json")}
        stock = {p["file"] for p in self.call("stock_syntax_json")["packs"]}
        self.assertEqual(files, stock)

    def test_load_all_returns_every_pack(self):
        self.call("syntax_ensure")
        names = {p["name"] for p in self.call("syntax_load_all")["packs"]}
        self.assertEqual(names, EXPECTED_STOCK)

    def test_ensure_is_idempotent_and_keeps_user_files(self):
        self.call("syntax_ensure")
        custom = self.dir / "mine.json"
        custom.write_text(json.dumps({"name": "Mine", "extensions": ["mine"]}))
        before = {p.name: p.read_bytes() for p in self.dir.glob("*.json")}
        self.call("syntax_ensure")
        after = {p.name: p.read_bytes() for p in self.dir.glob("*.json")}
        self.assertEqual(before, after)
        self.assertIn("Mine", {p["name"] for p in self.call("syntax_load_all")["packs"]})

    def test_missing_stock_pack_is_restored(self):
        self.call("syntax_ensure")
        (self.dir / "python.json").unlink()
        self.call("syntax_ensure")
        self.assertTrue((self.dir / "python.json").exists())

    def test_outdated_builtin_pack_is_refreshed(self):
        self.call("syntax_ensure")
        f = self.dir / "c.json"
        j = json.loads(f.read_text())
        j["revision"] = 0
        j["extensions"] = ["old"]
        f.write_text(json.dumps(j))
        self.call("syntax_ensure")
        self.assertIn("c", json.loads(f.read_text())["extensions"], "outdated builtin pack should be rewritten")

    def test_user_owned_pack_is_never_overwritten(self):
        self.call("syntax_ensure")
        f = self.dir / "c.json"
        j = json.loads(f.read_text())
        j["revision"] = 0
        j["builtin"] = False
        j["extensions"] = ["mine"]
        f.write_text(json.dumps(j))
        self.call("syntax_ensure")
        self.assertEqual(json.loads(f.read_text())["extensions"], ["mine"], "builtin:false means 'hands off'")

    def test_current_revision_pack_is_not_touched(self):
        self.call("syntax_ensure")
        f = self.dir / "rust.json"
        f.write_text(f.read_text() + "\n// my note\n")
        self.call("syntax_ensure")
        self.assertIn("my note", f.read_text())

    def test_broken_files_are_skipped_not_fatal(self):
        self.call("syntax_ensure")
        (self.dir / "broken.json").write_text("{ this is not json")
        (self.dir / "empty.json").write_text("")
        (self.dir / "noext.json").write_text('{"name":"NoExt"}')
        (self.dir / "notes.txt").write_text("ignored")
        names = {p["name"] for p in self.call("syntax_load_all")["packs"]}
        self.assertEqual(names, EXPECTED_STOCK)

    def test_jsonc_files_are_loaded(self):
        self.call("syntax_ensure")
        (self.dir / "extra.jsonc").write_text('// my language\n{"name":"Extra","extensions":["ex"],}')
        self.assertIn("Extra", {p["name"] for p in self.call("syntax_load_all")["packs"]})

    def test_unwritable_syntax_dir_does_not_crash(self):
        import os
        self.call("syntax_ensure")
        for p in self.dir.glob("*.json"):
            os.chmod(p, 0o444)
        os.chmod(self.dir, 0o555)
        try:
            self.call("syntax_ensure")
        finally:
            os.chmod(self.dir, 0o755)


class TestFileMatching(HarnessCase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.h.call("syntax_ensure")

    def pack(self, path):
        return self.call("syntax_for_file", path=path)["pack"]

    def test_extensions(self):
        cases = {"a.c": "C", "a.h": "C", "x.cpp": "C++", "x.hpp": "C++", "y.rs": "Rust", "z.py": "Python", "s.sh": "Bash",
                 "w.bat": "Batch", "w.CMD": "Batch", "p.html": "HTML / XML", "i.svg": "HTML / XML", "s.css": "CSS",
                 "a.js": "JavaScript / TypeScript", "a.tsx": "JavaScript / TypeScript", "l.ld": "Linker script",
                 "/some/dir/with.dots/file.PY": "Python"}
        for path, name in cases.items():
            self.assertEqual(self.pack(path), name, path)

    def test_exact_file_names_win_over_extension(self):
        self.assertEqual(self.pack("Makefile"), "Makefile")
        self.assertEqual(self.pack("/proj/GNUmakefile"), "Makefile")
        self.assertEqual(self.pack("makefile"), "Makefile")
        self.assertEqual(self.pack(".bashrc"), "Bash")
        self.assertEqual(self.pack("SConstruct"), "Python")

    def test_no_pack(self):
        for path in ("", "README.md", "notes.txt", "noextension", "archive.tar.gz", "photo.png"):
            self.assertEqual(self.pack(path), "", path)

    def test_markdown_never_gets_a_pack(self):
        self.assertEqual(self.pack("doc.md"), "")
        self.assertEqual(self.pack("doc.markdown"), "")


class TestHighlighter(HarnessCase):
    """Run packs over text with the real QSyntaxHighlighter and check what gets coloured."""

    def hl(self, pack, text):
        return self.call("highlight_pack", json=json.dumps(pack), text=text)["lines"]

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.stock = {json.loads(p["json"])["name"]: p["json"] for p in cls.h.call("stock_syntax_json")["packs"]}

    def stock_hl(self, name, text):
        return self.call("highlight_pack", json=self.stock[name], text=text)["lines"]

    def spans(self, lines, n=0):
        return [(f["start"], f["len"]) for f in lines[n]]

    def test_word_rule_colours_whole_words_only(self):
        pack = {"name": "T", "extensions": ["t"], "words": {"keyword": ["if"]}}
        lines = self.hl(pack, "if iffy xif if")
        self.assertEqual(self.spans(lines), [(0, 2), (12, 2)])

    def test_block_comment_spans_lines(self):
        pack = {"name": "T", "extensions": ["t"], "regions": [{"scope": "comment", "begin": "/\\*", "end": "\\*/"}]}
        lines = self.hl(pack, "a /* start\nmiddle\nend */ b")
        self.assertEqual(self.spans(lines, 0), [(2, 8)])
        self.assertEqual(self.spans(lines, 1), [(0, 6)])
        self.assertEqual(self.spans(lines, 2), [(0, 6)])

    def test_single_line_region_does_not_leak(self):
        pack = {"name": "T", "extensions": ["t"],
                "regions": [{"scope": "string", "begin": '"', "end": '"', "escape": "\\", "multiline": False}]}
        lines = self.hl(pack, 'x "open\nnext line')
        self.assertEqual(self.spans(lines, 1), [], "unterminated single-line string must not colour the next line")

    def test_escape_char_in_string(self):
        pack = {"name": "T", "extensions": ["t"],
                "regions": [{"scope": "string", "begin": '"', "end": '"', "escape": "\\", "multiline": False}]}
        lines = self.hl(pack, r'"a\"b" c')
        self.assertEqual(self.spans(lines), [(0, 6)])

    def test_keywords_inside_strings_and_comments_are_not_coloured_separately(self):
        pack = {"name": "T", "extensions": ["t"], "words": {"keyword": ["if"]},
                "regions": [{"scope": "string", "begin": '"', "end": '"', "multiline": False}]}
        lines = self.hl(pack, 'say "if x" if')
        self.assertEqual(self.spans(lines), [(4, 6), (11, 2)])

    def test_empty_matching_rule_cannot_hang(self):
        pack = {"name": "T", "extensions": ["t"], "rules": [{"scope": "x", "pattern": "a*"}, {"scope": "y", "pattern": "^"}]}
        self.hl(pack, "bbb aaa\n\nccc")

    def test_pack_with_no_rules(self):
        self.assertEqual(self.hl({"name": "T", "extensions": ["t"]}, "anything\ngoes"), [[], []])

    def test_c_pack(self):
        lines = self.stock_hl("C", '#include <stdio.h>\nint main(void) { /* hi */ return 0; } // end\nchar *s = "x\\n";')
        self.assertTrue(all(lines), "every line of this C snippet should have colour spans")

    def test_cpp_block_comment_multiline(self):
        lines = self.stock_hl("C++", "/* a\nb\nc */ int x;")
        self.assertTrue(lines[1], "middle line of a block comment must be coloured")
        self.assertTrue(lines[2])

    def test_python_triple_quoted_string(self):
        lines = self.stock_hl("Python", 'x = """doc\nstring\n"""\ny = 1')
        self.assertTrue(lines[1], "the middle of a triple-quoted string must be coloured")

    def test_python_keywords(self):
        lines = self.stock_hl("Python", "def f(x):\n    return None")
        self.assertTrue(lines[0] and lines[1])

    def test_every_stock_pack_survives_hostile_text(self):
        hostile = ("\"" * 500 + "'" * 500 + "/*" * 200 + "\\" * 300 + "`" * 100 + "\n" + "x" * 50000 + "\n"
                   + "<!--" * 100 + "\n" + "😀" * 500 + "\n" + "\x01\x02\x00".replace("\x00", "") * 50)
        for name in self.stock:
            self.stock_hl(name, hostile)

    def test_long_line_performance(self):
        import time
        t = time.time()
        for name in ("C++", "Python", "JavaScript / TypeScript", "HTML / XML"):
            self.stock_hl(name, ("int x = 1; // c\n" * 3000))
        self.assertLess(time.time() - t, 20)

    def test_colours_are_valid(self):
        for f in self.stock_hl("C++", "int main() { return 0; }")[0]:
            self.assertRegex(f["color"], r"^#[0-9a-f]{6}$")


if __name__ == "__main__":
    unittest.main(verbosity=2)
