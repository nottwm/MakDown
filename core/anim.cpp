#include <makdown/core/anim.h>
#include <QtGlobal>

static int g_level = Anim::Normal;

void Anim::setLevel(int l) { g_level = qBound(0, l, 3); }
int  Anim::level() { return g_level; }
int  Anim::ms(int base) {
    switch (g_level) {
        case Little: return base / 2;
        case Normal: return base;
        case Full:   return base * 3 / 2;
        default:     return 0;
    }
}
