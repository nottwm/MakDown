#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Regenerates core/langstock.cpp from lang/*.json (the stock interface languages).

    python3 scripts/gen-lang.py           # rewrite core/langstock.cpp
    python3 scripts/gen-lang.py --check   # exit 1 if core/langstock.cpp is out of date (used by the test-suite)

lang/*.json are the files to edit; core/langstock.cpp only embeds them so MakDown can write them to
<home>/lang/ on first launch. English (en.json) is always first: it is also the built-in fallback.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "lang"
OUT = ROOT / "core" / "langstock.cpp"
DELIM = "JSON"


def render() -> str:
    files = sorted(SRC.glob("*.json"), key=lambda p: (p.stem != "en", p.stem))
    if not files or files[0].stem != "en":
        sys.exit("lang/en.json is required")
    out = ["// langstock.cpp - GENERATED from lang/*.json by scripts/gen-lang.py. Do not edit by hand:",
           "// change the .json files and run  python3 scripts/gen-lang.py",
           "// The stock languages are written to <home>/lang/ on first launch (and refreshed when \"revision\" goes up",
           "// and the file still says \"builtin\": true). English doubles as the built-in fallback. Format: Documentation/languages.md",
           ""]
    names = []
    for i, f in enumerate(files):
        text = f.read_text(encoding="utf-8")
        json.loads(text)                                   # must be valid
        if f"){DELIM}\"" in text:
            sys.exit(f"{f.name} contains the raw-string terminator")
        var = "kLang" + re.sub(r"[^A-Za-z0-9]", "_", f.stem)
        names.append((f.name, var))
        out.append(f"static const char* const {var} = R\"{DELIM}({text.rstrip()}\n){DELIM}\";\n")
    out.append("extern const char* const kStockLangFiles[];")
    out.append("extern const char* const kStockLangJson[];")
    out.append("const char* const kStockLangFiles[] = {" + ", ".join(f'"{n}"' for n, _ in names) + ", nullptr};")
    out.append("const char* const kStockLangJson[]  = {" + ", ".join(v for _, v in names) + ", nullptr};")
    return "\n".join(out) + "\n"


def main() -> int:
    text = render()
    if "--check" in sys.argv:
        ok = OUT.exists() and OUT.read_text(encoding="utf-8") == text
        if not ok:
            print("core/langstock.cpp is out of date - run: python3 scripts/gen-lang.py")
        return 0 if ok else 1
    OUT.write_text(text, encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
