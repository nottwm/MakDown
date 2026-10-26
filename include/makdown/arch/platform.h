// platform.h - the ONLY place where OS-specific behaviour is declared.
// Implemented once per target in arch/unx/platform.cpp and arch/win/platform.cpp;
// the top-level Makefile links exactly one of them (make unx / make win).
#pragma once
#include <QString>

namespace platform {
    // MakDown's home folder.
    //   unx: $HOME/.local/share/MakDown      win: $HOME\AppData\Local\MakDown
    QString appHome();
    // Windows GUI apps have no console; this attaches/creates one for --debug. No-op on unx.
    void attachDebugConsole();
}
