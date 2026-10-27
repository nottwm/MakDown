// arch/unx/platform.cpp - Linux / macOS / BSD implementation of the platform layer.
#include <makdown/arch/platform.h>
#include <QDir>

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
}
