// buildinfo.cpp - kept in its OWN translation unit so __DATE__/__TIME__ describe a single build.
#include <makdown/core/buildinfo.h>
#include <QSysInfo>
#include <QCoreApplication>
#include <cstdio>

bool g_makdownDebug = false;

void mdLog(const QString& message) {
    if (!g_makdownDebug) return;
    std::fprintf(stdout, "[makdown] %s\n", message.toLocal8Bit().constData());
    std::fflush(stdout);
}

QString makdownBuildInfo() {
    // ---- compiler -----------------------------------------------------------
    QString compiler;
#if defined(__clang__)
    compiler = QStringLiteral("Clang ") + QString::fromLatin1(__clang_version__);
#elif defined(__GNUC__)
    compiler = QStringLiteral("GCC ") + QString::fromLatin1(__VERSION__);
#elif defined(_MSC_VER)
    compiler = QStringLiteral("MSVC ") + QString::number(_MSC_VER);
#else
    compiler = QStringLiteral("unknown compiler");
#endif

    // ---- target CPU architecture (from predefined compiler macros) ----------
    QString arch;
#if defined(__x86_64__) || defined(_M_X64)
    arch = QStringLiteral("x86_64");
#elif defined(__aarch64__) || defined(_M_ARM64)
    arch = QStringLiteral("arm64 (aarch64)");
#elif defined(__i386__) || defined(_M_IX86)
    arch = QStringLiteral("x86 (32-bit)");
#elif defined(__arm__) || defined(_M_ARM)
    arch = QStringLiteral("arm (32-bit)");
#elif defined(__riscv)
    arch = QStringLiteral("riscv");
#else
    arch = QStringLiteral("unknown");
#endif

    // ---- makefile target flag (-DMAKDOWN_TARGET="unx"|"win") ----------------
#ifdef MAKDOWN_TARGET
    const QString target = QStringLiteral(MAKDOWN_TARGET);
#else
    const QString target = QStringLiteral("(unset)");
#endif

#ifdef MAKDOWN_WEBENGINE
    const QString webengine = QStringLiteral("yes (browser tab enabled)");
#else
    const QString webengine = QStringLiteral("no (browser tab opens the system browser)");
#endif

    QStringList l;
    l << QStringLiteral("MakDown build information")
      << QStringLiteral("  Makefile target : ") + target
      << QStringLiteral("  Compiler        : ") + compiler
      << QStringLiteral("  C++ standard    : ") + QString::number(static_cast<long>(__cplusplus))
      << QStringLiteral("  Build date      : ") + QString::fromLatin1(__DATE__ " " __TIME__)
      << QStringLiteral("  Architecture    : ") + arch + QStringLiteral(" (built) / ") + QSysInfo::currentCpuArchitecture() + QStringLiteral(" (running)")
      << QStringLiteral("  Qt (compiled)   : ") + QStringLiteral(QT_VERSION_STR)
      << QStringLiteral("  Qt (runtime)    : ") + QString::fromLatin1(qVersion())
      << QStringLiteral("  QtWebEngine     : ") + webengine
      << QStringLiteral("  Operating system: ") + QSysInfo::prettyProductName();
    return l.join('\n');
}
