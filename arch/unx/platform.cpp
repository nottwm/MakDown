// arch/unx/platform.cpp - Linux / macOS / BSD implementation of the platform layer.
#include <makdown/arch/platform.h>
#include <QDir>
#include <QProcess>
#include <signal.h>
#include <unistd.h>

namespace platform {
QString appHome() { return QDir::homePath() + "/.local/share/MakDown"; }
void attachDebugConsole() { /* a normal terminal already provides stdout */ }

// The window manager owns the frame here: the custom frame is a Windows-only feature.
bool customFrameSupported() { return false; }
void installCustomFrame(QWidget*, std::function<bool(const QPoint&)>) {}
void removeCustomFrame(QWidget*) {}
void syncFrame(QWidget*) {}
void setFrameAppearance(QWidget*, bool) {}
int frameCornerRadius() { return 0; }
bool frameNativeEvent(QWidget*, const QByteArray&, void*, NativeResult*) { return false; }

QString shellName() { return QStringLiteral("/bin/sh"); }

void prepareShellCommand(QProcess* p, const QString& command) {
    p->setProgram(QStringLiteral("/bin/sh"));
    p->setArguments({QStringLiteral("-c"), command});
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    p->setChildProcessModifier([] { ::setsid(); });          // own process group: killProcessTree() can reach the children
#endif                                                       // (Qt 5: only the shell itself is killed)
}

void killProcessTree(QProcess* p) {
    if (p->state() == QProcess::NotRunning) return;
    const qint64 pid = p->processId();
    if (pid > 1) ::kill(-pid_t(pid), SIGKILL);               // the whole group
    p->kill();
}
}
