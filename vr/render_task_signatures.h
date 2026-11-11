#pragma once
#include <cstdint>
// The game's render graph, for the draw sampler's task times ([debug] draw_sampler, steamvr/PERFORMANCE.md).
// D2R 3.3.93787 native ABI fingerprints; the same bytes sit at the same addresses in 3.4.95041. A build in
// which any of them differs gets no hook: the capture then reports the samples alone.
namespace rendertasks::abi {
// The task runner: one call runs one render task as a job. rcx -> {u64 task index, RenderGraph*}; the
// graph's tasks are slots of SlotSize bytes from its start. These first bytes are the runner's prologue up
// to where it has read the index, multiplied it by the slot size and added the graph: they prove the two
// fields and the slot size the hook reads. No branch and no address in them, so a hook may move any of them.
inline constexpr uint64_t RunRva=0xf18bc0;
inline constexpr uint8_t RunSig[]={0x40,0x56,0x41,0x54,0x48,0x83,0xec,0x48,0x4c,0x8b,0xe1,0x48,0x89,0x6c,0x24,0x78,0x48,0x8b,0x09,0x41,0xb0,0x01,0x48,0x69,0xf1,0xa0,0x02,0x00,0x00,0x48,0x89,0x7c,0x24,0x40,0x49,0x03,0x74,0x24,0x08};
inline constexpr uint32_t RunHookBytes=16;      // of RunSig: what the hook itself is installed over
inline constexpr uint64_t SlotSize=0x2a0;
// Where a task is registered: the slot is the graph + index * SlotSize, and the name pointer goes to slot + NameOffset.
inline constexpr uint64_t SlotRva=0xf1a71d;
inline constexpr uint8_t SlotSig[]={0x48,0x69,0xcf,0xa0,0x02,0x00,0x00,0x48,0x03,0xd9,0x48,0x8b,0xcb};
inline constexpr uint64_t NameRva=0xf1a72f;
inline constexpr uint8_t NameSig[]={0x48,0x8b,0x74,0x24,0x48,0x48,0x8b,0xc7,0x48,0x89,0xab,0x38,0x02,0x00,0x00};
inline constexpr uint64_t NameOffset=0x238;
inline constexpr uint64_t MaxTasks=32;
// The graph's drain: the draw thread stays in it, running jobs or spinning, until every task is done.
// Its first bytes up to the first branch: nothing in them moves with the code.
inline constexpr uint64_t DrainRva=0xf1bd50;
inline constexpr uint8_t DrainSig[]={0x40,0x53,0x56,0x57,0x48,0x83,0xec,0x30,0x80,0xb9,0x68,0x5f,0x00,0x00,0x00,0x48,0x8b,0xf9,0x4c,0x89,0x74,0x24,0x28};
inline constexpr uint32_t DrainHookBytes=18;
}
