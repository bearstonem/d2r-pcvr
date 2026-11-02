#pragma once
#include <cmath>
#include <cstdint>
namespace framevr::qol {
// D2R life is 24.8 fixed point. Use the damage record rather than the
// already-clamped proposed health, so lethal overkill remains lethal when
// its scaled amount still exceeds current life. Round damage upward: a
// positive multiplier cannot accidentally erase tiny hits.
inline int ScaleHealthWrite(int before,int proposed,int damage,double multiplier) {
    if(before<=0||proposed<0||proposed>=before||damage<=0||
       !std::isfinite(multiplier)||multiplier<0||multiplier>=1) return proposed;
    const int64_t scaled=int64_t(std::ceil(double(damage)*multiplier));
    const int64_t life=int64_t(before)-scaled;
    // The native direct-damage path treats less than one full HP as death.
    // Zero multiplier must preserve even a pre-existing fractional HP.
    return scaled==0?before:life>=256?int(life):0;
}

}
