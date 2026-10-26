// arch/win/platform.cpp - Windows implementation of the platform layer.
#include <makdown/arch/platform.h>
#include <QDir>
#include <cstdio>
#include <windows.h>

namespace platform {
QString appHome() { return QDir::homePath() + "/AppData/Local/MakDown"; }   // $HOME\AppData\Local\MakDown

// The exe is linked with -mwindows (no console). For --debug, reuse the parent console or open a new one.
void attachDebugConsole() {
    if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
}
}
