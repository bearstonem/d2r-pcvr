#pragma once
#include "journal.h"

namespace framevr::dialogue {
// Widget rectangles are local to native layout parents. After a VR resize
// their origin need not match the captured texture, even when the box itself
// looks plausible. Retain every source pixel instead of guessing that mapping.
// Rendering and laser hits use this same canvas on every controller profile.
inline journal::Rect Canvas(int width,int height) {
    return width>0&&height>0?journal::Rect{0,0,width,height}:journal::Rect{};
}
}
