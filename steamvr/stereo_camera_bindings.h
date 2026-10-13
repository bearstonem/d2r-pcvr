#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace framevr::stereoreplay {
using CameraRole=std::pair<uint64_t,uint32_t>;
using CameraBindingKey=std::pair<uint64_t,uint64_t>; // resource identity, byte offset
struct CameraBindingUse {
    uint64_t buffer=0,offset=0,size=0;
    CameraRole cameraKey{};
    // Roles actually bound on this list, never aliases seen only on other lists.
    // Discovery consumes these; keeping the same bound after discovery also
    // gives resolved bindings a bounded role/offset union.
    std::set<CameraRole> discoveryAliases;
    std::vector<size_t> cachedOffsets;
};
struct RecentCameraBinding {uint64_t epoch=0,list=0,address=0;};
enum class CameraBindingResult {Recorded,WindowCapacity,ReferenceCapacity,RoleCapacity,LayoutCapacity};
struct CameraBindingLimits {
    size_t discoveryWindows=64,resolvedWindows=512,listReferences=4096,roleAliases=64,layoutOffsets=1024;
};

// The callback mutex serializes map operations. Only RecentMatches reads state
// outside that mutex, through the atomic epoch. A window's capture budget is
// global to the pair; its uses must remain present on every submitting list.
class CameraBindingRegistry {
public:
    using ListBindings=std::map<CameraBindingKey,CameraBindingUse>;
private:
    CameraBindingLimits limits;
    std::map<uint64_t,ListBindings> lists;
    std::set<CameraBindingKey> windows;
    size_t references=0;
    bool resolved=false;
    // Unique stamps also prevent a cache from one registry matching another.
    inline static std::atomic<uint64_t> nextEpoch{1};
    std::atomic<uint64_t> epoch{nextEpoch.fetch_add(1)};
    void InvalidateRecent(){epoch.store(nextEpoch.fetch_add(1),std::memory_order_release);}
public:
    explicit CameraBindingRegistry(CameraBindingLimits bounds={}):limits(bounds){}
    void Clear(){lists.clear();windows.clear();references=0;resolved=false;InvalidateRecent();}
    void BeginPair(bool layoutResolved){Clear();resolved=layoutResolved;}
    void ResetList(uint64_t list){
        if(const auto found=lists.find(list);found!=lists.end()){
            references-=found->second.size();lists.erase(found);
        }
        // Reset/destruction may reuse the same list address in this pair. Do
        // not refund the distinct-window budget or retain a TLS fast-path hit.
        InvalidateRecent();
    }
    bool RecentMatches(const RecentCameraBinding& recent,uint64_t list,uint64_t address)const noexcept {
        return recent.epoch==epoch.load(std::memory_order_acquire)&&recent.list==list&&recent.address==address;
    }
    void Remember(RecentCameraBinding& recent,uint64_t list,uint64_t address)const noexcept {
        recent={epoch.load(std::memory_order_acquire),list,address};
    }
    const ListBindings* List(uint64_t list)const {
        const auto found=lists.find(list);return found==lists.end()?nullptr:&found->second;
    }
    size_t UniqueWindows()const noexcept{return windows.size();}
    CameraBindingResult Add(uint64_t list,uint64_t buffer,uint64_t offset,uint64_t size,
                            CameraRole role,std::span<const size_t> knownOffsets={}) {
        const CameraBindingKey key{buffer,offset};
        const bool freshWindow=!windows.contains(key);
        if(freshWindow&&windows.size()>=(resolved?limits.resolvedWindows:limits.discoveryWindows))
            return CameraBindingResult::WindowCapacity;
        const auto currentList=lists.find(list);
        const CameraBindingUse* old=nullptr;
        if(currentList!=lists.end())if(const auto use=currentList->second.find(key);use!=currentList->second.end())old=&use->second;
        if(!old&&references>=limits.listReferences)return CameraBindingResult::ReferenceCapacity;
        if((!old||!old->discoveryAliases.contains(role))&&
           (old?old->discoveryAliases.size():0)>=limits.roleAliases)return CameraBindingResult::RoleCapacity;
        std::vector<size_t> merged;
        if(!knownOffsets.empty()){
            if(old)merged=old->cachedOffsets;
            for(const size_t offset:knownOffsets)if(std::find(merged.begin(),merged.end(),offset)==merged.end()){
                if(merged.size()>=limits.layoutOffsets)return CameraBindingResult::LayoutCapacity;
                merged.push_back(offset);
            }
            std::sort(merged.begin(),merged.end());
        }
        // Capacity failures leave the journal untouched. Repeated bindings
        // retain the same list entry; repeated Execute callbacks can reread it
        // to detect A -> B -> A mutations rather than observing only final A.
        windows.insert(key);
        auto [use,fresh]=lists[list].try_emplace(key,CameraBindingUse{buffer,offset,size,role,{},{}});
        if(fresh)++references;
        use->second.size=std::max(use->second.size,size);
        use->second.discoveryAliases.insert(role);
        if(!knownOffsets.empty())use->second.cachedOffsets=std::move(merged);
        return CameraBindingResult::Recorded;
    }
};
}
