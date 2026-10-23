// A sampling CPU profiler inside the process, for phones where perf events are off
// (Samsung): every thread gets a POSIX timer on its own CPU clock that raises SIGPROF
// after each millisecond of CPU time; the handler records the thread and the host PC,
// and a reporter thread writes the hottest functions per thread to the log.
#pragma once

#include <cstdint>

namespace dao::profiler
{
    // Starts sampling every thread of the process (new threads are picked up at each
    // report) and logs a report every reportSeconds. Function names come from the
    // dynamic symbol table (the recompiled guest functions are sub_<guest address>).
    void Start(int reportSeconds);
}
