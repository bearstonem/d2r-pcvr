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
struct CameraWindow {
    uint64_t buffer,offset;
    std::span<const float> words;
    std::span<const size_t> records;
    std::span<const std::pair<uint64_t,uint32_t>> roles{};
};
struct CameraOwnership {
    bool consistent=false;
    std::vector<std::vector<size_t>> records;
    std::map<std::pair<uint64_t,uint32_t>,std::vector<size_t>> roles;
};
// Upload resources contain several allocations. A validated camera record can
// therefore appear in more than one submitted CBV window. Assign its physical
// bytes to the closest complete submitted window, without dropping any unique
// record or guessing a shader's CBV size. Equal-base aliases retain ownership.
inline CameraOwnership OwnCameraRecords(std::span<const CameraWindow> windows) {
    struct Owner {uint64_t base;std::span<const float> snapshot;};
    std::map<std::pair<uint64_t,uint64_t>,Owner> owners;
    for(const auto& window:windows)for(const size_t o:window.records){
        if(o>window.words.size()||window.words.size()-o<112||
           o>(std::numeric_limits<uint64_t>::max()-window.offset)/sizeof(float))return {};
        const auto key=std::pair{window.buffer,window.offset+uint64_t(o)*sizeof(float)};
        const auto snapshot=window.words.subspan(o,112);
        const auto [found,fresh]=owners.try_emplace(key,Owner{window.offset,snapshot});
        if(!fresh){
            if(std::memcmp(found->second.snapshot.data(),snapshot.data(),112*sizeof(float)))return {};
            found->second.base=std::max(found->second.base,window.offset);
        }
    }
    CameraOwnership result{true,{},{}};result.records.resize(windows.size());
    for(size_t i=0;i<windows.size();++i){const auto& window=windows[i];
        for(const size_t o:window.records)if(owners.at({window.buffer,window.offset+uint64_t(o)*sizeof(float)}).base==window.offset)
            result.records[i].push_back(o);
        if(!result.records[i].empty())for(const auto& role:window.roles)result.roles.try_emplace(role);
    }
    // A retained role can use several layouts, including in another window
    // wholly owned by a closer alias this pair. Cache every validated layout
    // for that role in case the alias is absent in the next pair.
    for(const auto& window:windows)for(const auto& role:window.roles)if(auto retained=result.roles.find(role);retained!=result.roles.end())
        retained->second.insert(retained->second.end(),window.records.begin(),window.records.end());
    for(auto& [role,offsets]:result.roles){std::sort(offsets.begin(),offsets.end());offsets.erase(std::unique(offsets.begin(),offsets.end()),offsets.end());}
    return result;
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
