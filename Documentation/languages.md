# Interface languages

MakDown's menus, dialogs, tooltips, status messages and the welcome document come from **language files**: one
`.json` file per language in `<home>/lang/` (see *Configuration and data locations* in the README). Stock languages:

| File | Language |
|---|---|
| `en.json` | English (also the built-in fallback) |
| `ru.json` | Русский |
| `pl.json` | Polski |

They are written to the folder on first launch, like themes and syntax packs.

## Choosing a language

**Settings > Language**. *System default* (the default) follows the OS language and uses English when MakDown has no
file for it. The choice is stored as `"language"` in `settings.jsonc` (`"auto"` or a file name without `.json`).
The windows are built once at start-up, so **a change applies after restarting MakDown**.

## Adding a language

1. Open **Settings > Language > Open languages folder**, copy `en.json` to e.g. `de.json`.
2. Change `name` (the language's own name, shown in the list), `plural` and translate the values in `strings`.
   Keys you leave out fall back to English, so a half-finished translation works.
3. Restart MakDown and pick it in Settings > Language.

The file name is the language code (`de`, `pt-BR`). "System default" matches the OS language against it
(`pt_BR` matches `pt-BR`, otherwise `pt`). JSONC is accepted: `//` and `/* */` comments and trailing commas.
A file that is not valid JSON is skipped (see `MakDown --debug`).

## File format

```json
{
  "name": "Polski",
  "revision": 1,
  "builtin": true,
  "plural": "polish",
  "strings": {
    "menu.file": "&Plik",
    "status.words": ["%n słowo", "%n słowa", "%n słów"],
    "status.fileNotFound": "Nie znaleziono pliku: %1"
  }
}
```

| Field | Meaning |
|---|---|
| `name` | Name shown in Settings > Language. |
| `revision`, `builtin` | For stock files: when MakDown ships a higher `revision`, a file that still says `"builtin": true` is replaced. **Set `"builtin": false` in a file you edit** and MakDown never touches it. Your own files can leave both out. |
| `plural` | Plural rule, see below. Default `one_other`. |
| `strings` | key -> text, or key -> list of plural forms. |

Rules for the texts:

- `%1`, `%2` ... are filled in by the program (file names, numbers). Keep every placeholder the English text has;
  their order may change.
- `&` in menu texts marks the Alt shortcut letter; `&&` is a literal ampersand. Keep `&&` where English has it.
- Some texts contain HTML (`<b>`, `<br>`, `&amp;`): keep the tags.
- `\n` in a text is a line break. `welcome.doc` is the Markdown document shown when MakDown starts without a file.
- Keep shortcuts such as `Ctrl+Shift+F` as they are, and keep menu names inside texts equal to your menu translations.

### Plurals

A text that depends on a number is a list of forms. `%n` becomes the number:

```json
"search.nResults": ["%n result", "%n results"]
```

The `plural` rule decides which form is used:

| Rule | Languages | Forms (in this order) |
|---|---|---|
| `one_other` | English, German, Spanish ... | 1; everything else |
| `east_slavic` | Russian, Ukrainian, Belarusian | 1, 21, 101 ...; 2-4, 22-24 ...; 0, 5-20, 25-30 ... |
| `polish` | Polish | 1; 2-4, 22-24 ... (not 12-14); everything else |
| `single` | Japanese, Chinese, Korean ... | one form for every number |

If a list is shorter than the rule needs, its last form is used.

## For developers

- Code never contains UI text: `L("menu.file")` returns the string, `Ln("status.words", n)` the plural form
  (`include/makdown/core/lang.h`). Use `.arg()` for placeholders: `L("status.saved").arg(path)`. The name `tr` is not
  used because it clashes with `QObject::tr`.
- Lookup order: active language file, then built-in English, then the key itself. English is the embedded copy of
  `lang/en.json`, so widgets created without a `MainWindow` (tests, tools) still get proper text.
- `lang/*.json` in the repository are the source files. `core/langstock.cpp` is **generated** from them
  (`python3 scripts/gen-lang.py`; the test-suite fails when it is out of date). Raise `revision` in a stock file when
  you change its texts so existing installations get the update.
- New text: add the key to `lang/en.json` **and** to every other stock language (the tests check that all stock
  languages have the same keys, the same placeholders, and that every `L("...")` in the sources exists).
- Qt's own texts (standard dialog buttons) are translated when the Qt translation files (`qtbase_<lang>.qm`) are
  installed on the system.
