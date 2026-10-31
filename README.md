# MakDown

A Markdown editor with live preview, but fancier. Written in **C++17 / Qt 6 Widgets**, built with a plain **kbuild-style Makefile** (CMake or qmake not supported).

Edit on the left, see the rendered result on the right, and get a file explorer, project search, git clone, image/audio preview, an in-app browser with live reload and a console as bonuses. Themeable down to the last colour.

```
make unx      # Linux / macOS / BSD
make win      # Windows (MSYS2/MinGW)
make debug    # build + run with the compiler / build-date / arch banner
```

---

## Table of contents

- [Features](#features)
- [Quick start](#quick-start)
- [Building](#building)
- [Command line](#command-line)
- [Usage guide](#usage-guide)
- [Hotkeys](#hotkeys)
- [Themes and presets](#themes-and-presets)
- [Syntax highlighting packs](#syntax-highlighting-packs)
- [Interface languages](#interface-languages)
- [Configuration and data locations](#configuration-and-data-locations)
- [Project layout](#project-layout)
- [Tests](#tests)
- [Documentation index](#documentation-index)
- [Limitations](#limitations)
- [License](#license)

---

## Features

### Editing and preview
- Markdown editor with a **live rich-text preview**. View modes: editor only, split, preview only (`Ctrl+1/2/3`).
- Renders headings, paragraphs, **bold**, *italic*, ~~strikethrough~~, `inline code`, links, images, bullet / numbered / nested / **task lists**, nestable blockquotes, fenced code blocks (``` and ~~~), **tables** (with alignment) and horizontal rules.
- **Anchor links**: every heading gets a GitHub-style anchor (`## My Section` -> `#my-section`, duplicates become `-1`, `-2`). Click `[jump](#my-section)` in the preview to scroll there; `other.md#section` opens the file and jumps to the heading.
- Formatting shortcuts: bold, italic, strike, inline code, link, heading level up/down, bullet, task, quote.
- **Reveal-markers preview** (`Ctrl+Shift+M`) shows the hidden Markdown markers in the rendered output.
- **Word-style paragraph marks** (¶, `Ctrl+Shift+L`), optional visible spaces/tabs, line numbers.
- Status bar with line / column and word count.
- **Export to HTML** (File > Export HTML...) using the active theme's preview CSS.
- VS Code style **find / replace bar**: match case, whole word, regex (`$1`..`$9`, `$&`, `$$` in replacements), "3 of 12" counter, replace-all as one undo step.

### Vim mode
Optional built-in vim engine (`Ctrl+Alt+V`):
- Motions: `h j k l w b e 0 ^ $ gg G { }`
- Insert: `i a I A o O`
- Edit: `x X s D C dd yy cc J ~ r >> <<`
- Operators `d` / `c` / `y` + motion, text objects like `diw`, counts (`3dd`)
- `p P u Ctrl-R`, visual `v` / `V`, `/search`, `n` / `N`
- Ex commands: `:w :q :q! :wq :42 :set vim|novim :set nu|nonu :set list|nolist :theme Nord`

### Side bar (Files / Search / Git)
- **File explorer**: open folder, new file/folder, **rename (F2)**, **delete (Del, moves to trash)**, open in browser tab, show in file manager. Drag a folder onto the window to change the root. The root is remembered between runs.
- **Go to file** (`Ctrl+P`): fuzzy search over every file in the working folder, indexed in a background thread. `main.cpp:120` jumps straight to a line.
- **Find in files** (`Ctrl+Shift+F`): match case / whole word / regex, include / exclude globs, results grouped per file, runs on a worker thread.
- **Git clone** (`Ctrl+Shift+G`): clone over http(s) or ssh with the system `git`, progress bar, live log, cancel button, then open the result as the explorer root.

### Browser tab with live reload
- Opens `index.html` (or any page, folder, `file://`, `http(s)://`, `localhost:3000`) next to the preview.
- **Live reload**: watches the page's folder and every local file the page actually loaded (`.html .htm .css .js .mjs .json .svg` + images/fonts). Debounced at 200 ms, cache bypassed.
- Powered by **QtWebEngine**, which is **optional** at build time. Without it, pages open in the system browser.

### Media preview
- **Images** (png, jpg, gif, bmp, webp, ico, tiff, svg ...): checkerboard for transparency, fit / 100% / zoom, Ctrl+wheel, drag to pan, animated GIF/WebP.
- **Audio** (mp3, wav, ogg, opus, flac, m4a, aac ...): play/pause, seek, volume, mute, repeat, cover art and title/artist metadata. Needs **QtMultimedia** (optional; otherwise handed to the system player).

### Console panel
- `Ctrl+\`` opens a panel running your **default shell** as a login + interactive shell in the explorer folder (`cmd.exe` on Windows).
- Pty-backed on Linux/macOS (through `script`) so colours, job control and `Ctrl+C` work. 16-colour ANSI, history, tab completion, `Ctrl+L` to clear.
- It is a **line console, not a terminal emulator**: no vim/top/less.
- `MAKDOWN=1` is exported to the shell so your rc files can detect it.

### Syntax highlighting for source files
Editable JSON packs for **C, C++, Makefile, linker scripts, Rust, Bash, Batch, HTML/XML, Python, CSS, JavaScript/TypeScript**. Add your own language by dropping a `.json` file in a folder.

### Themes
Stock presets: **GitHub, Windows 11, Warm** (each with a Dark variant), **VS Code Light/Dark**, **Glass Light/Dark**, **TTY, opencode, Gruvbox, Nord, Catppuccin**. Create your own as `.jsonc` files or with the in-app theme editor.

### Interface languages
**English, Russian and Polish** out of the box, picked in Settings > Language (default: follow the OS language). Languages are plain `.json` files, so adding another one means copying `en.json` and translating it. See [`languages.md`](Documentation/languages.md).

### Custom window frame (Windows)
MakDown draws its own **title bar** (icon, menus, minimise / maximise / close) and a **themed window border** that changes between focused and unfocused. Resizing, Aero Snap, the shadow and Windows 11 rounded corners stay native. Toggle in Settings > Appearance, or run with `--native-frame`. See [`window-frame.md`](Documentation/window-frame.md).

### Animations
Four levels in Settings > Animations: *Don't distract me*, *Just a little bit*, *Normal*, *Entertain me*. Covers fades, sliding panels, theme cross-fades, springy motion, smooth wheel scrolling and a dancing equalizer in the audio preview.

---

## Quick start

```bash
git clone https://github.com/nottwm/MakDown MakDown
cd MakDown
./scripts/check-deps.sh      # sanity-check your toolchain
make unx
./build/unx/MakDown README.md
```

Try these first:
1. `Ctrl+2` for the split view.
2. `Ctrl+,` to open Settings and pick a theme.
3. `Ctrl+Shift+O` to open a project folder, then `Ctrl+P` to jump to a file.
4. `Ctrl+Shift+B` on an `.html` file to preview it with live reload.

---

## Building

**Requirements:** C++17 compiler, GNU make, pkg-config, Qt 6 Widgets (Qt 5.15 works with `make QT=Qt5`).

| OS | Install |
|---|---|
| Debian / Ubuntu | `sudo apt install build-essential pkg-config qt6-base-dev` |
| Fedora | `sudo dnf install gcc-c++ make pkgconf qt6-qtbase-devel` |
| macOS | `brew install qt pkg-config` then `export PKG_CONFIG_PATH="$(brew --prefix qt)/lib/pkgconfig"` |
| Windows (MSYS2 UCRT64) | `pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-qt6-base make` |

### Make targets

| Target | What it does |
|---|---|
| `make unx` | Build for Linux / macOS / BSD |
| `make win` | Build for Windows (MSYS2, or cross-compile with MinGW) |
| `make deploy` | Windows build + copy all required DLLs and plugins next to the exe |
| `make run` | Build and launch |
| `make debug` | Build and launch with `--debug` |
| `make clean` | Remove `build/<target>` |
| `make mrproper` | Remove `build/` entirely |
| `make help` | Show help |

Output goes to `build/unx/MakDown` or `build/win/MakDown.exe`.

### Make variables

| Variable | Values | Purpose |
|---|---|---|
| `TARGET` | `unx` \| `win` | Target platform |
| `QT` | `Qt6` \| `Qt5` | Qt major version |
| `CXX`, `CROSS`, `PKG_CONFIG`, `WINDRES` | - | Override the toolchain |
| `MOC` | path | Point to `moc` if it is not found (e.g. `/usr/lib/qt6/libexec/moc`) |
| `WEBENGINE` | `auto` \| `yes` \| `no` | Browser tab via QtWebEngine |
| `MULTIMEDIA` | `auto` \| `yes` \| `no` | Audio preview via QtMultimedia (Qt6 only) |
| `V=1` | - | Show full compiler command lines |

### Optional modules

| Feature | Debian/Ubuntu package | Without it |
|---|---|---|
| Browser tab (QtWebEngine) | `qt6-webengine-dev` | "Open in browser tab" opens the system browser |
| Audio preview (QtMultimedia) | `qt6-multimedia-dev` | Sound files open in the system player |

`auto` (default) enables each one if `pkg-config` finds it. Changing a setting triggers a full rebuild automatically. Check **Help > About** or run `MakDown --debug` to see what got compiled in.

### Windows notes
- **Cross-compiling from Linux/WSL:** `make win` picks `x86_64-w64-mingw32-g++` automatically. An inherited `CXX=clang++` is ignored for `TARGET=win` unless you pass it on the make command line.
- **Missing DLL errors** (`Qt6Core.dll`, `libssp-0.dll`, ...): run `make deploy`. It copies every needed DLL (via `objdump`), `platforms/qwindows.dll` and image plugins into `build/win/`, and runs `windeployqt6` when WebEngine is in use. Custom DLL location: `SEARCH="/path/to/bin" make deploy`.
- **WSL with a blank/crashing browser tab:** `QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu ./build/unx/MakDown`.

### App icon
`assets/icon.png` is embedded in the binary (`core/appicon.cpp`). On Windows, `arch/win/makdown.rc` is compiled with `windres` so the `.exe` carries `assets/icon.ico`. After changing the PNG, run `python3 scripts/gen-icon.py` (needs Pillow) to regenerate both.

Full details: [`Documentation/building.md`](Documentation/building.md).

---

## Command line

```
makdown [file] [--debug] [--native-frame] [--version] [--help]
```

| Flag | Meaning |
|---|---|
| `file` | Open this file on start (markdown, source, image, sound, html ...) |
| `--debug`, `-d` | Print compiler / build date / architecture and verbose logs (opens a console on Windows) |
| `--native-frame` | Windows: keep the OS title bar and border instead of MakDown's themed ones |
| `--version` | Print version |
| `--help`, `-h` | Show usage |

---

## Usage guide

### Opening things
- File > Open, drag and drop, or pass a path on the command line.
- Markdown and text open in the editor. Images and sound open in a preview tab. Binaries and PDFs go to the system default app. Files over 20 MB are refused.
- `.html`, `.js`, `.css` open in the editor with Markdown colouring switched off.

### Working with a project
1. `Ctrl+Shift+O` to choose a folder. It becomes the explorer root, the search root and the console's start directory.
2. `Ctrl+P` for fuzzy file jump, `Ctrl+Shift+F` for text search across files.
3. Right-click in the explorer for rename, delete, new file/folder, open in browser tab.

### Previewing a web page
`Ctrl+Shift+B` opens, in order: the current file if it is `.html/.htm/.xhtml/.svg`, else the explorer folder's `index.html`, else a file dialog. `Ctrl+T` opens an empty browser tab with an address bar. Save in the editor and the page reloads (unsaved edits are not shown because the browser reads from disk). The **Live** toggle switches reloading off globally.

### Searching
- **In the current file:** `Ctrl+F` / `Ctrl+H`, `Enter` / `Shift+Enter` or `F3` / `Shift+F3`. Toggles: `Alt+C` case, `Alt+W` whole word, `Alt+R` regex. `Ctrl+Alt+Enter` replaces all.
- **In files:** results are searched on disk (unsaved edits are not seen), binary files and files over 2 MB are skipped, hidden / `node_modules` / `__pycache__` folders are skipped, and results stop at 5000.

### Cloning a repo
`Ctrl+Shift+G`, paste an `https://`, `http://`, `ssh://` or `git@host:user/repo.git` URL, pick the destination, **Clone**. There are no terminal prompts: private https repos need a credential helper or a token in the URL (masked in the log), ssh needs an agent-usable key. ssh runs with `BatchMode=yes` and `StrictHostKeyChecking=accept-new`; set `GIT_SSH_COMMAND` to change that.

---

## Hotkeys

| Key | Action |
|---|---|
| `Ctrl+B / I / E / K` | Bold / italic / inline code / link |
| `Ctrl+Shift+X / 8 / T / 9` | Strike / bullet / task / quote |
| `Ctrl+Alt+Up/Down` | Heading level up / down |
| `Ctrl+1 / 2 / 3` | Editor / split / preview |
| `Ctrl+Shift+L` | Paragraph marks (¶) |
| `Ctrl+Shift+M` | Reveal Markdown markers in preview |
| `Ctrl+Alt+V` | Toggle vim keys |
| `Ctrl+,` | Settings |
| `Ctrl+Shift+O` | Open folder |
| `Ctrl+Shift+E` | Show / hide side bar |
| `Ctrl+Shift+B` | Open page in browser tab |
| `Ctrl+T` | New browser tab |
| `Ctrl+F / Ctrl+H` | Find / replace (with vim on, `Ctrl+F` opens `/`) |
| `F3 / Shift+F3` | Next / previous match |
| `Ctrl+P` | Go to file |
| `Ctrl+Shift+F` | Find in files |
| `Ctrl+Shift+G` | Git clone tab |
| `Ctrl+\`` | Show / hide console |
| `F2 / Del` | Rename / delete in explorer |
| `F5` | Refresh preview |
| `F11` | Full screen |

Vim extras: `Alt+Up/Down` move line, `Ctrl+Shift+D` duplicate line, `Ctrl+D/U` half page. Full list: [`Documentation/hotkeys.md`](Documentation/hotkeys.md).

---

## Themes and presets

Themes are `.jsonc` files (JSON with `//` comments). Stock presets are written to the presets folder on first launch and are read-only in the UI. Use **Settings > Appearance > Clone... / New...** to make editable ones, or copy [`Documentation/presets/example-custom.jsonc`](Documentation/presets/example-custom.jsonc).

You can customise:
- Colours (missing keys are derived automatically)
- Fonts and sizes (UI, editor, preview, code)
- UI corner radius, code-block style, heading rules, document page width
- **Chrome flavour** (overall app layout): `github`, `win11`, `terminal`, `warm`, `vscode`, `glass`, `plain`

Minimal example:

```jsonc
{
  "name": "Example Sunset",
  "builtin": false,
  "dark": true,
  "chrome": "plain",
  "colors": { "bg": "#1b1423", "fg": "#f2e9f7", "accent": "#ff8a5b" },
  "fonts":  { "editor": "JetBrains Mono, monospace", "editorSize": 11 },
  "shape":  { "cornerRadius": 12, "pageWidth": 820 }
}
```

Notes:
- Untouched stock presets auto-refresh when the app ships a newer `revision`. To keep your edits, clone the preset or set `"builtin": false`.
- *Warm* themes use the **Anthropic Serif** font. It is not bundled; install it or MakDown falls back to Tiempos Text / Georgia / any serif.
- *Glass* renders translucent capsule tiles with a specular sheen. `"dark": true` gives the smoked-glass variant.

More: [`Documentation/presets.md`](Documentation/presets.md).

---

## Syntax highlighting packs

One `.json` file per language. Stock packs are written on first launch; add a file to add a language.

```json
{
  "name": "Example",
  "revision": 1,
  "builtin": false,
  "extensions": ["ex"],
  "filenames": ["Examplefile"],
  "caseSensitive": true,
  "words":   { "keyword": ["if", "else"], "type": ["int"] },
  "regions": [{ "scope": "comment", "begin": "/\\*", "end": "\\*/" }],
  "rules":   [{ "scope": "number", "pattern": "\\b\\d+\\b" }]
}
```

- Regexes are PCRE (Qt `QRegularExpression`), so backslashes are doubled in JSON.
- Scopes: `keyword type constant builtin function variable string escape number comment preprocessor attribute macro label`.
- Colours come from the active theme; override per pack with `"colors"`.
- Packs load at launch. `MakDown --debug` logs packs that fail to load or contain a broken regex.

More: [`Documentation/syntax.md`](Documentation/syntax.md).

---

## Interface languages

Every menu, dialog, tooltip and message is looked up in a language file: `<home>/lang/*.json`. `en.json`, `ru.json` and `pl.json` are written on first launch. **Settings > Language** switches (after a restart); *System default* follows the OS language. To add one, copy `en.json`, rename it (`de.json`), translate the values and restart; missing keys fall back to English.

```json
{ "name": "Polski", "plural": "polish", "strings": { "menu.file": "&Plik", "status.words": ["%n słowo", "%n słowa", "%n słów"] } }
```

More: [`Documentation/languages.md`](Documentation/languages.md).

---

## Configuration and data locations

| | Linux / macOS | Windows |
|---|---|---|
| App home | `$HOME/.local/share/MakDown` | `%USERPROFILE%\AppData\Local\MakDown` |
| Themes | `<home>/presets/*.jsonc` | `<home>\presets\*.jsonc` |
| Syntax packs | `<home>/syntax/*.json` | `<home>\syntax\*.json` |
| Languages | `<home>/lang/*.json` | `<home>\lang\*.json` |
| Settings | `<home>/settings.jsonc` | `<home>\settings.jsonc` |

Settings remember the explorer root, console visibility, view mode, theme, animation level, vim on/off, live reload and more.

---

## Project layout

```
MakDown/
├── Makefile               top-level build (kbuild-style, collects obj-y from subdirs)
├── init/                  program entry point (main.cpp)
├── core/                  UI-independent logic
│   ├── theme.cpp          presets on disk, stock palettes, QSS generator, AppSettings
│   ├── markdown.cpp       Markdown -> rich-text renderer
│   ├── syntax.cpp         syntax packs: load / match
│   ├── syntaxstock.cpp    stock packs as JSON strings
│   ├── lang.cpp           interface languages: load, lookup, plural rules
│   ├── langstock.cpp      GENERATED from lang/*.json (scripts/gen-lang.py)
│   ├── search.cpp         regex builder, file walker, fuzzy score, globs
│   ├── anim.cpp           global animation level
│   ├── appicon.cpp        GENERATED from assets/icon.png (scripts/gen-icon.py)
│   └── buildinfo.cpp      compiler / date / arch info, debug logging
├── ui/                    widgets
│   ├── mainwindow.cpp     window, themed chrome, menus, files, command line
│   ├── editor.cpp         editor, gutter, Markdown highlighter, vim engine
│   ├── synhl.cpp          generic highlighter that runs a syntax pack
│   ├── sidebar.cpp        left rail (Files / Search / Git)
│   ├── explorer.cpp       folder tree
│   ├── searchtab.cpp      find in files (worker thread)
│   ├── quickopen.cpp      go to file (worker thread, fuzzy)
│   ├── gittab.cpp         git clone via QProcess
│   ├── browsertab.cpp     browser tab + live reload (QtWebEngine, optional)
│   ├── mediatab.cpp       image / audio preview
│   ├── console.cpp        shell console in a pty
│   ├── findbar.cpp        find / replace bar
│   ├── fx.cpp             animation helpers
│   ├── titlebar.cpp       custom window frame: title bar, caption buttons, border
│   └── settingsdialog.cpp settings + custom theme editor
├── arch/
│   ├── unx/               Linux/macOS platform code
│   └── win/               Windows platform code + exe icon resource
├── include/makdown/       public headers, mirrors core/ ui/ arch/
├── lang/                  stock interface languages (en, ru, pl) as .json
├── assets/                icon.png, icon.ico
├── Documentation/         per-feature docs + example preset
└── scripts/               check-deps.sh, deploy-win.sh, gen-icon.py, gen-lang.py
```

Design rules:
- Anything OS-specific goes behind `include/makdown/arch/platform.h`.
- `core/` never includes `ui/`.
- Each directory contributes objects through its own `Makefile` fragment (`obj-y`, `moc-y`).

Details: [`Documentation/architecture.md`](Documentation/architecture.md).

---

# Tests

Tests were created to check for any problems or errors in the code, requiring a rebuild or changes in the code.
To run tests enter the following command:
```sh
make test
```

**There should be 9 expected failures.**
Expected failures are known issues in the code. Known failures:
  - `TestTables.test_KNOWN_ISSUE_single_dash_separator_cells`
  - `TestFuzz.test_KNOWN_ISSUE_paste_inside_visual_mode_leaves_stale_selection_state`
  - `TestIndentation.test_KNOWN_ISSUE_prefix_on_selection_ending_at_column_zero`
  - `TestIndentation.test_KNOWN_ISSUE_selection_ending_at_column_zero_does_not_include_next_line`
  - `TestVimEditing.test_KNOWN_ISSUE_undo_inside_visual_line_leaves_stale_selection_state`
  - `TestVimOperators.test_KNOWN_ISSUE_dG_is_not_linewise`
  - `TestVimOperators.test_KNOWN_ISSUE_dgg_is_ignored`
  - `TestVimVisual.test_KNOWN_ISSUE_visual_line_indent_touches_one_line_too_many`
  - `TestLivePreview.test_KNOWN_ISSUE_deeply_nested_blockquotes_freeze_the_preview`

---

## Documentation index

| Doc | Covers |
|---|---|
| [`building.md`](Documentation/building.md) | Dependencies, make variables, optional modules, Windows deploy, cross-compiling |
| [`presets.md`](Documentation/presets.md) | Theme files, chrome flavours, fonts |
| [`window-frame.md`](Documentation/window-frame.md) | Windows custom title bar and themed window border |
| [`syntax.md`](Documentation/syntax.md) | Syntax pack format and scopes |
| [`languages.md`](Documentation/languages.md) | Interface language files, adding a translation, plural rules |
| [`hotkeys.md`](Documentation/hotkeys.md) | All shortcuts and vim keys |
| [`search.md`](Documentation/search.md) | Find/replace, go to file, find in files |
| [`browser.md`](Documentation/browser.md) | Side bar, explorer, browser tab, live reload |
| [`git.md`](Documentation/git.md) | Clone tab |
| [`console.md`](Documentation/console.md) | Console panel |
| [`media.md`](Documentation/media.md) | Image and sound preview |
| [`architecture.md`](Documentation/architecture.md) | Source tree and conventions |

---

## Limitations

- The console is a line console, not a terminal emulator (no full-screen programs).
- Find in files reads from disk, so unsaved edits are not searched. It is line based and cannot match across lines.
- The browser tab reads files from disk, so unsaved edits are not shown until you save.
- Browser tab and audio preview need optional Qt modules; without them MakDown falls back to system apps.
- Git clone only; no commit / push / diff UI.
- The Markdown renderer targets Qt's rich-text subset, so output will not be pixel-identical to a browser-based renderer.

---

# AI

*Claude* was used to help with this project. No, **not all of the code was written by Claude**. Don't get me wrong

---

## License

MIT, (c) 2026 nottwm. See [`LICENSE`](LICENSE).

See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for Qt (LGPLv3) compliance and theme attributions.
