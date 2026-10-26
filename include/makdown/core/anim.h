// anim.h - one global "how much animation" knob (Settings > Animations).
//   0 = Don't distract me   no animations at all
//   1 = Just a little bit   short fades, folder expand, explorer slide
//   2 = Normal              + window fade-in, theme cross-fade, smooth image zoom
//   3 = Entertain me        + longer / springy motion, smooth wheel scrolling, bouncing equalizer
// Every animated thing asks Anim::level() / Anim::ms() instead of deciding for itself.
#pragma once

namespace Anim {
    enum Level { None = 0, Little = 1, Normal = 2, Full = 3 };
    void setLevel(int level);
    int  level();
    inline bool on() { return level() > 0; }
    int  ms(int baseMs);              // duration for this level (0 when animations are off)
}
