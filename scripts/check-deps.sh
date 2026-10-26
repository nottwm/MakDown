#!/bin/sh
# Quick sanity check of the build environment: ./scripts/check-deps.sh [Qt6|Qt5]
QT=${1:-Qt6}
for tool in make pkg-config g++; do command -v $tool >/dev/null || echo "missing: $tool"; done
pkg-config --exists ${QT}Widgets && echo "${QT}Widgets $(pkg-config --modversion ${QT}Widgets): OK" || echo "missing: ${QT}Widgets (see Documentation/building.md)"
command -v moc >/dev/null || command -v moc-qt6 >/dev/null || echo "note: moc not on PATH (the Makefile also searches Qt's libexec dir)"
pkg-config --exists ${QT}WebEngineWidgets && echo "${QT}WebEngineWidgets: OK (browser tab will be built in)" || echo "note: ${QT}WebEngineWidgets not found - optional; without it the browser tab opens pages in the system browser"
