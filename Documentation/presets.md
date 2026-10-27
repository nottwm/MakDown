# Presets (themes)

Stock and custom themes are `.jsonc` files (JSON + `//` comments):

* Linux / macOS: `$HOME/.local/share/MakDown/presets`
* Windows: `%USERPROFILE%\AppData\Local\MakDown\presets`

Stock presets (GitHub, Windows 11, Warm (each with a Dark variant), VS Code Light/Dark, Glass Light/Dark, TTY, opencode, Gruvbox, Nord, Catppuccin) are written there on first launch and are
read-only in the UI. Use Settings -> Appearance -> Clone... / New... (you must name the preset) to make editable ones.
Customisable: fonts, UI corner radius, code-block style, document width, app layout (`chrome`), all colours, window border width (Windows, see [`window-frame.md`](window-frame.md)).
See `Documentation/presets/example-custom.jsonc`. Settings live next to `presets/` in `settings.jsonc`.

## Dark variants and fonts
GitHub Dark, Windows 11 Dark and Warm dark sit next to their light versions and share the same app layout.
Warm / Warm Dark use the **Anthropic Serif** font for the interface and preview (editor and code stay monospace).
The font is not bundled - install it on your system, otherwise MakDown falls back to Tiempos Text / Georgia / any serif.
Untouched stock presets are refreshed automatically when MakDown updates them (`revision` field); to keep your own edits
of a stock preset, clone it or set `"builtin": false` in its file.

## Chrome flavours

`"chrome"` picks the overall app layout: `github`, `win11`, `terminal`, `warm`, `vscode`, `glass` or `plain`.

* `vscode` - editor-tab style headers, blue status bar, flat selection colours (stock: *VS Code Light*, *VS Code Dark*).
* `glass` - **Glass**: bars, rail, tabs, buttons and reading surfaces become capsule-shaped translucent tiles with a
  diagonal specular sheen and a light rim; active controls get an accent-tinted glossy fill. No backdrop is painted - the
  plain `bg` colour shows between the tiles. Set `"dark": true` for the smoked-glass variant, `cornerRadius` for rounder/squarer tiles.
  Stock: *Glass Light*, *Glass Dark*. Experimental, may look ugly.