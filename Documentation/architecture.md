# Source tree

    MakDown/
    ├── Makefile               top-level build (kbuild-style, collects obj-y from subdirs)
    ├── README.md
    ├── LICENSE                MIT
    ├── init/                  program entry point (main.cpp)
    ├── core/                  UI-independent logic
    │   ├── theme.cpp          presets on disk (.jsonc), stock palettes, QSS generator, AppSettings
    │   ├── markdown.cpp       markdown -> rich-text renderer
    │   ├── syntax.cpp         syntax packs: load / match (<home>/syntax/*.json)
    │   ├── syntaxstock.cpp    the stock packs (c, cpp, makefile, ld, rust, bash, batch, html, python, css, js) as JSON strings
    │   ├── search.cpp         search helpers: regex builder, file walker, per-file search, fuzzy score, globs
    │   ├── anim.cpp           the global animation level (Settings > Animations)
    │   ├── appicon.cpp        GENERATED: assets/icon.png embedded (scripts/gen-icon.py)
    │   └── buildinfo.cpp      compiler / date / arch info, debug logging
    ├── ui/                    widgets
    │   ├── editor.cpp         editor, gutter, markdown highlighter, vim engine
    │   ├── synhl.cpp          generic highlighter that runs a syntax pack
    │   ├── mainwindow.cpp     window, themed chrome, menus, files, command line
    │   ├── sidebar.cpp        left panel: tab rail (Files / Search / Git) + pages
    │   ├── explorer.cpp       folder tree on the left: open, new, rename, delete, preview
    │   ├── mediatab.cpp       image / audio preview tab (checkerboard, zoom, GIF; QtMultimedia player)
    │   ├── console.cpp        console panel: user's shell in a pty (script), ANSI colours
    │   ├── findbar.cpp        VS Code style find / replace bar (vim keys off)
    │   ├── searchtab.cpp      "find in files" tab (worker thread, results tree)
    │   ├── quickopen.cpp      "go to file" panel (Ctrl+P) (worker thread, fuzzy filter)
    │   ├── gittab.cpp         Git tab: clone over http(s) / ssh with the system git (QProcess, progress bar, log)
    │   ├── fx.cpp             animation helpers (fade, tween, cross-fade, smooth scroll)
    │   ├── titlebar.cpp       custom window frame widgets: title bar, caption buttons, themed border (Windows)
    │   ├── browsertab.cpp     browser tab + live reload (QtWebEngine, optional: behind MAKDOWN_WEBENGINE)
    │   └── settingsdialog.cpp settings + custom theme editor
    ├── arch/
    │   ├── unx/platform.cpp   Linux/macOS: paths (custom-frame hooks are no-ops)
    │   └── win/               platform.cpp (paths, debug console, custom window frame via Win32/DWM), makdown.rc (exe icon)
    ├── include/makdown/       public headers, mirrors the source dirs (core/ ui/ arch/)
    ├── assets/                icon.png (source), icon.ico (Windows exe icon)
    ├── Documentation/         building, presets, syntax, hotkeys, example preset
    └── scripts/               helper scripts (check-deps.sh, deploy-win.sh)

Rule of thumb: anything OS-specific goes behind `include/makdown/arch/platform.h`; `core/` never includes `ui/`.
