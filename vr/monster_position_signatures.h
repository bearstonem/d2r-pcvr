#pragma once
#include <cstdint>
namespace monsterimmunity::abi {
// Native label context getter, manager hash layout, model root and transform
// layout, verified read-only against D2RLoader 3.3.93847 on 2026-11-03.
inline constexpr uint64_t ContextRva=0x685750;
inline constexpr uint64_t ContextLayoutRva=0x685777;
inline constexpr uint8_t ContextSig[]={0x48,0x83,0xec,0x28,0x65,0x48,0x8b,0x04,0x25,0x58,0,0,0};
inline constexpr uint8_t ContextLayoutSig[]={0x48,0x8d,0x05};
inline constexpr uint64_t ManagerLayoutRva=0x76b332;
inline constexpr uint64_t RootLayoutRva=0x76a88a;
inline constexpr uint8_t ManagerLayoutSig[]={0x48,0x8b,0x91,0x40,0x17,0,0};
inline constexpr uint64_t ManagerBucketsRva=0x76b37c;
inline constexpr uint64_t ModelLayoutRva=0x76b3b3;
inline constexpr uint64_t UnitKeyRva=0x76a809;
inline constexpr uint8_t ManagerBucketsSig[]={0x49,0x8b,0x82,0x38,0x17,0,0,0x48,0xff,0xc8,0x48,0x23,0xc1};
inline constexpr uint8_t ModelLayoutSig[]={0x4c,0x8b,0x42,0x18,0x48,0x8b,0x52,0x20};
inline constexpr uint8_t UnitKeySig[]={0x4c,0x8b,0x82,0xd8,0,0,0};
inline constexpr uint8_t RootLayoutSig[]={0x41,0x8b,0xbe,0xa8,0,0,0};
inline constexpr uint64_t TransformTypeRva=0x20b8f0;
inline constexpr uint8_t TransformTypeSig[]={0x48,0x83,0xec,0x28,0x65,0x48,0x8b,0x04,0x25,0x58,0,0,0};
inline constexpr uint64_t TransformLayoutRva=0x970669;
inline constexpr uint8_t TransformLayoutSig[]={0x48,0x69,0xc8,0xd0,0,0,0,0x48,0x8b,0x43,0x38};
}
