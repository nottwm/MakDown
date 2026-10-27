# Building

    make unx     # Linux / macOS / BSD
    make win     # Windows via MSYS2/MinGW
    make debug   # run with --debug: prints compiler, build date, architecture

Needs a C++17 compiler, GNU make, pkg-config and Qt 6 Widgets (Qt 5.15: `make QT=Qt5`). Run `scripts/check-deps.sh` to verify.

| OS | Install |
|---|---|
| Debian/Ubuntu | `sudo apt install build-essential pkg-config qt6-base-dev` |
| Fedora | `sudo dnf install gcc-c++ make pkgconf qt6-qtbase-devel` |
| macOS | `brew install qt pkg-config` then `export PKG_CONFIG_PATH="$(brew --prefix qt)/lib/pkgconfig"` |
| Windows (MSYS2 UCRT64) | `pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-qt6-base make` |

If `moc` is not found, pass it explicitly: `make MOC=/usr/lib/qt6/libexec/moc`.

## Optional: QtMultimedia (audio preview)
`make` links QtMultimedia when `pkg-config` finds `Qt6Multimedia` (Debian/Ubuntu: `qt6-multimedia-dev`, MSYS2: `mingw-w64-x86_64-qt6-multimedia`).
`MULTIMEDIA=no` turns it off, `MULTIMEDIA=yes` fails if it is missing. Without it images still preview and sound files are handed to the
system player. `scripts/deploy-win.sh` runs `windeployqt` for it (the backend plugin is not a plain DLL dependency).

## App icon
`assets/icon.png` is embedded into the program (`core/appicon.cpp`, window / taskbar icon). On Windows `make win` also compiles
`arch/win/makdown.rc` with `windres` (part of the MinGW toolchain) so `MakDown.exe` itself carries `assets/icon.ico`.
After replacing `assets/icon.png` run `python3 scripts/gen-icon.py` (needs Pillow) to regenerate `icon.ico` and `core/appicon.cpp`.

## Optional: QtWebEngine (the browser tab)
The browser tab needs the Qt WebEngine module. It is **optional**: the Makefile enables it when `pkg-config` finds
`Qt6WebEngineWidgets` (`WEBENGINE=auto`, the default). Without it MakDown still builds and "Open in browser tab"
hands the page to the system browser instead. `MakDown --debug` / Help > About shows whether it is compiled in.

| OS | Extra package |
|---|---|
| Debian/Ubuntu | `sudo apt install qt6-webengine-dev` |
| Fedora | `sudo dnf install qt6-qtwebengine-devel` |
| Arch | `sudo pacman -S qt6-webengine` |
| macOS | included in `brew install qt` |
| Windows (MSYS2 UCRT64) | `pacman -S mingw-w64-ucrt-x86_64-qt6-webengine` (if your MSYS2 repo provides it) |

`make WEBENGINE=yes` fails with a clear error if it is missing; `make WEBENGINE=no` builds without it even when installed.
Changing the setting rebuilds everything automatically (the Makefile tracks the build configuration and header dependencies).

WebEngine is a large module (Chromium). On WSL, if the browser tab is blank or crashes, try `QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu ./build/unx/MakDown`.

## Cross-compiling for Windows from Linux / WSL

A `CXX` set in your environment (for example `CXX=clang++`) is ignored for `TARGET=win` - the MinGW compiler `x86_64-w64-mingw32-g++` is
used. To force another compiler pass it on the command line: `make TARGET=win CXX=...`.

`make win` auto-selects `x86_64-w64-mingw32-g++` and `x86_64-w64-mingw32-pkg-config` when it is not run inside MSYS2.
Needs the MinGW toolchain and MinGW Qt (Arch: `mingw-w64-gcc`, `mingw-w64-pkg-config`, `mingw-w64-qt6-base` from Martchus' `[ownstuff]` repo)
plus the host `qt6-base` for `moc`. A `windows.h: No such file` error means the Linux g++ was used - check `make -n win | head`.
Copy the Qt DLLs and `platforms/qwindows.dll` next to `MakDown.exe` (or use the `-static` Qt packages).

## Missing DLL errors (Qt6Core.dll, libssp-0.dll, libfreetype-6.dll ...)
`make deploy` builds the Windows exe and runs `scripts/deploy-win.sh`, which copies every DLL the exe needs (found recursively
with objdump) plus `platforms/qwindows.dll` and the image plugins (`imageformats/qjpeg|qgif|qsvg|qico`) into `build/win/`. When the exe uses QtWebEngine it
also runs `windeployqt6` (needed for `QtWebEngineProcess.exe`, the `.pak` resources and locale packs). If your DLLs live somewhere unusual: `SEARCH="/path/to/bin" make deploy`.

## Build output
`make` prints one line per step, e.g. `[ 42%] Compiling core/theme.cpp`; whatever the compiler itself prints (errors, warnings, notes,
suggestions) still appears right below it. `make V=1` shows the full compiler command lines instead. The percentage is the step's
position in the whole build (moc, objects, link), so an incremental build starts part-way, and with `-j` the lines can appear out of order.
