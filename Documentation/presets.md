# Presets (themes)

Stock and custom themes are `.jsonc` files (JSON + `//` comments):

* Linux / macOS: `$HOME/.local/share/MakDown/presets`
* Windows: `%USERPROFILE%\AppData\Local\MakDown\presets`

Stock presets (**MakDown** and MakDown Dark - the default look, GitHub, Windows 11, Warm (each with a Dark variant), VS Code Light/Dark, Glass Light/Dark, TTY, opencode, Gruvbox, Nord, Catppuccin) are written there on first launch and are
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

`"chrome"` picks the overall app layout: `makdown`, `github`, `win11`, `terminal`, `warm`, `vscode`, `glass` or `plain`.

* `makdown` - **the MakDown style** (stock: *MakDown*, *MakDown Dark*), described below.

* `vscode` - editor-tab style headers, blue status bar, flat selection colours (stock: *VS Code Light*, *VS Code Dark*).
* `glass` - **Glass**: bars, rail, tabs, buttons and reading surfaces become capsule-shaped translucent tiles with a
  diagonal specular sheen and a light rim; active controls get an accent-tinted glossy fill. No backdrop is painted - the
  plain `bg` colour shows between the tiles. Set `"dark": true` for the smoked-glass variant, `cornerRadius` for rounder/squarer tiles.
  Stock: *Glass Light*, *Glass Dark*. Experimental, may look ugly.

## The MakDown style

MakDown's own look is meant for long sessions of writing: calm, flat and quiet. It is built from a few rules.

* **Paper and ink, not white and black.** *MakDown* is a warm paper background (`#f3f0e8`) with a lighter page for the editor and preview and
  blue-black ink (`#2a2d32`) for text. *MakDown Dark* is a soft charcoal (`#202427`) with warm off-white text (`#d8d5cc`).
  Body text sits at roughly 10-12:1 contrast in both: easy to read without the glare of maximum contrast. Muted text and links stay above WCAG AA (4.5:1).
* **One accent.** Pine green (`#2f6f5e`) on paper, sage (`#86bba4`) on charcoal: for the primary button, focus rings, the active tab underline and the checkbox.
  Links are a separate, quieter blue; code text is brick (light) or sand (dark). Nothing is neon and there are no gradients, glows or translucency.
* **Soft selection.** Selected rows, menu items and the active view button use a pale tint of the accent with normal ink text, instead of a solid block of colour.
* **A book page for reading.** The preview uses a reading serif (Source Serif 4, Charter, Georgia, Noto Serif), looser lines (140%), more air between
  paragraphs and above headings, and a 740 px column (about 70 characters). The interface and the editor stay in a plain sans / monospace.
* **Quiet structure.** 6 px corners, 1 px hairline borders, no heading rules, an unobtrusive 11 px scrollbar and a masthead that shows the name in the reading serif.

It is the default for new installs; existing settings keep their theme. Clone *MakDown* in Settings to adjust any colour while keeping the style.
