#pragma once
// One component storage of the renderer's registry, as the skeleton hook reads it
// (vr/skeletons.cpp, "The hero's transform in the renderer's ECS"). A storage's first 0x40 bytes
// hold everything a look-up needs:
//   +0x00 vtable, +0x08 / +0x10 the sparse pages (begin, end), +0x20 / +0x28 the packed entities
//   (begin, end), +0x38 the components.
// Natively every guarded read is a system call, so the header is read as one block and parsed
// here, not field by field (seven reads for one entity's dense index were four of them).
// `read(destination, address, bytes)` is the caller's guarded read; it refuses what it must.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace framevr::ecsstorage {
inline constexpr size_t HeaderBytes = 0x40;
struct Header {
    uint64_t vtable = 0, sparseBegin = 0, sparseEnd = 0, packedBegin = 0, packedEnd = 0, components = 0;
};
inline Header Parse(const unsigned char raw[HeaderBytes]) {
    Header h;
    std::memcpy(&h.vtable, raw, 8);
    std::memcpy(&h.sparseBegin, raw + 0x08, 8);
    std::memcpy(&h.sparseEnd, raw + 0x10, 8);
    std::memcpy(&h.packedBegin, raw + 0x20, 8);
    std::memcpy(&h.packedEnd, raw + 0x28, 8);
    std::memcpy(&h.components, raw + 0x38, 8);
    return h;
}
template <class Read> bool ReadHeader(uint64_t storage, Read&& read, Header* out) {
    unsigned char raw[HeaderBytes];
    if (!storage || !read(raw, storage, sizeof raw)) return false;
    *out = Parse(raw);
    return true;
}
// The dense index of `entity`: its sparse page's slot (page = (entity >> 8) & 0xFFF, slot =
// entity & 0xFF, the index in the low 20 bits, all set = none), checked against the packed list.
template <class Read> bool DenseIndex(const Header& h, uint32_t entity, Read&& read, uint32_t* index) {
    if (h.sparseEnd < h.sparseBegin) return false;
    const uint64_t page = (entity >> 8) & 0xFFF;
    uint64_t slots = 0;
    if (page >= (h.sparseEnd - h.sparseBegin) / 8 || !read(&slots, h.sparseBegin + page * 8, sizeof slots) || !slots) return false;
    uint32_t value = 0;
    if (!read(&value, slots + uint64_t(entity & 0xFF) * 4, sizeof value)) return false;
    value &= 0xFFFFF;
    if (value == 0xFFFFF || h.packedEnd < h.packedBegin || value >= (h.packedEnd - h.packedBegin) / 4) return false;
    uint32_t back = 0;
    if (!read(&back, h.packedBegin + uint64_t(value) * 4, sizeof back) || back != entity) return false;
    *index = value;
    return true;
}
// Whether `entity` is still at dense index `index`: one read. The packed list is what the storage
// holds, each entity once, so the entry there being this entity (its version with it) is the
// check DenseIndex ends on, without the two sparse reads that lead to it.
template <class Read> bool DenseStill(const Header& h, uint32_t entity, uint32_t index, Read&& read) {
    if (h.packedEnd < h.packedBegin || index >= (h.packedEnd - h.packedBegin) / 4) return false;
    uint32_t back = 0;
    return read(&back, h.packedBegin + uint64_t(index) * 4, sizeof back) && back == entity;
}
// The index an entity was found at last time, kept by whoever looks the same entity up again and
// again (the skeleton hook: a body's transform, every pose). Only ever a hint: DenseIndex below
// checks it against the packed list, and walks the sparse pages when it no longer holds (the
// storage moves its last entity into a freed place).
struct DenseHint {
    uint32_t entity = 0, index = 0;
    bool known = false;
};
// DenseIndex with a hint: one read while the entity is where it was, else the hint's read and the
// walk's three. The index is the one the walk gives either way.
template <class Read> bool DenseIndex(const Header& h, uint32_t entity, Read&& read, uint32_t* index, DenseHint& hint) {
    if (hint.known && hint.entity == entity && DenseStill(h, entity, hint.index, read)) { *index = hint.index; return true; }
    hint.known = false;
    if (!DenseIndex(h, entity, read, index)) return false;
    hint.entity = entity; hint.index = *index; hint.known = true;
    return true;
}
}  // namespace framevr::ecsstorage
