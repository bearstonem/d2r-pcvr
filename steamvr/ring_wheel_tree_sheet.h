#pragma once
// Written by tools/ring_tree_sheet.py: which slots of steamvr/assets/ring-wheel/ring-tree-sheet.png hold a painted square (bit = slot,
// steamvr/ring_wheel_tree_art.h). Do not edit by hand; the script rewrites it with the sheet.
#include <cstdint>
namespace framevr::ringwheel::treeart {
constexpr uint32_t PaintedMask = 0x00FFFFFFu;   // 24 painted of 24
}
