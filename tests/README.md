# MakDown test-suite

Run before opening a PR or publishing a release:

```sh
make test                                      # same as the next line
python3 tests/run_tests.py                       # static + unit + real-app tests (~4 min)
python3 tests/run_tests.py --fast                # static + in-process unit tests only
python3 tests/run_tests.py --full                # + clean/incremental/variant builds in throw-away copies
python3 tests/run_tests.py --release --tag v1.0  # everything + release gate (clean tree, tag == version)
python3 tests/run_tests.py --sanitize            # ASan + UBSan build of a copy, runs the fast tests there
python3 tests/run_tests.py -k markdown -v        # one module, verbose
```

Needs Python 3.8+ (stdlib only), g++, make and the Qt 6 dev packages (`qt6-base-dev` includes Qt Test).
Everything runs headless (`QT_QPA_PLATFORM=offscreen`) with a temporary `$HOME`; your settings are never touched.

| Module | What it checks |
|---|---|
| `test_00_repo` | Makefile fragments list every source, moc list, architecture rules, docs/links/anchors, icon sync, versions |
| `test_10_build` | warnings allow-list (`known_warnings.txt`), incremental builds, Qt5/clang/WebEngine/Multimedia variants |
| `test_20_cli` | the real binary: flags, headless start-up, hostile files, corrupt user data, CPU-busy (frozen UI) detection |
| `test_30_markdown` | renderer behaviour, every theme, pathological inputs |
| `test_40_search` | regex builder, text search, fuzzy file search, globs, binary sniffing, directory walker |
| `test_50_syntax` | stock packs, parser, on-disk lifecycle, file matching, highlighter |
| `test_60_theme` | presets, JSONC, generated QSS, settings, contrast |
| `test_65_lang` | `lang/*.json`: same keys / placeholders / plural forms in every language, generated `langstock.cpp` in sync, lookup + fallback + plural rules, a real window in ru / pl |
| `test_70_editor` | formatting helpers, list continuation, vim engine, key-storm fuzzing |
| `test_80_window` | whole `MainWindow` in-process: opening files, live preview, dialogs |
| `test_90_release` | release gate (only with `--release`) |

`tests/harness/harness.cpp` is compiled against the application's own object files and driven over JSON lines.

## Known issues

Tests named `test_KNOWN_ISSUE_*` are `@expectedFailure`: they document real bugs without breaking the run. When a bug is
fixed its test *unexpectedly passes* and the runner fails, telling you to delete the decorator.
