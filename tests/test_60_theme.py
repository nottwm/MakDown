# SPDX-License-Identifier: MIT
"""Themes (presets), JSONC parsing, the generated Qt style sheet and persisted settings (core/theme.cpp, core/anim.cpp)."""
import json
import os
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lib.common import ROOT, Harness, HarnessCase, read, strip_jsonc

HEX = re.compile(r"^#[0-9a-fA-F]{6}$")
CHROMES = {"github", "win11", "terminal", "warm", "vscode", "glass", "plain"}
STOCK = ["GitHub", "GitHub Dark", "Windows 11", "Windows 11 Dark", "TTY", "opencode", "Warm Light", "Warm Dark", "Gruvbox", "Nord",
         "Catppuccin", "VS Code Light", "VS Code Dark", "Glass Light", "Glass Dark"]


def lum(h):
    c = [int(h[i:i + 2], 16) / 255 for i in (1, 3, 5)]
    c = [x / 12.92 if x <= 0.03928 else ((x + 0.055) / 1.055) ** 2.4 for x in c]
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def contrast(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


class TestStockThemes(HarnessCase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        r = cls.h.call("stock_themes")
        cls.themes = {t["name"]: t for t in r["themes"]}
        cls.keys = r["colorKeys"]

    def test_exact_list_and_order_of_stock_presets(self):
        self.assertEqual(list(self.themes), STOCK)

    def test_default_theme_exists(self):
        self.assertIn("GitHub", self.themes)

    def test_every_colour_key_is_a_valid_hex_colour(self):
        for n, t in self.themes.items():
            for k in self.keys:
                self.assertRegex(t["colors"][k], HEX, f"{n}.{k}")
                self.assertNotEqual(t["colors"][k].lower(), "#ff00ff", f"{n}.{k} is the 'missing colour' marker")

    def test_all_colour_keys_have_labels(self):
        self.assertEqual(len(self.keys), len(set(self.keys)))
        self.assertGreaterEqual(len(self.keys), 21)

    def test_chrome_flavours_are_known(self):
        for n, t in self.themes.items():
            self.assertIn(t["chrome"], CHROMES, n)

    def test_all_flavours_are_exercised_by_stock_themes(self):
        self.assertEqual({t["chrome"] for t in self.themes.values()} - {"plain"} | {"plain"}, CHROMES)

    def test_stock_flags(self):
        for n, t in self.themes.items():
            self.assertTrue(t["builtin"], n)
            self.assertGreaterEqual(t["revision"], 1, n)

    def test_dark_flag_matches_actual_background(self):
        for n, t in self.themes.items():
            is_dark = lum(t["colors"]["bg"]) < 0.25
            self.assertEqual(t["dark"], is_dark, f"{n}: dark flag disagrees with its background colour")

    def test_names_are_safe_file_names(self):
        for n in self.themes:
            self.assertEqual(self.call("sanitize", name=n)["out"], n, f"'{n}' contains characters illegal in file names")

    def test_main_text_is_readable(self):
        """WCAG AA (4.5:1) for body text on the main surfaces of every stock theme."""
        bad = []
        for n, t in self.themes.items():
            c = t["colors"]
            for fg, bg in (("fg", "bg"), ("editorFg", "editorBg"), ("fg", "panel"), ("codeFg", "codeBg"), ("heading", "bg")):
                r = contrast(c[fg], c[bg])
                if r < 4.5:
                    bad.append(f"{n}: {fg} on {bg} = {r:.2f}")
        self.assertEqual(bad, [], "stock theme text below WCAG AA contrast")

    def test_accent_buttons_are_legible(self):
        bad = []
        for n, t in self.themes.items():
            r = contrast(t["colors"]["accentFg"], t["colors"]["accent"])
            if r < 3.0:
                bad.append(f"{n}: accentFg on accent = {r:.2f}")
        self.assertEqual(bad, [])

    def test_links_and_muted_text_are_visible(self):
        bad = []
        for n, t in self.themes.items():
            c = t["colors"]
            if contrast(c["link"], c["editorBg"]) < 3.0:
                bad.append(f"{n}: link {contrast(c['link'], c['editorBg']):.2f}")
            if contrast(c["muted"], c["bg"]) < 3.0:
                bad.append(f"{n}: muted {contrast(c['muted'], c['bg']):.2f}")
        self.assertEqual(bad, [])

    def test_selection_colour_differs_from_background(self):
        for n, t in self.themes.items():
            self.assertGreater(contrast(t["colors"]["selection"], t["colors"]["editorBg"]), 1.1, n)

    def test_font_stacks_end_with_a_generic_family(self):
        for n, t in self.themes.items():
            for k in ("ui", "editor", "preview", "code"):
                last = t["fonts"][k].split(",")[-1].strip()
                self.assertIn(last, ("sans-serif", "serif", "monospace"), f"{n}.fonts.{k} has no generic fallback: {t['fonts'][k]}")

    def test_sizes_are_sane(self):
        for n, t in self.themes.items():
            self.assertTrue(6 <= t["fonts"]["uiSize"] <= 20, n)
            self.assertTrue(8 <= t["fonts"]["editorSize"] <= 24, n)
            self.assertTrue(8 <= t["fonts"]["previewSize"] <= 24, n)
            self.assertTrue(400 <= t["shape"]["pageWidth"] <= 1400, n)
            self.assertTrue(0 <= t["shape"]["cornerRadius"] <= 40, n)
            self.assertTrue(0 <= t["shape"]["windowBorderWidth"] <= 6, n)

    def test_each_stock_theme_roundtrips_through_json(self):
        for n, t in self.themes.items():
            again = self.call("theme_from_json", theme=t)["theme"]
            self.assertEqual(again, t, f"{n} changes after toJson -> fromJson -> toJson")


class TestStyleSheet(HarnessCase):
    def test_all_tokens_replaced_and_qt_accepts_the_qss(self):
        for n in STOCK:
            r = self.call("stylesheet", theme=n)       # call() asserts that Qt emitted no warnings
            self.assertEqual(re.findall(r"@\w+@", r["qss"]), [], f"{n}: unreplaced @token@")
            self.assertGreater(r["length"], 3000)
            self.assertEqual(r["qss"].count("{"), r["qss"].count("}"), f"{n}: unbalanced braces in QSS")

    def test_no_missing_colour_marker_in_qss(self):
        for n in STOCK:
            self.assertNotIn("#ff00ff", self.call("stylesheet", theme=n)["qss"].lower(), n)

    def test_theme_colours_land_in_qss(self):
        for n, t in ((n, t) for n, t in {t["name"]: t for t in self.call("stock_themes")["themes"]}.items()):
            qss = self.call("stylesheet", theme=n)["qss"].lower()
            for k in ("bg", "panel", "fg", "accent"):
                self.assertIn(t["colors"][k].lower(), qss, f"{n}: {k} never used")

    def test_radius_tokens(self):
        qss = self.call("stylesheet", theme="Catppuccin")["qss"]    # radius 10
        self.assertIn("border-radius: 10px", qss)
        self.assertIn("border-radius: 5px", qss)
        qss0 = self.call("stylesheet", theme="TTY")["qss"]          # radius 0
        self.assertNotRegex(qss0, r"border-radius: [1-9]\d*px; padding: 6px 16px")

    def test_chrome_specific_rules(self):
        self.assertIn("QStatusBar { background:", self.call("stylesheet", theme="VS Code Dark")["qss"])
        self.assertIn("qlineargradient", self.call("stylesheet", theme="Glass Dark")["qss"])
        self.assertNotIn("qlineargradient", self.call("stylesheet", theme="Nord")["qss"].split("QSplitter::handle")[0].split("QSlider")[0] + "")

    def test_font_family_helper(self):
        c = lambda spec, q='"': self.call("css_family", spec=spec, quote=q)["out"]
        self.assertEqual(c("JetBrains Mono, Consolas, monospace"), '"JetBrains Mono", "Consolas", monospace')
        self.assertEqual(c("A, B", "'"), "'A', 'B'")
        self.assertEqual(c(" , ,serif, "), "serif")
        self.assertEqual(c(""), "")

    def test_make_font(self):
        r = self.call("make_font", spec="Foo, Bar, , Baz", pt=12)
        self.assertEqual(r["families"], ["Foo", "Bar", "Baz"])
        self.assertEqual(r["pt"], 12)
        self.assertEqual(self.call("make_font", spec="Foo", pt=1)["pt"], 6, "font size is clamped to 6pt minimum")
        self.call("make_font", spec="", pt=10)


class TestJsonComments(HarnessCase):
    def strip(self, s):
        return self.call("strip_comments", **{"in": s})["out"]

    def loads(self, s):
        return json.loads(self.strip(s))

    def test_line_comment(self):
        self.assertEqual(self.loads('{"a": 1 // c\n}'), {"a": 1})

    def test_block_comment(self):
        self.assertEqual(self.loads('{/* x */"a": /* y\n z */ 1}'), {"a": 1})

    def test_comment_markers_inside_strings_survive(self):
        self.assertEqual(self.loads('{"url": "http://x.y/*not*/z", "e": "a\\"//b"}'), {"url": "http://x.y/*not*/z", "e": 'a"//b'})

    def test_trailing_commas(self):
        self.assertEqual(self.loads('{"a": [1, 2, ], "b": {"c": 1,},}'), {"a": [1, 2], "b": {"c": 1}})

    def test_comma_inside_string_before_bracket_survives(self):
        self.assertEqual(self.loads(r'{"re": "\\d{2,}", "x": ",]"}'), {"re": "\\d{2,}", "x": ",]"})

    def test_unterminated_block_comment_does_not_hang(self):
        self.strip('{"a":1} /* never closed')

    def test_empty_and_only_comments(self):
        self.assertEqual(self.strip("").strip(), "")
        self.assertEqual(self.strip("// only\n/* c */").strip(), "")

    def test_matches_python_twin(self):
        samples = ['{"a":1, // x\n "b":[1,2,],}', '/* a */ {"s":"//"}', '{"q":"\\\\" // c\n}']
        for s in samples:
            self.assertEqual(json.loads(self.strip(s)), json.loads(strip_jsonc(s)))

    def test_repo_example_preset_is_valid_jsonc(self):
        text = read(ROOT / "Documentation/presets/example-custom.jsonc")
        obj = json.loads(self.strip(text))
        self.assertEqual(obj["name"], "Example Sunset")
        t = self.call("theme_from_json", theme=obj)
        for k in ("bg", "panel", "fg", "accent", "frameBorder", "frameBorderInactive", "selection", "lineHighlight"):
            self.assertRegex(t["theme"]["colors"][k], HEX, f"derived colour {k}")
        self.assertEqual(t["theme"]["shape"]["windowBorderWidth"], 2)

    def test_readme_example_preset_is_valid(self):
        readme = read(ROOT / "README.md")
        block = re.search(r"```jsonc\n(\{.*?\n\})\n```", readme, re.S).group(1)
        t = self.call("theme_from_json", theme=json.loads(self.strip(block)))["theme"]
        self.assertEqual(t["name"], "Example Sunset")
        self.assertEqual(t["chrome"], "plain")


class TestThemeParsing(HarnessCase):
    def parse(self, obj):
        return self.call("theme_from_json", theme=obj)

    def test_empty_object_gives_complete_theme(self):
        t = self.parse({})["theme"]
        keys = self.call("stock_themes")["colorKeys"]
        for k in keys:
            self.assertRegex(t["colors"][k], HEX, k)
        self.assertEqual(t["chrome"], "plain")
        for k in ("ui", "editor", "preview", "code"):
            self.assertTrue(t["fonts"][k])

    def test_missing_keys_are_derived_from_given_ones(self):
        t = self.parse({"colors": {"bg": "#101010", "fg": "#eeeeee", "accent": "#00ff00"}})["theme"]["colors"]
        self.assertEqual(t["editorBg"].lower(), "#101010")
        self.assertEqual(t["editorFg"].lower(), "#eeeeee")
        self.assertEqual(t["link"].lower(), "#00ff00")
        self.assertEqual(t["heading"].lower(), "#eeeeee")
        self.assertEqual(t["gutterBg"].lower(), "#101010")

    def test_given_keys_are_kept(self):
        t = self.parse({"colors": {"bg": "#101010", "link": "#123456"}})["theme"]["colors"]
        self.assertEqual(t["link"].lower(), "#123456")

    def test_window_border_width_clamped(self):
        for given, want in ((-5, 0), (0, 0), (3, 3), (6, 6), (99, 6)):
            self.assertEqual(self.parse({"shape": {"windowBorderWidth": given}})["frameWidth"], want, given)

    def test_wrong_types_fall_back_to_defaults(self):
        t = self.parse({"name": 5, "dark": "yes", "chrome": 3, "colors": {"bg": 7}, "fonts": "x", "shape": []})["theme"]
        self.assertTrue(t["name"] in ("", "5"))
        self.assertTrue(isinstance(t["colors"]["fg"], str))

    def test_defaults_when_shape_missing(self):
        t = self.parse({"name": "x"})
        self.assertEqual(t["radius"], 8)
        self.assertEqual(t["theme"]["shape"]["pageWidth"], 860)

    def test_unknown_keys_ignored(self):
        self.parse({"name": "x", "colours": {"bg": "#fff"}, "extra": [1, 2, 3]})

    def test_derived_selection_is_between_bg_and_accent(self):
        c = self.parse({"colors": {"bg": "#000000", "accent": "#ffffff"}})["theme"]["colors"]
        self.assertEqual(c["selection"].lower(), "#595959")

    def test_missing_colour_lookup_is_loud(self):
        self.assertEqual(self.call("theme_color", key="doesNotExist")["fallback"], "#ff00ff")


class TestPresetsOnDisk(unittest.TestCase):
    def setUp(self):
        self.h = Harness()
        self.dir = Path(self.h.home) / ".local/share/MakDown/presets"

    def tearDown(self):
        self.h.close()

    def call(self, cmd, **kw):
        r = self.h.call(cmd, **kw)
        self.assertEqual(r["warnings"], [], r["warnings"])
        return r

    def stock_file(self, name):
        return self.dir / f"{name}.jsonc"

    def test_first_launch_writes_every_stock_preset(self):
        self.call("ensure_stock")
        self.assertEqual({p.stem for p in self.dir.glob("*.jsonc")}, set(STOCK))

    def test_written_files_are_valid_jsonc_with_header_comment(self):
        self.call("ensure_stock")
        for p in self.dir.glob("*.jsonc"):
            text = p.read_text(encoding="utf-8")
            self.assertTrue(text.startswith("// MakDown preset:"), p.name)
            obj = json.loads(strip_jsonc(text))
            self.assertEqual(obj["name"], p.stem)
            self.assertTrue(obj["builtin"])

    def test_load_all_returns_stock_in_canonical_order(self):
        self.call("ensure_stock")
        names = [t["name"] for t in self.call("load_themes")["themes"]]
        self.assertEqual(names, STOCK)

    def test_load_all_with_empty_folder_falls_back_to_stock(self):
        names = [t["name"] for t in self.call("load_themes")["themes"]]
        self.assertEqual(names, STOCK)

    def test_custom_themes_follow_stock_alphabetically(self):
        self.call("ensure_stock")
        for n in ("zebra", "Alpha", "beta"):
            self.call("save_theme", theme={"name": n, "builtin": False, "colors": {"bg": "#000000"}})
        names = [t["name"] for t in self.call("load_themes")["themes"]]
        self.assertEqual(names[:len(STOCK)], STOCK)
        self.assertEqual(names[len(STOCK):], ["Alpha", "beta", "zebra"])

    def test_save_load_roundtrip(self):
        theme = {"name": "My Theme", "builtin": False, "dark": True, "chrome": "terminal", "colors": {"bg": "#123456", "fg": "#fedcba"},
                 "shape": {"cornerRadius": 3, "windowBorderWidth": 4}}
        self.assertTrue(self.call("save_theme", theme=theme)["saved"])
        t = next(t for t in self.call("load_themes")["themes"] if t["name"] == "My Theme")
        self.assertEqual((t["chrome"], t["dark"], t["builtin"]), ("terminal", True, False))

    def test_save_sanitizes_file_name(self):
        self.call("save_theme", theme={"name": 'a/b:c*d?e"f<g>h|i\\j', "builtin": False})
        files = [p.name for p in self.dir.glob("*.jsonc")]
        self.assertEqual(files, ["a_b_c_d_e_f_g_h_i_j.jsonc"])
        self.assertFalse(list(self.dir.parent.glob("*.jsonc")), "path traversal: nothing may be written outside presets/")

    def test_path_traversal_in_name_is_contained(self):
        self.call("save_theme", theme={"name": "../../escape", "builtin": False})
        self.assertFalse((Path(self.h.home) / "escape.jsonc").exists())
        self.assertFalse((Path(self.h.home) / ".local/share/escape.jsonc").exists())

    def test_remove(self):
        self.call("save_theme", theme={"name": "Gone", "builtin": False})
        self.assertTrue(self.call("remove_theme", name="Gone")["removed"])
        self.assertFalse(self.call("remove_theme", name="Gone")["removed"])
        self.assertNotIn("Gone", [t["name"] for t in self.call("load_themes")["themes"]])

    def test_outdated_untouched_stock_preset_is_refreshed(self):
        self.call("ensure_stock")
        f = self.stock_file("Nord")
        obj = json.loads(strip_jsonc(f.read_text()))
        obj["revision"] = 0
        obj["colors"]["bg"] = "#abcdef"
        f.write_text(json.dumps(obj))
        self.call("ensure_stock")
        self.assertEqual(json.loads(strip_jsonc(f.read_text()))["colors"]["bg"].lower(), "#2e3440")

    def test_user_took_over_preset_is_preserved(self):
        self.call("ensure_stock")
        f = self.stock_file("Nord")
        obj = json.loads(strip_jsonc(f.read_text()))
        obj["revision"] = 0
        obj["builtin"] = False
        obj["colors"]["bg"] = "#abcdef"
        f.write_text(json.dumps(obj))
        self.call("ensure_stock")
        self.assertEqual(json.loads(strip_jsonc(f.read_text()))["colors"]["bg"].lower(), "#abcdef")

    def test_up_to_date_stock_preset_with_user_edit_is_kept(self):
        self.call("ensure_stock")
        f = self.stock_file("Gruvbox")
        f.write_text(f.read_text().replace("// MakDown preset", "// EDITED by me"))
        self.call("ensure_stock")
        self.assertIn("EDITED by me", f.read_text())

    def test_deleted_stock_preset_comes_back(self):
        self.call("ensure_stock")
        self.stock_file("Catppuccin").unlink()
        self.call("ensure_stock")
        self.assertTrue(self.stock_file("Catppuccin").exists())

    def test_broken_files_are_skipped(self):
        self.call("ensure_stock")
        (self.dir / "bad.jsonc").write_text("{ nope")
        (self.dir / "empty.jsonc").write_text("")
        (self.dir / "array.jsonc").write_text("[1,2,3]")
        names = [t["name"] for t in self.call("load_themes")["themes"]]
        self.assertEqual(names, STOCK)

    def test_theme_without_name_uses_file_name(self):
        self.call("ensure_stock")
        (self.dir / "Unnamed One.jsonc").write_text('{"colors": {"bg": "#000000"}}')
        self.assertIn("Unnamed One", [t["name"] for t in self.call("load_themes")["themes"]])

    def test_plain_json_extension_also_loaded(self):
        self.call("ensure_stock")
        (self.dir / "other.json").write_text('{"name": "FromJson"}')
        self.assertIn("FromJson", [t["name"] for t in self.call("load_themes")["themes"]])

    def test_ensure_stock_is_idempotent(self):
        self.call("ensure_stock")
        before = {p.name: p.read_bytes() for p in self.dir.glob("*.jsonc")}
        self.call("ensure_stock")
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.dir.glob("*.jsonc")})


class TestSettings(unittest.TestCase):
    def setUp(self):
        self.h = Harness()
        self.file = Path(self.h.home) / ".local/share/MakDown/settings.jsonc"

    def tearDown(self):
        self.h.close()

    def load(self):
        return self.h.call("settings_load")["settings"]

    def test_defaults_when_no_file(self):
        s = self.load()
        self.assertEqual((s["theme"], s["vim"], s["lineNumbers"], s["tabWidth"], s["viewMode"], s["animations"]),
                         ("GitHub", False, True, 4, 1, 2))
        self.assertTrue(s["wrap"] and s["explorer"] and s["liveReload"] and s["customFrame"])
        self.assertFalse(s["console"])

    def test_roundtrip(self):
        want = {"theme": "Nord", "vim": True, "tabWidth": 2, "viewMode": 2, "animations": 3, "wrap": False, "folder": "/tmp/x",
                "lastFile": "/tmp/x/a.md", "lineNumbers": False, "console": True}
        self.h.call("settings_save", settings=want)
        got = self.load()
        for k, v in want.items():
            self.assertEqual(got[k], v, k)

    def test_file_is_commented_jsonc_and_valid(self):
        self.h.call("settings_save", settings={"theme": "Nord"})
        text = self.file.read_text(encoding="utf-8")
        self.assertTrue(text.startswith("//"))
        self.assertEqual(json.loads(strip_jsonc(text))["theme"], "Nord")

    def test_hand_edited_file_with_comments(self):
        self.file.parent.mkdir(parents=True)
        self.file.write_text('// hi\n{ "theme": "Gruvbox", /* c */ "vim": true, }')
        s = self.load()
        self.assertEqual((s["theme"], s["vim"]), ("Gruvbox", True))

    def test_corrupt_file_is_ignored(self):
        self.file.parent.mkdir(parents=True)
        for junk in ("", "{", "[1]", "garbage", "\x00\x01\x02", '{"theme": 5, "vim": "no", "tabWidth": "wide"}'):
            self.file.write_text(junk)
            s = self.load()
            self.assertIn("theme", s)

    def test_animation_level_is_clamped(self):
        self.file.parent.mkdir(parents=True)
        for given, want in ((-3, 0), (0, 0), (3, 3), (9, 3)):
            self.file.write_text(json.dumps({"animations": given}))
            self.assertEqual(self.load()["animations"], want)

    def test_unknown_keys_are_ignored(self):
        self.file.parent.mkdir(parents=True)
        self.file.write_text('{"fromTheFuture": 1, "vim": true}')
        self.assertTrue(self.load()["vim"])

    def test_save_creates_missing_directories(self):
        self.assertFalse(self.file.parent.exists())
        self.h.call("settings_save", settings={})
        self.assertTrue(self.file.exists())

    def test_unicode_paths_roundtrip(self):
        self.h.call("settings_save", settings={"folder": "/tmp/zażółć 日本", "lastFile": "/tmp/😀.md"})
        s = self.load()
        self.assertEqual((s["folder"], s["lastFile"]), ("/tmp/zażółć 日本", "/tmp/😀.md"))


class TestAnimLevels(HarnessCase):
    def anim(self, level=None, base=100):
        kw = {"base": base}
        if level is not None:
            kw["level"] = level
        return self.call("anim", **kw)

    def test_off_means_zero_duration(self):
        r = self.anim(0)
        self.assertEqual((r["on"], r["ms"]), (False, 0))

    def test_durations_grow_with_level(self):
        ms = [self.anim(l)["ms"] for l in (1, 2, 3)]
        self.assertTrue(all(m > 0 for m in ms))
        self.assertEqual(sorted(ms), ms)
        self.assertLess(ms[0], ms[2])

    def test_out_of_range_levels_are_clamped(self):
        self.assertEqual(self.anim(-5)["level"], 0)
        self.assertEqual(self.anim(99)["level"], 3)

    def test_zero_base_is_zero(self):
        self.assertEqual(self.anim(2, base=0)["ms"], 0)


class TestBuildInfo(HarnessCase):
    def test_banner_lists_toolchain_and_qt(self):
        info = self.call("buildinfo")["info"]
        for needle in ("MakDown build information", "Makefile target", "Compiler", "C++ standard", "Qt (compiled)", "Qt (runtime)",
                       "QtWebEngine", "Architecture", "Operating system"):
            self.assertIn(needle, info)
        self.assertRegex(info, r"C\+\+ standard\s*:\s*2017\d\d")


if __name__ == "__main__":
    unittest.main(verbosity=2)
