// main.cpp - entry point. Handles command line flags, debug banner and starts the Qt event loop.
//   makdown [file.md] [--debug] [--help] [--version]
#include <QApplication>
#include <QStringList>
#include <cstdio>
#include <cstring>
#include <makdown/ui/mainwindow.h>
#include <makdown/core/appicon.h>
#include <makdown/core/buildinfo.h>
#include <makdown/core/syntax.h>
#include <makdown/core/theme.h>
#include <makdown/arch/platform.h>

int main(int argc, char** argv) {
    // ---- tiny argument parser (before Qt starts, so --help is instant)
    bool debug = false; QString file;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!std::strcmp(a, "--debug") || !std::strcmp(a, "-d")) debug = true;
        else if (!std::strcmp(a, "--help") || !std::strcmp(a, "-h")) {
            std::puts("MakDown - markdown editor, but fancier\n\nusage: makdown [file.md] [--debug] [--version]\n"
                      "  --debug, -d    print compiler / build date / architecture and verbose logs to the console");
            return 0;
        }
        else if (!std::strcmp(a, "--version")) { std::puts("MakDown 1.0"); return 0; }
        else if (a[0] != '-') file = QString::fromLocal8Bit(a);
    }

    if (debug) platform::attachDebugConsole();      // no-op on unx; opens a console on Windows
    g_makdownDebug = debug;

#ifdef MAKDOWN_WEBENGINE
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);   // QtWebEngine wants this set before QApplication exists
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationName("MakDown");
    QApplication::setApplicationVersion("1.0");
    QApplication::setWindowIcon(makdownIcon());   // embedded assets/icon.png (the .exe icon comes from arch/win/makdown.rc)
    QApplication::setStyle("Fusion");           // Fusion honours our palette + QSS identically on every OS

    if (debug) {
        std::printf("==============================================\n%s\n==============================================\n",
                    makdownBuildInfo().toLocal8Bit().constData());
        mdLog("home folder   : " + ThemeStore::appHome());
        mdLog("presets folder: " + ThemeStore::presetsDir());
        mdLog("syntax folder : " + SyntaxStore::syntaxDir());
    }

    MainWindow w(file);
    w.show();
    return app.exec();
}
