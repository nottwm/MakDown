// buildinfo.h - compile-time information (compiler, date, architecture) + debug logging.
#pragma once
#include <QString>

// Multi-line, human readable description of this build (used by --debug and About page).
QString makdownBuildInfo();

// Global switch set by main() when the app is started with --debug.
extern bool g_makdownDebug;

// Prints "[makdown] message" to the console, but only in debug mode.
void mdLog(const QString& message);
