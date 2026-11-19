#pragma once
#include "interactions.h"
#include <string>
#include <memory>
#include <vector>
namespace framevr::interaction::icons {
struct Image {unsigned width=0,height=0;std::vector<uint8_t> rgba;};
inline std::string ItemKey(Item item) {
    if(!item.handle||!item.code)return {};
    if(item.qualityRecord>=0&&(item.quality==5||item.quality==7))
        return std::string(item.quality==7?"u":"t")+std::to_string(item.qualityRecord);
    // Native codes can have NUL or space padding; the cache uses spaces.
    uint32_t code=item.code;for(int i=0;i<4;++i)if(!(code&(255u<<(i*8))))code|=32u<<(i*8);
    constexpr char hex[]="0123456789abcdef";std::string key="i00000000";
    for(int i=0;i<8;++i)key[8-i]=hex[(code>>(i*4))&15];
    return key;
}
// Get only queues work or returns immutable decoded pixels. File I/O and WIC
// decoding run on a worker, never in the headset's frame submission thread.
std::shared_ptr<const Image> Get(const std::string& key);
std::shared_ptr<const Image> Get(Item item);
void Prefetch(const Snapshot&);
uint64_t Revision();
// A picture the plugin makes itself for a key no manifest has (the Army X menu's entry): asked on the decode thread before
// the cache's file; null: not its key. wic: the thread's IWICImagingFactory.
using BuiltIn=std::shared_ptr<const Image>(*)(const std::string& key,void* wic);
void SetBuiltIn(BuiltIn decode);
void Shutdown();
// Preview-only override; set before the first Get. Production uses DLL folder.
void Directory(const std::wstring& path);
bool Wait(unsigned milliseconds);
}
