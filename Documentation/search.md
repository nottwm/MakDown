# Search

Both searches live in the **side bar** (see `browser.md`): the **Search** tab, with a **Text** / **File names** switch.
"Working directory" below = the explorer root (File > Open Folder...). It is also where the console starts.

## Find / replace in the open file
* **vim keys off** (default): VS Code style bar above the editor. **Ctrl+F** find, **Ctrl+H** replace, **Enter / Shift+Enter** (or **F3 / Shift+F3**)
  next / previous, **Esc** closes. Every match is highlighted and the bar shows "3 of 12". The word under the caret (or the selection) seeds the search.
* Toggles: **Alt+C** match case, **Alt+W** whole word, **Alt+R** regular expression (`^` / `$` match per line). In regex mode the replacement
  accepts `$1`..`$9`, `$&` (whole match) and `$$`.
* Replace: **Enter** in the replace box selects the next match, then replaces it; **Ctrl+Alt+Enter** (or the button) replaces all in one undo step.
* **vim keys on**: Ctrl+F opens the `/` line as before (`n` / `N`, F3 work too); the find bar is not used.

## Go to file (Ctrl+P) - Search > File names
Fuzzy search over the names and paths of every file below the working directory (indexing runs in the background; hidden folders such as `.git`,
`node_modules` and `__pycache__` are skipped). Up / Down + Enter (or a click) opens it, Esc clears the box. `main.cpp:120` opens the file at line 120.

## Find in files (Ctrl+Shift+F) - Search > Text
Type (it searches after a short pause, or press Enter): matches are listed per file; double-click or Enter
opens the file with the match selected.
* options: match case, whole word, regex; **files to include / exclude** take comma separated globs (`*.md, src/**`, `docs/`): a pattern without
  `/` is matched against the file name, otherwise against the path relative to the working directory
* searches the files on disk (unsaved edits are not seen); skips binary files, files over 2 MB and hidden / `node_modules` / `__pycache__` folders;
  stops after 5000 results; the search is line based, so a pattern cannot span lines
* a selection in the editor seeds the query
