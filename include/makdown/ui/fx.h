// fx.h - small animation helpers that respect Anim::level() (core/anim.h).
#pragma once
#include <QObject>
#include <functional>

class QWidget;
class QAbstractScrollArea;
class QVariantAnimation;

namespace fx {
    // fade a widget in (not used on QtWebEngine views: graphics effects break their rendering)
    void fadeIn(QWidget* w, int baseMs = 180, int minLevel = 1);
    // animate an int from..to; step() gets every value, done() runs at the end. With animations off both run at once.
    // Returns the running animation (null when nothing is animating) so a caller can stop it.
    QVariantAnimation* tween(QObject* parent, int from, int to, int baseMs, std::function<void(int)> step,
                             std::function<void()> done = {}, bool springy = false, int minLevel = 1);
    // snapshot the widget now and fade the snapshot out over whatever changes next (theme switch)
    void crossfade(QWidget* w, int baseMs = 320, int minLevel = 2);
    // wheel scrolling glides instead of jumping (only while the level is "Entertain me")
    void smoothScroll(QAbstractScrollArea* area);
}
