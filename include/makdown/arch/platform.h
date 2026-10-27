// platform.h - the ONLY place where OS-specific behaviour is declared.
// Implemented once per target in arch/unx/platform.cpp and arch/win/platform.cpp;
// the top-level Makefile links exactly one of them (make unx / make win).
#pragma once
#include <QByteArray>
#include <QPoint>
#include <QString>
#include <QtGlobal>
#include <functional>

class QWidget;

namespace platform {
    // MakDown's home folder.
    //   unx: $HOME/.local/share/MakDown      win: $HOME\AppData\Local\MakDown
    QString appHome();
    // Windows GUI apps have no console; this attaches/creates one for --debug. No-op on unx.
    void attachDebugConsole();

    // ---- custom window frame ------------------------------------------------------------------
    // MakDown can replace the OS title bar + border with its own themed ones (see ui/titlebar.h).
    // The OS keeps doing the hard parts - resizing, Aero Snap, the drop shadow, the system menu -
    // so the window still behaves like any other window. Only implemented on Windows; on unx
    // customFrameSupported() is false and every function below is a no-op.
    //
    // QWidget::nativeEvent() has a different "result" type in Qt 5 (long*) and Qt 6 (qintptr*).
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    using NativeResult = qintptr;
#else
    using NativeResult = long;
#endif
    // True when this OS can host a custom frame.
    bool customFrameSupported();
    // Makes the top-level widget frameless and gives it native resize / snap / shadow behaviour.
    // isCaptionAt(p) is asked (p in the widget's own logical coordinates) whether a point belongs
    // to the draggable title area; the OS then treats it as a real caption (drag, double-click, snap).
    void installCustomFrame(QWidget* w, std::function<bool(const QPoint&)> isCaptionAt);
    // Back to the native frame. (Qt recreates the native window, so the caller re-shows it.)
    void removeCustomFrame(QWidget* w);
    // Re-applies the native window styles (idempotent; call after show / state changes).
    void syncFrame(QWidget* w);
    // Light / dark flag for the native bits that still show (system menu, etc.).
    void setFrameAppearance(QWidget* w, bool dark);
    // Corner radius, in logical px, the OS gives a custom-framed window (0 = square corners).
    int frameCornerRadius();
    // Call from QWidget::nativeEvent(). Returns true when the message was consumed.
    bool frameNativeEvent(QWidget* w, const QByteArray& eventType, void* message, NativeResult* result);
}
