#pragma once

#include <chrono>

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <sys/resource.h>
#include <sys/time.h>
#endif

namespace gpmc {

// Seconds elapsed on a monotonic clock since an arbitrary fixed epoch.
// Only differences between two calls are meaningful (it is not wall-clock time).
//
// Use this for elapsed-time *reporting* and human-facing progress logs, where
// the quantity of interest is the real time a user/benchmark waits.
inline double now_sec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// User CPU time consumed by this process, in seconds (getrusage ru_utime).
//
// Use this for *time-limit* decisions (tree decomposition, preprocessing, DVE):
// unlike wall-clock, it is independent of machine load and preemption, so the
// "compute budget" stays reproducible even when several solver processes run
// concurrently on one machine.
inline double cpu_sec() {
#if defined(_MSC_VER) || defined(__MINGW32__)
    return (double)clock() / CLOCKS_PER_SEC;
#else
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return (double)ru.ru_utime.tv_sec + (double)ru.ru_utime.tv_usec / 1000000.0;
#endif
}

}
