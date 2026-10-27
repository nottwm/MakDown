// titlebar.cpp - custom window frame widgets: title bar, caption buttons, themed border.
#include <makdown/ui/titlebar.h>
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>

// ---------------------------------------------------------------------------------------------
// CaptionButton
// ---------------------------------------------------------------------------------------------
CaptionButton::CaptionButton(Kind kind, QWidget* parent) : QAbstractButton(parent), kind_(kind) {
    setAttribute(Qt::WA_Hover);                     // repaint on enter / leave
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
    setFixedWidth(46);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setColors(QColor("#cccccc"), QColor(255, 255, 255, 30), QColor(255, 255, 255, 55), QColor("#ffffff"));
}

void CaptionButton::setColors(const QColor& fg, const QColor& hover, const QColor& press, const QColor& hoverFg) {
    fg_ = fg; hover_ = hover; press_ = press; hoverFg_ = hoverFg; update();
}

void CaptionButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const bool down = isDown(), hot = underMouse();
    if (down || hot) p.fillRect(rect(), down ? press_ : hover_);
    const QColor ink = (hot || down) ? hoverFg_ : fg_;
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(ink, 1.0); pen.setCapStyle(Qt::FlatCap); p.setPen(pen);
    const double cx = width() / 2.0, cy = height() / 2.0, s = 5.0;      // glyphs are 10 x 10 logical px
    switch (kind_) {
    case Minimize: p.drawLine(QPointF(cx - s, cy + 0.5), QPointF(cx + s, cy + 0.5)); break;
    case Maximize: p.drawRect(QRectF(cx - s + 0.5, cy - s + 0.5, 2 * s - 1, 2 * s - 1)); break;
    case Restore: {                                                    // two overlapping squares
        const double q = 2 * s - 3;
        p.drawRect(QRectF(cx - s + 0.5, cy - s + 3.5, q, q));
        p.drawPolyline(QPolygonF({QPointF(cx - s + 3.5, cy - s + 3.5), QPointF(cx - s + 3.5, cy - s + 0.5),
                                  QPointF(cx + s - 0.5, cy - s + 0.5), QPointF(cx + s - 0.5, cy + s - 3.5),
                                  QPointF(cx + s - 3.5, cy + s - 3.5)}));
        break;
    }
    case Close:
        p.drawLine(QPointF(cx - s, cy - s), QPointF(cx + s, cy + s));
        p.drawLine(QPointF(cx - s, cy + s), QPointF(cx + s, cy - s));
        break;
    }
}

// ---------------------------------------------------------------------------------------------
// TitleBar
// ---------------------------------------------------------------------------------------------
TitleBar::TitleBar(QWidget* parent) : QFrame(parent) {
    setObjectName("titleBar");
    auto* l = new QHBoxLayout(this); l->setContentsMargins(0, 0, 0, 0); l->setSpacing(0);

    icon_ = new QLabel; icon_->setFixedSize(34, 16); icon_->setAlignment(Qt::AlignCenter);
    icon_->setPixmap(QApplication::windowIcon().pixmap(16, 16));
    icon_->setProperty("dragRegion", true);          // the OS treats clicks here as title-bar clicks
    l->addWidget(icon_);

    menu_ = new QMenuBar;
    menu_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    l->addWidget(menu_);

    auto* drag = new QWidget; drag->setProperty("dragRegion", true);   // the empty stretch you grab to move the window
    drag->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    l->addWidget(drag, 1);

    min_ = new CaptionButton(CaptionButton::Minimize); max_ = new CaptionButton(CaptionButton::Maximize);
    close_ = new CaptionButton(CaptionButton::Close);
    min_->setToolTip("Minimize"); max_->setToolTip("Maximize"); close_->setToolTip("Close");
    for (CaptionButton* b : {min_, max_, close_}) l->addWidget(b);
    setCustomFrame(false);
}

void TitleBar::setCustomFrame(bool on) {
    icon_->setVisible(on);
    for (CaptionButton* b : {min_, max_, close_}) b->setVisible(on);
}

void TitleBar::setMaximized(bool on) {
    max_->setKind(on ? CaptionButton::Restore : CaptionButton::Maximize);
    max_->setToolTip(on ? "Restore" : "Maximize");
}

void TitleBar::applyTheme(const Theme& t) {
    const QColor fg(t.color("fg"));
    QColor hover = fg, press = fg; hover.setAlpha(28); press.setAlpha(52);
    for (CaptionButton* b : {min_, max_}) b->setColors(fg, hover, press, fg);
    close_->setColors(fg, QColor("#e81123"), QColor("#f1707a"), QColor("#ffffff"));   // the familiar red close button
}

// ---------------------------------------------------------------------------------------------
// FrameBorder
// ---------------------------------------------------------------------------------------------
FrameBorder::FrameBorder(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
}

void FrameBorder::setStyle(int width, const QColor& active, const QColor& inactive, int radius) {
    width_ = width; active_c_ = active; inactive_c_ = inactive; radius_ = radius; update();
}

void FrameBorder::paintEvent(QPaintEvent*) {
    if (width_ <= 0) return;
    QPainter p(this);
    const QColor c = active_ ? active_c_ : inactive_c_;
    if (radius_ <= 0) {                               // square window: four crisp strips
        const int w = qMin(width_, qMin(width(), height()) / 2);
        p.fillRect(0, 0, width(), w, c);               p.fillRect(0, height() - w, width(), w, c);
        p.fillRect(0, w, w, height() - 2 * w, c);      p.fillRect(width() - w, w, w, height() - 2 * w, c);
        return;
    }
    p.setRenderHint(QPainter::Antialiasing);           // rounded window: a ring that follows the OS corner radius
    QPainterPath outer, inner;
    outer.addRoundedRect(QRectF(rect()), radius_, radius_);
    const double w = width_;
    inner.addRoundedRect(QRectF(rect()).adjusted(w, w, -w, -w), qMax(0.0, radius_ - w), qMax(0.0, radius_ - w));
    p.fillPath(outer.subtracted(inner), c);
}
