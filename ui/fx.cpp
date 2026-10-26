#include <makdown/ui/fx.h>
#include <makdown/core/anim.h>
#include <QAbstractScrollArea>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPointer>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <QWidget>

void fx::fadeIn(QWidget* w, int baseMs, int minLevel) {
    if (!w || Anim::level() < minLevel || Anim::ms(baseMs) <= 0) return;
    auto* e = new QGraphicsOpacityEffect(w);                 // replaces (and deletes) a previous effect
    e->setOpacity(0.0);
    w->setGraphicsEffect(e);
    auto* a = new QPropertyAnimation(e, "opacity", e);
    a->setDuration(Anim::ms(baseMs));
    a->setStartValue(0.0); a->setEndValue(1.0);
    a->setEasingCurve(QEasingCurve::OutCubic);
    QPointer<QGraphicsOpacityEffect> pe(e);
    QObject::connect(a, &QPropertyAnimation::finished, w, [w, pe] {
        QMetaObject::invokeMethod(w, [w, pe] { if (pe && w->graphicsEffect() == pe) w->setGraphicsEffect(nullptr); },
                                  Qt::QueuedConnection);     // not inside the animation's own signal
    });
    a->start();
}

QVariantAnimation* fx::tween(QObject* parent, int from, int to, int baseMs, std::function<void(int)> step,
                             std::function<void()> done, bool springy, int minLevel) {
    if (Anim::level() < minLevel || Anim::ms(baseMs) <= 0 || from == to) {
        step(to);
        if (done) done();
        return nullptr;
    }
    auto* a = new QVariantAnimation(parent);
    a->setStartValue(from); a->setEndValue(to);
    a->setDuration(Anim::ms(baseMs));
    a->setEasingCurve(springy && Anim::level() >= Anim::Full ? QEasingCurve::OutBack : QEasingCurve::OutCubic);
    QObject::connect(a, &QVariantAnimation::valueChanged, a, [step](const QVariant& v) { step(v.toInt()); });
    QObject::connect(a, &QVariantAnimation::finished, a, [a, step, to, done] {
        if (a->currentTime() >= a->duration()) { step(to); if (done) done(); }     // stop() mid-way must not run done()
        a->deleteLater();
    });
    a->start();
    return a;
}

void fx::crossfade(QWidget* w, int baseMs, int minLevel) {
    if (!w || !w->isVisible() || Anim::level() < minLevel || Anim::ms(baseMs) <= 0) return;
    auto* ov = new QLabel(w);
    ov->setPixmap(w->grab());
    ov->setGeometry(w->rect());
    ov->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* e = new QGraphicsOpacityEffect(ov);
    ov->setGraphicsEffect(e);
    ov->show(); ov->raise();
    auto* a = new QPropertyAnimation(e, "opacity", ov);
    a->setDuration(Anim::ms(baseMs));
    a->setStartValue(1.0); a->setEndValue(0.0);
    a->setEasingCurve(QEasingCurve::InOutQuad);
    QObject::connect(a, &QPropertyAnimation::finished, ov, &QObject::deleteLater);
    a->start();
}

namespace {
class SmoothScroller : public QObject {
public:
    explicit SmoothScroller(QAbstractScrollArea* a) : QObject(a), area_(a) {
        a->viewport()->installEventFilter(this);
        anim_ = new QVariantAnimation(this);
        anim_->setEasingCurve(QEasingCurve::OutCubic);
        anim_->setDuration(Anim::ms(160));
        connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { area_->verticalScrollBar()->setValue(v.toInt()); });
    }
protected:
    bool eventFilter(QObject*, QEvent* e) override {
        if (e->type() != QEvent::Wheel || Anim::level() < Anim::Full) return false;
        auto* we = static_cast<QWheelEvent*>(e);
        if (we->modifiers() != Qt::NoModifier || we->angleDelta().y() == 0 || !we->pixelDelta().isNull()) return false;
        QScrollBar* sb = area_->verticalScrollBar();
        if (sb->maximum() <= sb->minimum()) return false;
        const bool running = anim_->state() == QAbstractAnimation::Running;
        int base = running ? anim_->endValue().toInt() : sb->value();
        const int target = qBound(sb->minimum(), base - qRound(we->angleDelta().y() / 120.0 * sb->singleStep() * 3), sb->maximum());
        anim_->stop();
        anim_->setDuration(Anim::ms(160));
        anim_->setStartValue(sb->value()); anim_->setEndValue(target);
        anim_->start();
        return true;
    }
private:
    QAbstractScrollArea* area_;
    QVariantAnimation* anim_;
};
}

void fx::smoothScroll(QAbstractScrollArea* area) { if (area) new SmoothScroller(area); }
