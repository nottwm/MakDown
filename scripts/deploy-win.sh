#!/bin/sh
# deploy-win.sh - copy every DLL MakDown.exe needs (recursively) + the Qt "windows" platform plugin next to it.
#   usage: scripts/deploy-win.sh [build/win/MakDown.exe]      (or: make deploy)
#   env  : SEARCH="dir1 dir2"  extra directories that contain the MinGW/Qt DLLs
# Works in MSYS2 (UCRT64/MINGW64) and in Arch/other Linux with the mingw-w64 packages.
EXE=${1:-build/win/MakDown.exe}
[ -f "$EXE" ] || { echo "not found: $EXE (run 'make win' first)"; exit 1; }
DEST=$(dirname "$EXE")
OBJDUMP=${OBJDUMP:-x86_64-w64-mingw32-objdump}
command -v "$OBJDUMP" >/dev/null 2>&1 || OBJDUMP=objdump

# directories where the DLLs may live (first match wins)
BINS="$SEARCH /ucrt64/bin /mingw64/bin /clang64/bin /usr/x86_64-w64-mingw32/bin /usr/x86_64-w64-mingw32/sys-root/mingw/bin"

find_dll() {   # $1 = dll name -> prints full path of first match
    for d in $BINS; do
        for f in "$d/$1" "$d/$(echo "$1" | tr 'A-Z' 'a-z')"; do [ -f "$f" ] && { echo "$f"; return 0; }; done
    done
    return 1
}

# Qt's platform plugin: Windows refuses to start a Qt GUI without platforms/qwindows.dll
PLUGIN=
for d in $BINS; do
    for p in "$d/../lib/qt6/plugins/platforms/qwindows.dll" "$d/../plugins/platforms/qwindows.dll" "$d/../share/qt6/plugins/platforms/qwindows.dll"; do
        [ -f "$p" ] && { PLUGIN=$p; break 2; }
    done
done
if [ -n "$PLUGIN" ]; then mkdir -p "$DEST/platforms"; cp -u "$PLUGIN" "$DEST/platforms/"; echo "plugin  : platforms/qwindows.dll";
else echo "WARNING: qwindows.dll not found - set SEARCH=\"/path/to/mingw/bin\""; fi

# breadth-first walk over DLL dependencies (the exe, the plugin, then each copied DLL)
QUEUE=$(mktemp); echo "$EXE" > "$QUEUE"; [ -n "$PLUGIN" ] && echo "$DEST/platforms/qwindows.dll" >> "$QUEUE"

# image plugins: without them the preview cannot show JPEG / GIF / SVG / ICO images (PNG is built in)
if [ -n "$PLUGIN" ]; then
    PDIR=$(dirname "$(dirname "$PLUGIN")")
    for im in qjpeg qgif qsvg qico; do
        if [ -f "$PDIR/imageformats/$im.dll" ]; then
            mkdir -p "$DEST/imageformats"; cp -u "$PDIR/imageformats/$im.dll" "$DEST/imageformats/"
            echo "plugin  : imageformats/$im.dll"; echo "$DEST/imageformats/$im.dll" >> "$QUEUE"
        fi
    done
fi
n=1
while [ "$n" -le "$(wc -l < "$QUEUE")" ]; do
    cur=$(sed -n "${n}p" "$QUEUE"); n=$((n+1))
    for dll in $("$OBJDUMP" -p "$cur" 2>/dev/null | awk '/DLL Name:/{print $3}'); do
        [ -f "$DEST/$dll" ] && continue
        if src=$(find_dll "$dll"); then cp "$src" "$DEST/"; echo "copied  : $dll"; echo "$DEST/$dll" >> "$QUEUE"
        else :  # Windows system DLL (kernel32, user32 ...) - nothing to bundle
        fi
    done
done
rm -f "$QUEUE"

# QtMultimedia (audio preview) needs its backend plugin (multimedia/ffmpegmediaplugin.dll) - also a job for windeployqt.
# QtWebEngine (browser tab): besides DLLs it needs QtWebEngineProcess.exe, .pak resources, icudtl.dat and
# locale packs, which the DLL walk cannot discover. windeployqt knows the full list.
if "$OBJDUMP" -p "$EXE" 2>/dev/null | grep -qiE 'Qt[56](WebEngineWidgets|Multimedia)'; then
    WDQ=
    for t in windeployqt6 windeployqt-qt6 x86_64-w64-mingw32-windeployqt6 windeployqt; do
        command -v "$t" >/dev/null 2>&1 && { WDQ=$t; break; }
    done
    if [ -n "$WDQ" ]; then
        echo "QtWebEngine / QtMultimedia detected: running $WDQ"
        "$WDQ" --no-compiler-runtime "$EXE" || echo "WARNING: $WDQ failed - the browser tab may not start"
    else
        echo "WARNING: MakDown.exe uses QtWebEngine but windeployqt was not found."
        echo "         Run it yourself: windeployqt6 $EXE   (the browser tab needs QtWebEngineProcess.exe + .pak files)"
    fi
fi
echo "done. Run $EXE from Explorer or any shell."
