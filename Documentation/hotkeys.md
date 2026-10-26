# Hotkeys

| Key | Action |
|---|---|
| Ctrl+B / I / E / K | bold / italic / inline code / link |
| Ctrl+Shift+X / 8 / T / 9 | strike / bullet / task / quote |
| Ctrl+Alt+Up/Down | heading level up/down |
| Ctrl+1 / 2 / 3 | editor / split / preview |
| Ctrl+Shift+L | paragraph marks (¶) |
| Ctrl+Shift+M | reveal hidden markdown markers in preview |
| Ctrl+Alt+V | toggle vim keys |
| Ctrl+, | settings |
| Ctrl+Shift+O | open folder (explorer root) |
| Ctrl+Shift+E | show / hide the side bar (Files / Search / Git tabs on the left rail) |
| Ctrl+Shift+B | open the current page (or the folder's `index.html`) in a browser tab |
| Ctrl+F / Ctrl+H | find / replace bar above the editor (vim keys off). With vim keys on, Ctrl+F opens the `/` line instead |
| F3 / Shift+F3 | next / previous match (find bar, or vim's last `/` search) |
| Ctrl+P | go to file (side bar > Search > File names): fuzzy search of every file in the working folder (`name:42` jumps to a line) |
| Ctrl+Shift+F | find in files: search a string in all files of the working folder (side bar > Search > Text) |
| Ctrl+Shift+G | Git tab: clone a repository (http(s) / ssh) into a path you choose |
| Ctrl+T | new browser tab (focus the address bar) |
| Ctrl+` | show / hide the console (the focus jumps into it; Esc-free: press Ctrl+` again) |
| F2 / Del | rename / delete the selected entry (while the explorer has focus; also in its right-click menu) |

## Vim keys
Motions `hjkl w b e 0 ^ $ gg G { }`; insert `i a I A o O`; edit `x X s D C dd yy cc J ~ r >> <<`; operators `d/c/y` + motion, `diw`;
`p P u Ctrl-R`; visual `v V`; counts (`3dd`); `/search n N`;
ex: `:w :q :q! :wq :42 :set vim|novim :set nu|nonu :set list|nolist :theme Nord`.
Extras in vim mode: Alt+Up/Down move line, Ctrl+Shift+D duplicate line, Ctrl+D/U half page.
