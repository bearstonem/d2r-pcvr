#pragma once
#include "replay_audit_model.h"
#include <cstring>

namespace framevr::stereoreplay {
// Copyright (c) 2026 BodyWalkVR. Camera record layout follows the credited
// camfix investigation; see THIRD_PARTY.md. Row vectors, actual XR eye poses.
// Keep the left view basis used by CPU-built billboards/object constants and
// fold the complete left-to-right transform into its projection.
inline bool CameraRecord(std::span<const float> words,size_t o,const replayaudit::Camera& left) {
    using namespace replayaudit;
    if(o>words.size()||words.size()-o<112||!left.haveView||!left.haveProjection||
       !Near(words.subspan(o),left.view,1e-5,.025)||!EyeProjection(words.subspan(o+32),left.projection))return false;
    const auto view=MatrixAt(words,o),projection=MatrixAt(words,o+32);
    Matrix iv{},ip{},ivp{};
    return Invert(view,iv)&&Invert(projection,ip)&&Invert(Multiply(view,projection),ivp)&&
        Near(words.subspan(o+16),iv)&&Near(words.subspan(o+48),ip)&&
        Near(words.subspan(o+64),Multiply(view,projection))&&Near(words.subspan(o+80),ivp);
}
// Resolve record offsets once per shader binding. Rendering uses only these
// offsets, with no recurring search through object/material constants.
inline std::vector<size_t> FindCameraRecords(std::span<const float> words,const replayaudit::Camera& left) {
    std::vector<size_t> offsets;
    for(size_t o=0;o+112<=words.size();++o)if(CameraRecord(words,o,left)){offsets.push_back(o);o+=111;}
    return offsets;
}
inline std::vector<size_t> SelectCameraRecords(std::span<const float> words,std::span<const size_t> cached,const replayaudit::Camera& captured) {
    std::vector<size_t> records;
    for(const size_t o:cached)if(CameraRecord(words,o,captured))records.push_back(o);
    return records;
}
// A mapped upload window may contain object constants on a known camera root.
// Reject those windows before copying 4 KiB from write-combined memory. This
// only checks a cached record's view: a match still needs CameraRecord's full
// projection/inverse validation on the copied snapshot.
inline bool MayContainCachedCameraRecord(std::span<const float> mapped,std::span<const size_t> cached,const replayaudit::Camera& captured) {
    if(!captured.haveView||!captured.haveProjection)return false;
    for(const size_t o:cached){
        if(o>mapped.size()||mapped.size()-o<112)continue;
        float first;std::memcpy(&first,mapped.data()+o,sizeof first);
        const double expected=captured.view[0];
        if(!std::isfinite(first)||!std::isfinite(expected)||std::abs(double(first)-expected)>.025+1e-5*std::abs(expected))continue;
        std::array<float,16> view;std::memcpy(view.data(),mapped.data()+o,sizeof view);
        if(replayaudit::Near(view,captured.view,1e-5,.025))return true;
    }
    return false;
}
inline unsigned FoldCameraAt(std::span<float> words,std::span<const size_t> offsets,const replayaudit::Camera& left,const replayaudit::Camera& right) {
    using namespace replayaudit;
    if(!left.haveView||!left.haveProjection||!right.haveView||!right.haveProjection)return 0;
    unsigned records=0;
    for(const size_t o:offsets)if(!CameraRecord(words,o,left))return 0;
    for(const size_t o:offsets){
        const auto view=MatrixAt(words,o),projection=MatrixAt(words,o+32);
        const auto iv=MatrixAt(words,o+16),ip=MatrixAt(words,o+48);
        const auto shift=Multiply(iv,right.view);
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
        ++records;
    }
    return records;
}
inline unsigned FoldCamera(std::span<float> words,const replayaudit::Camera& left,const replayaudit::Camera& right) {
    return FoldCameraAt(words,FindCameraRecords(words,left),left,right);
}
}
