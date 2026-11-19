#pragma once
// Written by tools/ring_glyph_sheet.py: which slots of steamvr/assets/ring-wheel/ring-glyph-sheet.png hold a painted square (bit = slot,
// steamvr/ring_wheel_glyph_art.h). Do not edit by hand; the script rewrites it with the sheet.
#include <cstdint>
namespace framevr::ringwheel::glyphart {
constexpr uint32_t PaintedMask = 0x000000FFu;   // 8 painted of 12
}
