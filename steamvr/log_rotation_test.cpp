// log_rotation.h: when a log is put aside, what its kept copy is called, and where a session's log stops.
#include "log_rotation.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace framevr::logrotation;

int main() {
    // The session before is kept beside the log, its extension kept so it opens like any log.
    assert(PreviousName(std::wstring(L"C:\\Game\\mods\\D2R-PCVR\\d2rloader\\logs\\d2r-vr-frame.log")) ==
           L"C:\\Game\\mods\\D2R-PCVR\\d2rloader\\logs\\d2r-vr-frame.previous.log");
    assert(PreviousName(std::string("d2rloader/logs/pcvr-launch.log")) == "d2rloader/logs/pcvr-launch.previous.log");
    assert(PreviousName(std::string("a.b/log")) == "a.b/log.previous");            // a dot in a folder is not an extension
    assert(PreviousName(std::string("logs/.hidden")) == "logs/.hidden.previous");  // nor is a leading one
    assert(PreviousName(std::string("frame")) == "frame.previous");
    assert(PreviousName(std::string("frame.tar.log")) == "frame.tar.previous.log");

    // At a start: whatever the last session wrote is put aside; nothing, or an empty file, is not.
    assert(RotateAtStart(true, 1));
    assert(RotateAtStart(true, 259ull << 20));
    assert(!RotateAtStart(true, 0));
    assert(!RotateAtStart(false, 0) && !RotateAtStart(false, 500));

    // Within a session: whole batches up to the cap, the first that would pass it ends the log.
    assert(SessionCapBytes == 64ull << 20);
    assert(Fits(0, 100, 1000) && Fits(900, 100, 1000));
    assert(!Fits(901, 100, 1000) && !Fits(1000, 1, 1000));
    assert(Fits(1000, 0, 1000) && Fits(5000, 0, 1000));   // nothing to write always fits
    assert(!Fits(5000, 1, 1000));                          // past the cap already (never by this rule; no wrap-around)
    assert(!Fits(0, ~0ull, 1000) && !Fits(~0ull, 2, ~0ull));
    assert(Fits(SessionCapBytes - 655360, 655360, SessionCapBytes));   // the logger's largest batch, exactly to the cap

    char line[240];
    const int n = LimitLine(SessionCapBytes, line, sizeof line);
    assert(n > 0 && size_t(n) < 200 && !std::strchr(line, '\n'));   // one line, within the logger's buffer
    assert(std::strstr(line, "64 MB limit") && std::strstr(line, ".previous.log"));

    std::puts("Log rotation: previous-session name, start rule and the 64 MB session cap");
    return 0;
}
