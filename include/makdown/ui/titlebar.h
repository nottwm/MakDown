// titlebar.h - the pieces of MakDown's custom window frame (see Documentation/window-frame.md).
//
//   TitleBar      row above the app: [icon] [menu bar] [drag area] [minimise][maximise][close].
//                 Always hosts the menu bar; the icon and caption buttons only show when the
//                 custom frame is on (otherwise the OS draws its own title bar above this row).
//   FrameBorder   transparent overlay that paints the themed window border on top of everything.
//
// None of these classes declares signals or slots, so no moc step is needed.
#pragma once
#include <QAbstractButton>
#include <QColor>
#include <QFrame>
#include <makdown/core/theme.h>

class QLabel; class QMenuBar;

// Minimise / maximise / restore / close glyph button, drawn with QPainter so it follows the theme.
class CaptionButton : public QAbstractButton {
public:
    enum Kind { Minimize, Maximize, Restore, Close };
    explicit CaptionButton(Kind kind, QWidget* parent = nullptr);
    void setKind(Kind k) { kind_ = k; update(); }
    Kind kind() const { return kind_; }
    void setColors(const QColor& fg, const QColor& hover, const QColor& press, const QColor& hoverFg);
    QSize sizeHint() const override { return QSize(46, 28); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    Kind kind_;
    QColor fg_, hover_, press_, hoverFg_;
};

class TitleBar : public QFrame {
public:
    explicit TitleBar(QWidget* parent = nullptr);
    QMenuBar* menuBar() const { return menu_; }
    CaptionButton* minimizeButton() const { return min_; }
    CaptionButton* maximizeButton() const { return max_; }
    CaptionButton* closeButton() const { return close_; }
    void setCustomFrame(bool on);           // show / hide the icon and caption buttons
    void setMaximized(bool on);             // maximise <-> restore glyph
    void applyTheme(const Theme& t);        // caption glyph + hover colours
private:
    QLabel* icon_ = nullptr;
    QMenuBar* menu_ = nullptr;
    CaptionButton *min_ = nullptr, *max_ = nullptr, *close_ = nullptr;
};

class FrameBorder : public QWidget {
public:
    explicit FrameBorder(QWidget* parent = nullptr);
    // width in logical px, radius = corner radius of the OS window (0 = square)
    void setStyle(int width, const QColor& active, const QColor& inactive, int radius);
    void setActive(bool on) { if (active_ != on) { active_ = on; update(); } }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    int width_ = 1, radius_ = 0;
    QColor active_c_, inactive_c_;
    bool active_ = true;
};
