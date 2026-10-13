#pragma once
#include "replay_audit_model.h"

namespace framevr::stereoreplay {
// Copyright (c) 2026 BodyWalkVR. Camera record layout follows the credited
// camfix investigation; see THIRD_PARTY.md. Row vectors, actual XR eye poses.
// Keep the left view basis used by CPU-built billboards/object constants and
// fold the complete left-to-right transform into its projection.
inline unsigned FoldCamera(std::span<float> words,const replayaudit::Camera& left,const replayaudit::Camera& right) {
    using namespace replayaudit;
    if(!left.haveView||!left.haveProjection||!right.haveView||!right.haveProjection)return 0;
    Matrix inverseLeft{};if(!Invert(left.view,inverseLeft))return 0;
    const auto shift=Multiply(inverseLeft,right.view);
    unsigned records=0;
    for(size_t o=0;o+112<=words.size();++o){
        if(!Near(words.subspan(o),left.view,1e-5,.025)||!EyeProjection(words.subspan(o+32),left.projection))continue;
        const auto view=MatrixAt(words,o),projection=MatrixAt(words,o+32);
        Matrix iv{},ip{},ivp{};
        const auto vp=Multiply(view,projection);
        if(!Invert(view,iv)||!Invert(projection,ip)||!Invert(vp,ivp)||
           !Near(words.subspan(o+16),iv)||!Near(words.subspan(o+48),ip)||
           !Near(words.subspan(o+64),vp)||!Near(words.subspan(o+80),ivp))continue;
        auto target=right.projection;
        // Preserve the engine's subpixel offset without borrowing left-eye FOV.
        target[8]+=projection[8]-left.projection[8];target[9]+=projection[9]-left.projection[9];
        const auto folded=Multiply(shift,target);Matrix inverseFolded{};
        if(!Invert(folded,inverseFolded))continue;
        const auto newVp=Multiply(view,folded),newInverseVp=Multiply(inverseFolded,iv);
        const auto previous=Multiply(MatrixAt(words,o+96),Multiply(ip,folded));
        if(std::any_of(previous.begin(),previous.end(),[](double v){return !std::isfinite(v);}))continue;
        auto put=[&](size_t offset,const Matrix& m){for(unsigned i=0;i<16;++i)words[offset+i]=float(m[i]);};
        put(o+32,folded);put(o+48,inverseFolded);put(o+64,newVp);put(o+80,newInverseVp);put(o+96,previous);
        ++records;o+=111;
    }
    return records;
}
}
