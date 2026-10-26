#!/usr/bin/env python3
"""Regenerates the icon files from assets/icon.png:
     assets/icon.ico        Windows exe icon (referenced by arch/win/makdown.rc)
     core/appicon.cpp       the PNG embedded as a byte array (window icon on every OS)
   usage: python3 scripts/gen-icon.py        (needs Pillow: pip install pillow)"""
import os
from PIL import Image

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
png = os.path.join(root, "assets", "icon.png")
Image.open(png).convert("RGBA").save(os.path.join(root, "assets", "icon.ico"),
                                      sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
data = open(png, "rb").read()
rows = ",\n".join("    " + ", ".join("0x%02x" % b for b in data[i:i + 16]) for i in range(0, len(data), 16))
open(os.path.join(root, "core", "appicon.cpp"), "w").write(f'''// appicon.cpp - the application icon, embedded so the program stays a single file.
// GENERATED from assets/icon.png by scripts/gen-icon.py - do not edit by hand.
#include <makdown/core/appicon.h>
#include <QIcon>
#include <QPixmap>

static const unsigned char kIconPng[] = {{
{rows},
}};

QIcon makdownIcon() {{
    static const QIcon icon = [] {{
        QPixmap pm;
        pm.loadFromData(kIconPng, sizeof(kIconPng), "PNG");
        return QIcon(pm);
    }}();
    return icon;
}}
''')
print("icon.ico + core/appicon.cpp written")
