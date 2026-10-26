# Third-Party Notices

MakDown itself is licensed under the MIT License (see `LICENSE`). However, it relies on the following third-party software, color palettes, and specifications.

## 1. Qt Framework (LGPLv3)
MakDown uses the Qt framework (https://www.qt.io/), version 6.12.0 (or newer).
Qt is licensed under the GNU Lesser General Public License version 3 (LGPLv3).

*   **Dynamic Linking:** The official Windows release (`.zip`) links to Qt dynamically. The required Qt `.dll` files are provided in the `bin/` directory. 
*   **Source Code:** You can obtain the source code for Qt at https://download.qt.io/official_releases/qt/
*   **Modification:** Because the libraries are dynamically linked and unmodified, you are free to replace the Qt `.dll` files in this distribution with your own binary-compatible versions.

## 2. Color Palettes (MIT License)
The following built-in themes are inspired by, or derived from, open-source color palettes. The original authors retain copyright to their respective palettes.

*   **Gruvbox** (https://github.com/morhetz/gruvbox) - MIT License - Copyright (c) Pavel Pertsev
*   **Nord** (https://www.nordtheme.com/) - MIT License - Copyright (c) Arctic Ice Studio
*   **Catppuccin** (https://github.com/catppuccin/catppuccin) - MIT License - Copyright (c) the Catppuccin community
*   **opencode** (https://opencode.ai/) - MIT License
*   **GitHub Primer** (https://primer.style/) - MIT License - Copyright (c) GitHub, Inc.
*   **VS Code Dark+** (https://github.com/microsoft/vscode) - MIT License - Copyright (c) Microsoft Corporation

## 3. Syntax Highlighting Definitions
The stock syntax highlighting packs (found in `core/syntaxstock.cpp`) contain keyword lists and patterns derived from publicly available language specifications and documentation. 
*   ECMAScript (JavaScript/TypeScript) keywords are derived from the Ecma International specification.
*   CSS keywords are derived from the W3C specifications.
*   C, C++, Rust, Python, Bash, Batch, and HTML/XML keywords are derived from their respective language standards.
Language keywords and syntax rules are facts and are not subject to copyright.

## 4. Fonts
MakDown does not bundle or redistribute any proprietary font files. 
The built-in "Warm" themes reference the "Anthropic Serif" font in their fallback stack. If this font is not installed on your system, Qt will gracefully fall back to "Tiempos Text", "Georgia", or any available serif font.