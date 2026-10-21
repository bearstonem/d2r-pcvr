#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace framevr::coop {
// Diagnostic handshake only. A successful exchange does NOT authorize a
// remote combat action. Native pointers/PlayerHandles never go over the wire.
inline constexpr uint64_t ChannelToken=0x50435652434f4f01ULL;
inline constexpr std::size_t PacketSize=32;
enum class Kind:uint16_t {Hello=1,Ready=2,RulesMismatch=3,BuildMismatch=4};
struct Message {Kind kind=Kind::Hello;uint64_t nonce=0,rules=0,build=0;};
inline uint64_t Read(std::span<const uint8_t> bytes,std::size_t at,std::size_t count) {
    uint64_t value=0;for(std::size_t i=0;i<count;++i)value|=uint64_t(bytes[at+i])<<(8*i);return value;
}
inline void Write(std::span<uint8_t> bytes,std::size_t at,std::size_t count,uint64_t value) {
    for(std::size_t i=0;i<count;++i)bytes[at+i]=uint8_t(value>>(8*i));
}
inline auto Encode(const Message& m) {
    std::array<uint8_t,PacketSize> bytes{};
    Write(bytes,0,4,0x31524350);Write(bytes,4,2,1);Write(bytes,6,2,uint16_t(m.kind));
    Write(bytes,8,8,m.nonce);Write(bytes,16,8,m.rules);Write(bytes,24,8,m.build);return bytes;
}
inline bool Decode(std::span<const uint8_t> bytes,Message& message) {
    if(bytes.size()!=PacketSize||Read(bytes,0,4)!=0x31524350||Read(bytes,4,2)!=1)return false;
    const auto kind=Read(bytes,6,2);
    if(kind<1||kind>4||Read(bytes,8,8)==0)return false;
    message={Kind(kind),Read(bytes,8,8),Read(bytes,16,8),Read(bytes,24,8)};return true;
}
inline constexpr uint64_t BuildHash(const char* text) {
    uint64_t hash=14695981039346656037ULL;
    for(;*text;++text){hash^=uint8_t(*text);hash*=1099511628211ULL;}return hash;
}
inline Kind Reply(const Message& m,uint64_t rules,uint64_t build) {
    return m.build!=build?Kind::BuildMismatch:m.rules!=rules?Kind::RulesMismatch:Kind::Ready;
}
struct Receipt {
    uint64_t nonce=0,at=0;Kind result=Kind::Hello;
    void Reset(uint64_t next){nonce=next;at=0;result=Kind::Hello;}
    bool Accept(const Message& message,uint64_t now) {
        if(!nonce||message.nonce!=nonce||message.kind==Kind::Hello)return false;
        at=now;result=message.kind;return true;
    }
    bool Ready(uint64_t now)const{return nonce&&at&&now>=at&&now-at<=6000&&result==Kind::Ready;}
};
}
