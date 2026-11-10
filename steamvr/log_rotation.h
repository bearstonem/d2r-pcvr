// log_rotation.h: how the mod's own logs are kept from growing for ever. d2r-vr-frame.log was opened to append
// and never cut: a few megabytes a session, hundreds after some weeks of play.
//
//   At each start the log of the session before is renamed beside it, `<name>.previous.log`, taking the place of
//   the one kept there; the new session starts an empty file. A crash's log is therefore still there after the
//   next launch (as the .previous file), and gone after the one after that.
//   Within a session the file stops at SessionCapBytes with a last line saying so.
//   When the rename fails (another program has the file open, a read-only folder), the session appends to the
//   file as it always did: a log is never lost, and a launch never fails, over its rotation.
//
// This is the rule; steamvr/logging.cpp does the file work. steamvr/launch_windows.py keeps pcvr-launch.log the
// same way by size (it holds a few lines a launch).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace framevr::logrotation {

// A session's frame log stops here. Ordinary sessions write a few megabytes; only a diagnostic left on, or a
// line repeated every frame, comes near.
inline constexpr uint64_t SessionCapBytes = 64ull * 1024 * 1024;

// The file the last session's log is kept in: ".previous" before the name's extension, in the same folder.
template <class String>
String PreviousName(const String& path) {
    using Char = typename String::value_type;
    const auto slash = path.find_last_of(String{Char('\\'), Char('/')});
    const auto dot = path.find_last_of(Char('.'));
    const Char word[] = {'.', 'p', 'r', 'e', 'v', 'i', 'o', 'u', 's', 0};
    if (dot == String::npos || (slash != String::npos && dot < slash) || dot == (slash == String::npos ? 0 : slash + 1))
        return path + word;   // no extension to keep
    return path.substr(0, dot) + word + path.substr(dot);
}

// At a session's start: the log there is put aside when it holds anything. An empty one is simply used.
inline constexpr bool RotateAtStart(bool exists, uint64_t size) { return exists && size > 0; }

// A batch of whole lines, `batch` bytes, with `written` bytes already in this session's file: it is written while
// the session stays within the cap. The first that does not fit ends the log - never part of a batch, so the file
// ends on a whole line. An empty batch always fits.
inline constexpr bool Fits(uint64_t written, uint64_t batch, uint64_t cap) { return batch == 0 || (written <= cap && batch <= cap - written); }

// The text of the line that ends a capped log.
inline int LimitLine(uint64_t cap, char* out, size_t size) {
    return std::snprintf(out, size,
        "Frame logger: this session's log reached its %llu MB limit; nothing more is written until the next launch "
        "(the session before is in the .previous.log file beside this one)",
        (unsigned long long)(cap >> 20));
}

}  // namespace framevr::logrotation
