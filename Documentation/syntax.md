# Syntax highlighting

Source files opened in the editor are coloured by **syntax packs**: one `.json` file per language in the
`syntax/` folder next to `presets/`:

* Linux / macOS: `$HOME/.local/share/MakDown/syntax`
* Windows: `%USERPROFILE%\AppData\Local\MakDown\syntax`

Stock packs are written there on first launch: **C** (`.c` `.h`), **C++** (`.cpp` `.cc` `.cxx` `.hpp` `.hh` ...), **Makefile**
(`Makefile`, `GNUmakefile`, `.mk`), **Linker script** (`.ld` `.lds`), **Rust** (`.rs`), **Bash** (`.sh` `.bash`, `.bashrc` ...) **Batch** (`.bat` `.cmd`), **HTML / XML** (`.html` `.htm` `.xml` `.svg` ...) **Python** (`.py` `.pyw` `.pyi`), **CSS** (`.css`) and **JavaScript / TypeScript** (`.js` `.mjs` `.jsx` `.ts` `.tsx`). Packs are read at launch. Markdown files keep their own highlighting.

A stock pack with `"builtin": true` is refreshed automatically when MakDown ships a newer `revision`. To keep your edits, set
`"builtin": false` (or copy it under another name). To add a language, drop a new `.json` in the folder.
`MakDown --debug` lists the folder and logs packs that failed to load or contain a broken regex.

## Format

Plain JSON (`//` comments and trailing commas are tolerated). Every regex is a PCRE pattern (Qt's `QRegularExpression`), so every
backslash is doubled in JSON: `"\\bfoo\\b"`.

```json
{
  "name": "Example",
  "revision": 1,
  "builtin": false,
  "extensions": ["ex", "exm"],
  "filenames": ["Examplefile"],
  "caseSensitive": true,

  "colors": { "keyword": "#ff6600" },

  "words": {
    "keyword": ["if", "else", "while"],
    "type":    ["int", "char"]
  },

  "regions": [
    { "scope": "comment", "begin": "/\\*", "end": "\\*/" },
    { "scope": "string",  "begin": "\"", "end": "\"", "escape": "\\", "multiline": false }
  ],

  "rules": [
    { "scope": "comment", "pattern": "//.*" },
    { "scope": "number",  "pattern": "\\b\\d+\\b" }
  ]
}
```

| key | meaning |
|---|---|
| `extensions` / `filenames` | which files use the pack (no dot, case-insensitive). Exact file names win over extensions. |
| `caseSensitive` | default `true`; `false` for languages like Batch |
| `colors` | optional per-scope colour override, `"#rrggbb"` |
| `words` | `scope -> [words]`: whole-word matches (the words are escaped for you) |
| `regions` | things that can span lines: `begin`, `end` (regex), optional `escape` (a literal that skips the next character, e.g. `"\\"`), `multiline` (default `true`; `false` ends the region at the end of the line) |
| `rules` | single-line regexes: `scope` + `pattern` |

How a line is coloured: from the current position the **earliest** match wins; on a tie, regions come first, then `words`, then `rules`
in file order. A match is coloured and scanning continues behind it. Empty matches are ignored.

## Scopes

`keyword` `type` `constant` `builtin` `function` `variable` `string` `escape` `number` `comment` `preprocessor` `attribute` `macro` `label`

Colours come from the active theme: `comment` uses the theme's muted colour, `keyword` its accent colour (when it is readable on the
editor background), the others are fixed light / dark colours picked by the theme's `dark` flag. Use `colors` in a pack to override.
Unknown scope names are allowed - they are simply not coloured unless the pack gives them a colour in `colors`.
