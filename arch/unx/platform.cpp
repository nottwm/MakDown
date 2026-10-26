// arch/unx/platform.cpp - Linux / macOS / BSD implementation of the platform layer.
#include <makdown/arch/platform.h>
#include <QDir>

namespace platform {
QString appHome() { return QDir::homePath() + "/.local/share/MakDown"; }
void attachDebugConsole() { /* a normal terminal already provides stdout */ }
}
