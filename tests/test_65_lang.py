# SPDX-License-Identifier: MIT
"""Interface languages: lang/*.json, the embedded stock copy, lookup / fallback / plural rules (core/lang.cpp) and
what a real MainWindow shows in each language."""
import json
import re
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lib.common import ROOT, Harness, HarnessCase, read

LANG_DIR = ROOT / "lang"
CODES = ("en", "ru", "pl")
RULES = {"en": "one_other", "ru": "east_slavic", "pl": "polish"}


def load(code):
    return json.loads((LANG_DIR / f"{code}.json").read_text(encoding="utf-8"))


def forms(v):
    return v if isinstance(v, list) else [v]


def source_text():
    parts = []
    for d in ("core", "ui", "init"):
        for p in sorted((ROOT / d).glob("*.cpp")):
            if p.name not in ("langstock.cpp", "appicon.cpp"):
                parts.append(read(p))
    return "\n".join(parts)


class TestLanguageFiles(unittest.TestCase):
    """The shipped lang/*.json files, checked without building anything."""

    @classmethod
    def setUpClass(cls):
        cls.data = {c: load(c) for c in CODES}
        cls.en = cls.data["en"]["strings"]

    def test_files_exist_and_have_the_required_fields(self):
        for c in CODES:
            d = self.data[c]
            self.assertTrue(d["name"].strip(), c)
            self.assertIsInstance(d["revision"], int, c)
            self.assertIs(d["builtin"], True, f"{c}: stock files must say \"builtin\": true")
            self.assertEqual(d["plural"], RULES[c], c)
            self.assertTrue(d["strings"], c)

    def test_every_language_has_exactly_the_english_keys(self):
        for c in ("ru", "pl"):
            keys = set(self.data[c]["strings"])
            self.assertEqual(sorted(set(self.en) - keys), [], f"{c}: untranslated keys")
            self.assertEqual(sorted(keys - set(self.en)), [], f"{c}: keys English does not have")

    def test_no_empty_strings(self):
        for c in CODES:
            for k, v in self.data[c]["strings"].items():
                for f in forms(v):
                    self.assertIsInstance(f, str, f"{c}:{k}")
                    self.assertTrue(f.strip(), f"{c}:{k} is empty")

    def test_placeholders_match_english(self):
        for c in ("ru", "pl"):
            for k, v in self.data[c]["strings"].items():
                want = sorted(set(re.findall(r"%\d", " ".join(forms(self.en[k])))))
                for f in forms(v):
                    self.assertEqual(sorted(set(re.findall(r"%\d", f))), want, f"{c}:{k} {f!r}")

    def test_plural_entries_are_arrays_with_a_usable_number_of_forms(self):
        for k, v in self.en.items():
            if not isinstance(v, list):
                continue
            self.assertEqual(len(v), 2, f"en:{k}")
            for f in v:
                self.assertIn("%n", f, f"en:{k} plural form without %n")
            for c in ("ru", "pl"):
                t = self.data[c]["strings"][k]
                self.assertIsInstance(t, list, f"{c}:{k} must be a plural array")
                self.assertIn(len(t), (1, 3), f"{c}:{k}")
                for f in t:
                    self.assertIn("%n", f, f"{c}:{k}")

    def test_non_plural_strings_stay_plain_strings(self):
        for c in ("ru", "pl"):
            for k, v in self.data[c]["strings"].items():
                if not isinstance(self.en[k], list):
                    self.assertIsInstance(v, str, f"{c}:{k}")

    def test_escaped_ampersands_and_html_survive_translation(self):
        for c in ("ru", "pl"):
            for k, v in self.data[c]["strings"].items():
                if isinstance(v, str):
                    e = self.en[k]
                    self.assertEqual(v.count("&&"), e.count("&&"), f"{c}:{k} '&&' (a literal ampersand in menus)")
                    self.assertEqual(sorted(re.findall(r"</?\w+>", v)), sorted(re.findall(r"</?\w+>", e)), f"{c}:{k} html tags")

    def test_russian_is_really_translated(self):
        allow = {"menu.git", "filter.markdown", "side.git", "chrome.terminal", "search.summary", "unit.kb", "unit.b"}
        for k, v in self.data["ru"]["strings"].items():
            if k in allow:
                continue
            self.assertTrue(re.search("[а-яА-ЯёЁ]", " ".join(forms(v))), f"ru:{k} has no Cyrillic: {v!r}")

    def test_polish_differs_from_english_where_it_should(self):
        same = [k for k, v in self.data["pl"]["strings"].items() if v == self.en[k]]
        allow = {"menu.git", "filter.markdown", "side.git", "chrome.terminal", "search.summary", "unit.b", "unit.kb", "unit.mb",
                 "settings.title.x"}
        self.assertEqual(sorted(set(same) - allow), [], "Polish strings identical to English (forgotten?)")

    def test_every_key_used_in_the_code_exists_in_english(self):
        used = set(re.findall(r'\bLn?\(\s*"([\w.]+)"', source_text()))
        self.assertGreater(len(used), 150, "failed to scan the sources")
        self.assertEqual(sorted(k for k in used if k not in self.en), [], "L(\"key\") in the code but missing from lang/en.json")

    def test_every_english_key_is_used_by_the_code(self):
        src = source_text()
        unused = []
        for k in self.en:
            if k.startswith("color."):
                continue                                    # looked up dynamically: "color." + key (ThemeStore::colorLabel)
            if f'"{k}"' not in src:
                unused.append(k)
        self.assertEqual(unused, [], "keys in lang/en.json that no code uses (dead strings)")

    def test_every_theme_colour_has_a_label_in_every_language(self):
        from lib.common import Harness
        h = Harness()
        try:
            keys = h.call("stock_themes")["colorKeys"]
        finally:
            h.close()
        for c in CODES:
            for k in keys:
                self.assertIn("color." + k, self.data[c]["strings"], f"{c}: no label for colour '{k}'")

    def test_generated_stock_file_is_in_sync(self):
        p = subprocess.run([sys.executable, str(ROOT / "scripts/gen-lang.py"), "--check"], capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)


class TestLanguageEngine(HarnessCase):
    """core/lang.cpp through the harness (isolated $HOME, English OS locale)."""

    def lang_dir(self):
        return Path(self.h.call("lang_ensure")["dir"])

    def test_embedded_stock_matches_the_json_files(self):
        r = self.call("lang_stock")
        self.assertEqual([f["file"] for f in r["files"]], ["en.json", "pl.json", "ru.json"])
        for f in r["files"]:
            self.assertEqual(json.loads(f["json"]), load(Path(f["file"]).stem), f["file"])

    def test_ensure_stock_writes_the_stock_languages(self):
        d = self.lang_dir()
        for c in CODES:
            self.assertEqual(json.loads((d / f"{c}.json").read_text(encoding="utf-8")), load(c), c)
        codes = [l["code"] for l in self.call("lang_available")["languages"]]
        self.assertEqual(codes[:3], ["en", "pl", "ru"], "English first, then by code")

    def test_user_files_are_never_overwritten_but_outdated_stock_files_are_refreshed(self):
        d = self.lang_dir()
        mine = load("ru"); mine["builtin"] = False; mine["strings"]["menu.file"] = "&МойФайл"
        (d / "ru.json").write_text(json.dumps(mine, ensure_ascii=False), encoding="utf-8")
        old = load("pl"); old["revision"] = 0; old["strings"]["menu.file"] = "&Stary"
        (d / "pl.json").write_text(json.dumps(old, ensure_ascii=False), encoding="utf-8")
        self.call("lang_ensure")
        self.assertEqual(json.loads((d / "ru.json").read_text(encoding="utf-8"))["strings"]["menu.file"], "&МойФайл")
        self.assertEqual(json.loads((d / "pl.json").read_text(encoding="utf-8"))["strings"]["menu.file"], load("pl")["strings"]["menu.file"])
        self.call("lang_ensure")

    def test_lookup_in_each_language(self):
        self.lang_dir()
        for c in CODES:
            self.assertTrue(self.call("lang_load", code=c)["loaded"])
            self.assertEqual(self.h.call("lang_load", code=c)["current"], c)
            for k in ("menu.file", "dlg.unsaved.title", "vim.insert"):
                self.assertEqual(self.call("lang_text", key=k)["text"], load(c)["strings"][k], f"{c}:{k}")
        self.call("lang_load", code="en")

    def test_missing_keys_fall_back_to_english_and_then_to_the_key(self):
        d = self.lang_dir()
        (d / "xx.json").write_text(json.dumps({"name": "Test", "strings": {"menu.file": "&Datei"}}), encoding="utf-8")
        self.call("lang_load", code="xx")
        self.assertEqual(self.call("lang_text", key="menu.file")["text"], "&Datei")
        self.assertEqual(self.call("lang_text", key="menu.edit")["text"], "&Edit")
        self.assertEqual(self.call("lang_text", key="no.such.key")["text"], "no.such.key")
        self.assertEqual(self.call("lang_text", key="status.words", n=2)["text"], "2 words", "plural falls back to English too")
        self.call("lang_load", code="en")

    def test_unusable_language_falls_back_to_english(self):
        d = self.lang_dir()
        (d / "bad.json").write_text("{{{ not json", encoding="utf-8")
        (d / "empty.json").write_text("{}", encoding="utf-8")
        for c in ("bad", "empty", "nonexistent"):
            r = self.call("lang_load", code=c)
            self.assertFalse(r["loaded"], c)
            self.assertEqual(r["current"], "en", c)
            self.assertEqual(self.call("lang_text", key="menu.file")["text"], "&File")
        names = [l["code"] for l in self.call("lang_available")["languages"]]
        self.assertNotIn("bad", names)
        self.assertNotIn("empty", names)

    def test_jsonc_comments_and_trailing_commas_are_accepted(self):
        text = '{ // my language\n "name": "Commented", /* x */ "plural": "single",\n "strings": { "menu.file": "&F://x", } }'
        r = self.call("lang_parse", json=text)
        self.assertTrue(r["valid"], r["error"])
        self.assertEqual(r["name"], "Commented")

    def test_resolve(self):
        d = self.lang_dir()
        (d / "pt-BR.json").write_text(json.dumps({"name": "Português", "strings": {"menu.file": "&Arquivo"}}), encoding="utf-8")
        for setting, want in [("pl", "pl"), ("PL", "pl"), ("ru", "ru"), ("en", "en"), ("zz", "en"), ("", "en"),
                              ("pt_br", "pt-BR"), ("pt-BR", "pt-BR"), ("auto", "en")]:      # "auto" under the test locale (C) is English
            self.assertEqual(self.call("lang_resolve", setting=setting)["code"], want, setting)

    def test_plural_rules(self):
        cases = {
            "one_other": {0: 1, 1: 0, 2: 1, 5: 1, 21: 1},
            "east_slavic": {0: 2, 1: 0, 2: 1, 4: 1, 5: 2, 11: 2, 12: 2, 14: 2, 21: 0, 22: 1, 25: 2, 101: 0, 111: 2, 112: 2, 1000: 2},
            "polish": {0: 2, 1: 0, 2: 1, 4: 1, 5: 2, 11: 2, 12: 2, 14: 2, 21: 2, 22: 1, 25: 2, 101: 2, 111: 2, 112: 2, 122: 1},
            "single": {0: 0, 1: 0, 7: 0},
            "no-such-rule": {1: 0, 3: 1},
        }
        for rule, table in cases.items():
            for n, idx in table.items():
                self.assertEqual(self.call("lang_plural_index", rule=rule, n=n)["index"], idx, f"{rule}({n})")

    def test_plural_strings_per_language(self):
        self.lang_dir()
        want = {
            "en": {1: "1 word", 2: "2 words", 5: "5 words"},
            "ru": {1: "1 слово", 2: "2 слова", 5: "5 слов", 11: "11 слов", 21: "21 слово"},
            "pl": {1: "1 słowo", 2: "2 słowa", 5: "5 słów", 12: "12 słów", 22: "22 słowa"},
        }
        for c, table in want.items():
            self.call("lang_load", code=c)
            for n, text in table.items():
                self.assertEqual(self.call("lang_text", key="status.words", n=n)["text"], text, f"{c} {n}")
        self.call("lang_load", code="ru")
        self.assertEqual(self.call("lang_text", key="search.inNFiles", n=3)["text"], "в 3 файлах")
        self.call("lang_load", code="en")

    def test_vim_mode_indicator_is_translated(self):
        self.lang_dir()
        self.call("lang_load", code="ru")
        r = self.call("ed_reset", vim=True, text="abc", pos=0)
        self.assertEqual(r["mode"], load("ru")["strings"]["vim.normal"])
        self.call("lang_load", code="en")
        r = self.call("ed_reset", vim=True, text="abc", pos=0)
        self.assertEqual(r["mode"], "-- NORMAL --")

    def test_colour_labels_are_translated(self):
        self.lang_dir()
        self.call("lang_load", code="pl")
        self.assertEqual(self.call("lang_text", key="color.bg")["text"], "Tło okna")
        self.call("lang_load", code="en")


class TestLanguageSetting(HarnessCase):
    def test_setting_defaults_to_auto_and_round_trips(self):
        self.assertEqual(self.call("settings_load")["settings"]["language"], "auto")
        self.call("settings_save", settings={"language": "pl"})
        self.assertEqual(self.call("settings_load")["settings"]["language"], "pl")
        self.call("settings_save", settings={})
        self.assertEqual(self.call("settings_load")["settings"]["language"], "auto")


class TestWindowLanguages(unittest.TestCase):
    """The real MainWindow, built from the user's settings, in each language."""

    def window(self, language):
        h = Harness(timeout=90)
        try:
            if language:
                h.call("settings_save", settings={"language": language})
            r = h.call("win_open", file="", wait=500)
            r.update(h.call("win_strings"))
            return r
        finally:
            h.close()

    def test_english(self):
        r = self.window("en")
        self.assertEqual(r["menus"][:3], ["&File", "&Edit", "&Search"])
        self.assertTrue(r["text"].startswith("# Welcome to MakDown"))
        self.assertTrue(any("words" in l for l in r["labels"]), r["labels"])

    def test_default_follows_the_os_language(self):
        r = self.window(None)                                   # no settings file, LC_ALL=C.UTF-8
        self.assertEqual(r["menus"][0], "&File")

    def test_russian(self):
        r = self.window("ru")
        s = load("ru")["strings"]
        self.assertEqual(r["menus"], [s[k] for k in ("menu.file", "menu.edit", "menu.search", "menu.view", "menu.browser", "menu.git", "menu.settings", "menu.help")])
        self.assertTrue(r["text"].startswith("# Добро пожаловать в MakDown"))
        self.assertIn(s["file.save"], r["items"])
        self.assertTrue(any("слов" in l for l in r["labels"]), r["labels"])
        self.assertTrue(any("Просмотр" in p or "Только" in p for p in r["labels"] + r["items"]))

    def test_polish(self):
        r = self.window("pl")
        s = load("pl")["strings"]
        self.assertEqual(r["menus"][0], s["menu.file"])
        self.assertEqual(r["menus"][-1], s["menu.help"])
        self.assertTrue(r["text"].startswith("# Witaj w MakDown"))
        self.assertTrue(any("słów" in l for l in r["labels"]), r["labels"])

    def test_settings_dialog_pages_in_every_language(self):
        for code in CODES:
            h = Harness(timeout=90)
            try:
                h.call("settings_save", settings={"language": code})
                h.call("win_open", file="", wait=300)
                s = load(code)["strings"]
                for page in range(6):
                    r = h.call("settings_grab", page=page)
                    self.assertEqual(r["title"], s["settings.title"], code)
                    self.assertEqual(r["nav"], [s[k] for k in ("nav.appearance", "nav.editor", "nav.preview", "nav.animations", "nav.language", "nav.about")])
                lang_page = h.call("settings_grab", page=4)
                self.assertIn(s["lang.label"], lang_page["labels"], code)
                self.assertTrue(h.stderr.count("Qt") == 0 or "QObject" not in h.stderr, h.stderr[-500:])
            finally:
                h.close()

    def test_unknown_language_setting_shows_english(self):
        r = self.window("zz")
        self.assertEqual(r["menus"][0], "&File")

    def test_a_custom_language_file_is_picked_up(self):
        h = Harness(timeout=90)
        try:
            d = Path(h.call("lang_ensure")["dir"])
            (d / "de.json").write_text(json.dumps({"name": "Deutsch", "plural": "one_other", "strings": {
                "menu.file": "&Datei", "welcome.doc": "# Willkommen bei MakDown\n"}}), encoding="utf-8")
            h.call("settings_save", settings={"language": "de"})
            r = h.call("win_open", file="", wait=500)
            r.update(h.call("win_strings"))
            self.assertEqual(r["menus"][0], "&Datei")
            self.assertEqual(r["menus"][1], "&Edit", "untranslated menus fall back to English")
            self.assertTrue(r["text"].startswith("# Willkommen"))
        finally:
            h.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
