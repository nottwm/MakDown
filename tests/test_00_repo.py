# SPDX-License-Identifier: MIT
"""Static checks: the repository is internally consistent (no build, no Qt needed).

These catch the boring mistakes that break releases: a new .cpp that is not in a Makefile fragment, a header
with Q_OBJECT that was never added to moc-y, docs that link to files that moved, a Windows function that was
only implemented for Linux, a generated file that drifted from its source, and so on.
"""
import ast
import json
import os
import re
import stat
import struct
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lib.common import (ROOT, SOURCE_DIRS, github_slug, make_fragment_objs, markdown_headings, read, run, strip_jsonc,
                        tracked_files)

CPP_DIRS = ["init", "core", "ui", "arch/unx", "arch/win"]


def cpp_files():
    return sorted(p for d in CPP_DIRS for p in (ROOT / d).glob("*.cpp"))


class TestBuildSystemWiring(unittest.TestCase):
    """The kbuild-style Makefiles must list exactly the sources that exist."""

    def test_every_cpp_is_listed_in_its_makefile_fragment(self):
        missing = []
        for d in CPP_DIRS:
            frag = ROOT / d / "Makefile"
            self.assertTrue(frag.exists(), f"{d}/Makefile is missing")
            objs, _ = make_fragment_objs(frag)
            listed = {o for o in objs}
            for cpp in (ROOT / d).glob("*.cpp"):
                obj = f"{d}/{cpp.stem}.o"
                if obj not in listed:
                    missing.append(f"{d}/{cpp.name}")
        self.assertEqual(missing, [], f"source files not compiled by any Makefile fragment: {missing}")

    def test_every_listed_object_has_a_source(self):
        ghosts = []
        for d in CPP_DIRS:
            objs, _ = make_fragment_objs(ROOT / d / "Makefile")
            for o in objs:
                if o.endswith(".rc.o"):
                    src = ROOT / o[:-2]
                else:
                    src = ROOT / (o[:-2] + ".cpp")
                if not src.exists():
                    ghosts.append(f"{d}: {o} -> {src.relative_to(ROOT)} does not exist")
        self.assertEqual(ghosts, [])

    def test_moc_list_matches_headers_with_q_object(self):
        _, mocs = [], []
        for d in CPP_DIRS:
            _, m = make_fragment_objs(ROOT / d / "Makefile")
            mocs += m
        with_q = {h.stem for h in (ROOT / "include/makdown/ui").glob("*.h") if re.search(r"\bQ_OBJECT\b", read(h))}
        self.assertEqual(sorted(set(mocs) - with_q), [], "moc-y lists headers that have no Q_OBJECT")
        self.assertEqual(sorted(with_q - set(mocs)), [],
                         "headers with Q_OBJECT that are NOT in moc-y (signals/slots would fail at link time)")
        self.assertEqual(len(mocs), len(set(mocs)), "duplicate entries in moc-y")

    def test_no_q_object_outside_ui_headers(self):
        """The Makefile only runs moc on include/makdown/ui/*.h - Q_OBJECT anywhere else would silently not work."""
        offenders = []
        for h in list((ROOT / "include/makdown/core").glob("*.h")) + list((ROOT / "include/makdown/arch").glob("*.h")):
            if re.search(r"\bQ_OBJECT\b", read(h)):
                offenders.append(str(h.relative_to(ROOT)))
        for c in cpp_files():
            if re.search(r"^\s*Q_OBJECT\b", read(c), re.M):
                offenders.append(str(c.relative_to(ROOT)))
        self.assertEqual(offenders, [], "Q_OBJECT in files the Makefile does not moc")

    def test_no_duplicate_object_across_fragments(self):
        allobjs = []
        for d in CPP_DIRS:
            if d.startswith("arch/"):
                continue
            allobjs += make_fragment_objs(ROOT / d / "Makefile")[0]
        dup = {o for o in allobjs if allobjs.count(o) > 1}
        self.assertEqual(dup, set())

    def test_makefile_help_text_in_sync_with_phony_targets(self):
        mk = read(ROOT / "Makefile")
        for t in ("unx", "win", "deploy", "run", "debug", "clean", "mrproper", "help"):
            self.assertRegex(mk, rf"(?m)^{t}:", f"target '{t}' documented in README is not defined")
        readme = read(ROOT / "README.md")
        for t in ("make unx", "make win", "make deploy", "make run", "make debug", "make clean", "make mrproper", "make help"):
            self.assertIn(t, readme, f"README does not document '{t}'")


class TestArchitectureRules(unittest.TestCase):
    """The design rules written in README / architecture.md, enforced mechanically."""

    def test_core_never_includes_ui(self):
        bad = []
        for f in list((ROOT / "core").glob("*.cpp")) + list((ROOT / "include/makdown/core").glob("*.h")):
            for ln, line in enumerate(read(f).splitlines(), 1):
                if re.match(r'\s*#\s*include\s*[<"]makdown/ui/', line):
                    bad.append(f"{f.relative_to(ROOT)}:{ln}: {line.strip()}")
        self.assertEqual(bad, [], "core/ must stay UI-independent")

    def test_os_specific_code_only_in_arch(self):
        pat = re.compile(r"#\s*include\s*<(windows|winuser|dwmapi|shellapi|unistd|sys/\w+|termios|pty|signal)\.h>|\b_WIN32\b|Q_OS_(WIN|MAC|LINUX)")
        # QT platform macros in ui/ are tolerated only inside #ifdef blocks that existed before; flag raw OS headers always
        hard = re.compile(r"#\s*include\s*<(windows|winuser|dwmapi|shellapi)\.h>")
        bad = []
        for d in ("core", "ui", "init"):
            for f in (ROOT / d).glob("*.cpp"):
                for ln, line in enumerate(read(f).splitlines(), 1):
                    if hard.search(line):
                        bad.append(f"{f.relative_to(ROOT)}:{ln}: {line.strip()}")
        self.assertEqual(bad, [], "Windows headers belong in arch/win only")

    def test_platform_api_implemented_on_every_target(self):
        header = read(ROOT / "include/makdown/arch/platform.h")
        body = header.split("namespace platform", 1)[1]
        body = re.sub(r"//.*", "", body)
        names = re.findall(r"\b(\w+)\s*\([^;{]*\)\s*;", body)
        names = [n for n in names if n not in ("if", "QT_VERSION_CHECK", "function")]
        self.assertGreater(len(names), 8, "failed to parse platform.h")
        for target in ("unx", "win"):
            src = read(ROOT / f"arch/{target}/platform.cpp")
            missing = [n for n in dict.fromkeys(names) if not re.search(rf"\b{n}\s*\(", src)]
            self.assertEqual(missing, [], f"platform::{missing} declared in platform.h but not implemented in arch/{target}")

    def test_every_header_has_pragma_once(self):
        bad = [str(h.relative_to(ROOT)) for h in (ROOT / "include").rglob("*.h") if "#pragma once" not in read(h)]
        self.assertEqual(bad, [])

    def test_includes_resolve(self):
        """Every #include <makdown/...> must point at a header that exists."""
        bad = []
        for f in cpp_files() + list((ROOT / "include").rglob("*.h")):
            for ln, line in enumerate(read(f).splitlines(), 1):
                m = re.match(r"\s*#\s*include\s*<(makdown/[^>]+)>", line)
                if m and not (ROOT / "include" / m.group(1)).exists():
                    bad.append(f"{f.relative_to(ROOT)}:{ln}: {m.group(1)}")
        self.assertEqual(bad, [])

    def test_headers_are_self_contained(self):
        """Each public header must compile on its own (catches missing includes / forward declarations)."""
        if not (shutil_which("g++") and shutil_which("pkg-config")):
            self.skipTest("no compiler")
        from lib.common import pkg_exists, qt_flags
        if not pkg_exists("Qt6Widgets"):
            self.skipTest("Qt6Widgets not installed")
        cflags, _ = qt_flags("Qt6Widgets")
        failures = []
        for h in sorted((ROOT / "include/makdown").rglob("*.h")):
            rel = h.relative_to(ROOT / "include").as_posix()
            p = subprocess.run(["g++", "-std=c++17", "-fPIC", "-fsyntax-only", "-x", "c++", "-", f"-I{ROOT/'include'}", *cflags],
                               input=f'#include <{rel}>\n', text=True, capture_output=True)
            if p.returncode != 0:
                failures.append(f"{rel}:\n{p.stderr[:600]}")
        self.assertEqual(failures, [], "headers that do not compile standalone")


def shutil_which(x):
    import shutil
    return shutil.which(x)


class TestSourceHygiene(unittest.TestCase):
    TEXT_EXT = {".cpp", ".h", ".md", ".sh", ".py", ".jsonc", ".json", ".rc", ".txt"}

    def text_files(self):
        for f in tracked_files():
            p = ROOT / f
            if (p.suffix in self.TEXT_EXT or p.name in ("Makefile", "LICENSE", ".gitignore")) and p.name != "appicon.cpp":
                yield p

    def test_no_merge_conflict_markers(self):
        bad = []
        for p in self.text_files():
            for ln, line in enumerate(read(p).splitlines(), 1):
                if re.match(r"(<<<<<<< |>>>>>>> |=======$)", line) and p.suffix != ".md":
                    bad.append(f"{p.relative_to(ROOT)}:{ln}")
        self.assertEqual(bad, [])

    def test_no_crlf_in_shell_scripts_and_makefiles(self):
        """CRLF in a #! script or Makefile breaks it on Linux/macOS ('/bin/sh^M: bad interpreter')."""
        bad = []
        for p in self.text_files():
            if p.suffix in (".sh", ".py") or p.name == "Makefile" or p.name == "Makefile":
                if b"\r\n" in p.read_bytes():
                    bad.append(str(p.relative_to(ROOT)))
        self.assertEqual(bad, [])

    def test_valid_utf8_everywhere(self):
        bad = []
        for p in self.text_files():
            try:
                p.read_bytes().decode("utf-8")
            except UnicodeDecodeError as e:
                bad.append(f"{p.relative_to(ROOT)}: {e}")
        self.assertEqual(bad, [])

    def test_no_tabs_in_cpp_sources(self):
        bad = [str(p.relative_to(ROOT)) for p in cpp_files() if p.name != "appicon.cpp" and "\t" in read(p)]
        self.assertEqual(bad, [], "tab characters in C++ sources (project uses spaces)")

    def test_no_debug_leftovers(self):
        pat = re.compile(r"\b(qDebug\s*\(|std::cerr|printf\s*\(\s*\"DEBUG|FIXME|XXX_REMOVE|DO NOT COMMIT)")
        bad = []
        for p in cpp_files():
            if p.name in ("buildinfo.cpp", "appicon.cpp"):
                continue
            for ln, line in enumerate(read(p).splitlines(), 1):
                if pat.search(line) and not line.lstrip().startswith("//"):
                    bad.append(f"{p.relative_to(ROOT)}:{ln}: {line.strip()[:90]}")
        self.assertEqual(bad, [], "leftover debug output in shipped code (use mdLog, which is silent unless --debug)")

    def test_scripts_are_executable_and_have_shebang(self):
        bad = []
        for p in (ROOT / "scripts").glob("*"):
            first = p.read_bytes()[:2]
            if first != b"#!":
                bad.append(f"{p.name}: no shebang")
            if not os.access(p, os.X_OK):
                bad.append(f"{p.name}: not executable")
        self.assertEqual(bad, [])

    def test_shell_scripts_parse(self):
        for p in (ROOT / "scripts").glob("*.sh"):
            r = run(["sh", "-n", str(p)])
            self.assertEqual(r.returncode, 0, f"{p.name}: {r.stderr}")

    def test_python_scripts_compile(self):
        for p in list((ROOT / "scripts").glob("*.py")) + list((ROOT / "tests").rglob("*.py")):
            try:
                ast.parse(p.read_text(encoding="utf-8"), str(p))
            except SyntaxError as e:
                self.fail(f"{p.relative_to(ROOT)}: {e}")

    def test_no_secrets_or_junk_committed(self):
        junk_names = re.compile(r"(\.o|\.obj|\.exe|\.dll|\.so|\.dylib|\.a|\.pyc|\.swp|\.DS_Store|\.orig|\.rej)$")
        secret = re.compile(r"(-----BEGIN (RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----|ghp_[A-Za-z0-9]{30,}|AKIA[0-9A-Z]{16}|xox[baprs]-[A-Za-z0-9-]{10,})")
        bad = []
        for f in tracked_files():
            p = ROOT / f
            if f.startswith("build/") or junk_names.search(f) or p.name == "MakDown" and p.is_file():
                bad.append(f"junk tracked: {f}")
            if p.is_file() and p.stat().st_size < 3_000_000:
                try:
                    if secret.search(p.read_text(encoding="utf-8", errors="ignore")):
                        bad.append(f"possible secret in {f}")
                except OSError:
                    pass
            if p.is_file() and p.stat().st_size > 5_000_000:
                bad.append(f"large file ({p.stat().st_size // 1024} KB): {f}")
        self.assertEqual(bad, [])

    def test_gitignore_covers_build_outputs(self):
        gi = read(ROOT / ".gitignore").split()
        for needed in ("build/", "*.o", "*.exe", "*.dll"):
            self.assertIn(needed, gi, f".gitignore should list {needed}")


class TestAssets(unittest.TestCase):
    def test_icon_png_is_valid_square_rgba(self):
        data = (ROOT / "assets/icon.png").read_bytes()
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
        w, h, depth, ctype = struct.unpack(">IIBB", data[16:26])
        self.assertEqual(w, h, "icon must be square")
        self.assertGreaterEqual(w, 256)
        self.assertEqual(ctype, 6, "icon should be RGBA so transparency works")

    def test_icon_ico_header(self):
        data = (ROOT / "assets/icon.ico").read_bytes()
        reserved, typ, count = struct.unpack("<HHH", data[:6])
        self.assertEqual((reserved, typ), (0, 1), "not an ICO file")
        self.assertGreaterEqual(count, 4, "ico should carry several sizes")
        sizes = {data[6 + 16 * i] or 256 for i in range(count)}
        for s in (16, 32, 48, 256):
            self.assertIn(s, sizes, f"icon.ico lacks a {s}px image")

    def test_embedded_icon_matches_png(self):
        """core/appicon.cpp is GENERATED from assets/icon.png: they must not drift apart."""
        src = read(ROOT / "core/appicon.cpp")
        body = src.split("kIconPng[] = {", 1)[1].split("};", 1)[0]
        embedded = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", body))
        self.assertEqual(embedded, (ROOT / "assets/icon.png").read_bytes(),
                         "core/appicon.cpp is out of date - run: python3 scripts/gen-icon.py")

    def test_rc_file_references_existing_icon(self):
        rc = read(ROOT / "arch/win/makdown.rc")
        for ref in re.findall(r'"([^"]+\.ico)"', rc):
            self.assertTrue((ROOT / "arch/win" / ref).exists() or (ROOT / ref).exists(), f"makdown.rc references missing {ref}")


class TestLegal(unittest.TestCase):
    def test_license_is_mit_and_current(self):
        lic = read(ROOT / "LICENSE")
        self.assertIn("MIT License", lic)
        self.assertRegex(lic, r"Copyright \(c\) 20\d\d")

    def test_third_party_notices_cover_qt(self):
        n = read(ROOT / "THIRD_PARTY_NOTICES.md")
        self.assertIn("Qt", n)
        self.assertRegex(n, r"LGPL", "Qt LGPL compliance statement missing")

    def test_readme_license_matches(self):
        self.assertIn("MIT", read(ROOT / "README.md").split("## License", 1)[1])


class TestDocumentation(unittest.TestCase):
    DOCS = None

    @classmethod
    def setUpClass(cls):
        cls.docs = [ROOT / "README.md", ROOT / "THIRD_PARTY_NOTICES.md"] + sorted((ROOT / "Documentation").glob("*.md"))

    def test_relative_links_resolve(self):
        bad = []
        for md in self.docs:
            text = re.sub(r"```.*?```", "", read(md), flags=re.S)
            for target in re.findall(r"\]\(([^)\s]+)\)", text):
                if re.match(r"(https?:|mailto:|#)", target):
                    continue
                path = target.split("#", 1)[0]
                if path and not (md.parent / path).exists():
                    bad.append(f"{md.relative_to(ROOT)} -> {target}")
        self.assertEqual(bad, [])

    def test_in_page_anchors_resolve(self):
        bad = []
        for md in self.docs:
            text = read(md)
            used = {}
            slugs = {github_slug(h, used) for h in markdown_headings(text)}
            body = re.sub(r"```.*?```", "", text, flags=re.S)
            for a in re.findall(r"\]\(#([^)\s]+)\)", body):
                if a not in slugs:
                    bad.append(f"{md.relative_to(ROOT)}: #{a}")
        self.assertEqual(bad, [], "broken in-page anchors (table of contents?)")

    def test_cross_file_anchors_resolve(self):
        bad = []
        for md in self.docs:
            body = re.sub(r"```.*?```", "", read(md), flags=re.S)
            for path, anchor in re.findall(r"\]\(([^)#\s]+\.md)#([^)\s]+)\)", body):
                target = md.parent / path
                if target.exists():
                    used = {}
                    slugs = {github_slug(h, used) for h in markdown_headings(read(target))}
                    if anchor not in slugs:
                        bad.append(f"{md.relative_to(ROOT)} -> {path}#{anchor}")
        self.assertEqual(bad, [])

    def test_documentation_index_lists_every_doc(self):
        readme = read(ROOT / "README.md")
        for d in (ROOT / "Documentation").glob("*.md"):
            self.assertIn(f"Documentation/{d.name}", readme, f"{d.name} is not linked from the README documentation index")

    def test_code_fences_balanced(self):
        bad = []
        for md in self.docs:
            if len(re.findall(r"^\s*```", read(md), re.M)) % 2:
                bad.append(str(md.relative_to(ROOT)))
        self.assertEqual(bad, [], "unbalanced ``` fences swallow the rest of the page")

    def test_project_layout_in_readme_matches_tree(self):
        readme = read(ROOT / "README.md")
        layout = readme.split("## Project layout", 1)[1].split("```", 2)[1]
        mentioned = set(re.findall(r"([\w\-]+\.(?:cpp|h|sh|py|md|png|ico))", layout))
        existing = {p.name for p in ROOT.rglob("*") if p.is_file() and ".git" not in p.parts and "build" not in p.parts}
        missing = sorted(mentioned - existing)
        self.assertEqual(missing, [], f"README project layout mentions files that don't exist: {missing}")
        unlisted = sorted(p.name for d in ("core", "ui") for p in (ROOT / d).glob("*.cpp") if p.name not in layout)
        self.assertEqual(unlisted, [], f"source files missing from README project layout: {unlisted}")

    def test_hotkeys_in_readme_are_in_hotkeys_doc(self):
        readme = read(ROOT / "README.md").split("## Hotkeys", 1)[1].split("---", 1)[0]
        doc = read(ROOT / "Documentation/hotkeys.md")
        norm = lambda s: re.sub(r"\s+", "", s.replace("`", "")).lower()
        docn = norm(doc)
        missing = []
        for key in re.findall(r"^\|\s*`([^`]+)`\s*\|", readme, re.M):
            for alt in key.split(" / "):
                alt = alt.strip()
                if alt in ("Key",):
                    continue
                if norm(alt) not in docn and not re.search(r"ctrl\+(\w/)+\w", norm(alt)):
                    missing.append(alt)
        self.assertEqual(missing, [], "README hotkeys that Documentation/hotkeys.md does not mention")

    def test_documented_make_variables_exist_in_makefile(self):
        mk = read(ROOT / "Makefile")
        for var in ("TARGET", "QT", "CXX", "CROSS", "PKG_CONFIG", "WINDRES", "MOC", "WEBENGINE", "MULTIMEDIA"):
            self.assertRegex(mk, rf"\b{var}\b", f"README documents make variable {var} but the Makefile never uses it")

    def test_documented_cli_flags_exist_in_main(self):
        main = read(ROOT / "init/main.cpp")
        for flag in ("--debug", "-d", "--native-frame", "--version", "--help", "-h"):
            self.assertIn(f'"{flag}"', main, f"flag {flag} is documented but not parsed")


class TestVersionConsistency(unittest.TestCase):
    def test_version_strings_agree(self):
        main = read(ROOT / "init/main.cpp")
        puts = re.search(r'puts\("MakDown ([\d.]+[\w.\-]*)"\)', main)
        appv = re.search(r'setApplicationVersion\("([^"]+)"\)', main)
        self.assertTrue(puts and appv, "could not find the version strings in init/main.cpp")
        self.assertEqual(puts.group(1), appv.group(1), "--version output and QApplication version differ")
        rc = ROOT / "arch/win/makdown.rc"
        if rc.exists():
            rct = read(rc)
            for m in re.finditer(r'VALUE\s+"(?:File|Product)Version",\s*"([\d., ]+)"', rct):
                self.assertEqual(m.group(1).replace(",", ".").replace(" ", "").rstrip(".0") or "0",
                                 appv.group(1).rstrip(".0") or "0", "version in makdown.rc differs from main.cpp")

    def test_about_dialog_mentions_same_version_if_any(self):
        main = read(ROOT / "init/main.cpp")
        v = re.search(r'setApplicationVersion\("([^"]+)"\)', main).group(1)
        for m in re.finditer(r"MakDown\s+(\d+\.\d+(?:\.\d+)?)", read(ROOT / "ui/mainwindow.cpp")):
            self.assertEqual(m.group(1), v, "a different version is hard-coded in ui/mainwindow.cpp")


if __name__ == "__main__":
    unittest.main(verbosity=2)
