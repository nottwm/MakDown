# Explorer and browser tab

## Side bar (left)
A thin rail of tabs, top to bottom: **Files** (the explorer below), **Search** (find in files / go to file, `search.md`) and **Git** (clone, `git.md`).
**Ctrl+Shift+E** shows / hides the whole side bar; Ctrl+Shift+F, Ctrl+P and Ctrl+Shift+G jump to Search / Search > File names / Git
(and open the side bar if it is hidden).

## File explorer (Files tab)
Shown by default on the left; **Ctrl+Shift+E** toggles it, **Ctrl+Shift+O** (File > Open Folder...) or dropping a folder on the window
changes the root. The root is remembered between runs; a file given on the command line outside the saved root moves the root to its folder.

* click a folder to expand / collapse it, **double-click** (or Enter) a file to open it in the editor
* text files open in the editor (`.html`, `.js`, `.css` included - markdown colouring is switched off for them); binaries, images and PDFs go
  to the system's default app; files over 20 MB are refused
* right-click: open in editor, open in browser tab, open `index.html` of a folder, new file, new folder, show in file manager, open folder
* hidden files (dotfiles) are not listed

## Browser tab (right, next to the markdown preview)
**Browser > Open page in browser tab** (Ctrl+Shift+B) opens, in this order: the file in the editor if it is `.html/.htm/.xhtml/.svg`, else the
explorer folder's `index.html`, else a file dialog. **Ctrl+T** opens an empty tab: type a path, a folder (opens its `index.html`), a `file://`
URL or `http(s)://...` / `localhost:3000` in the address bar. The tab bar appears once a browser tab exists; the Preview tab cannot be closed.

### Live reload
For local pages MakDown watches the page's folder (up to 8 levels deep, ~2000 entries, skipping `node_modules`, `.git`, ...) for changes to
`.html .htm .css .js .mjs .json .svg` and common image / font files. It also asks the page which local files it actually loaded and watches those
too, so `<script src="../shared/app.js">` outside the folder is tracked. On a change (debounced 200 ms) the page reloads with the cache bypassed,
so edit in the editor, **save**, and the tab updates. Unsaved edits are not shown (the browser reads the file on disk).

* the **Live** button / Browser menu / Settings toggle turns it off for all tabs ("live reload off" shows in the tab bar)
* remote pages (`http://...`) are never watched
* the status pill shows how many files are watched

### Notes
* `LocalContentCanAccessRemoteUrls` is enabled so a local page can load CDN scripts and fonts; pages run with JavaScript on.
* QtWebEngine is optional at build time - see `building.md`. Without it, Ctrl+Shift+B opens the page in the system browser.
