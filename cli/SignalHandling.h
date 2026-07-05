#pragma once

#include <csignal>

namespace gpmc::cli {

// Cooperative-stop flag: cleared at startup, set by the SIGINT/SIGTERM
// handler. Pass its address to Counter::set_stop_flag() so the counting
// core polls it and returns through the normal path, preserving stats.
extern volatile std::sig_atomic_t g_stop;

// Which signal set g_stop (meaningful once g_stop is set).
extern volatile std::sig_atomic_t g_stop_sig;

// Set to 1 once the counting core is entered. Before that, nothing polls
// g_stop (e.g. FlowCutter's greedy ordering runs to completion), so the
// SIGINT/SIGTERM handler exits immediately instead of waiting for a
// cooperative stop that will never come.
extern volatile std::sig_atomic_t g_in_core;

// Registers SIGINT/SIGTERM (cooperative stop via g_stop, or immediate exit
// before g_in_core) and SIGABRT/SIGSEGV (print a diagnostic line, then
// re-raise with the default handler) for the current process.
void install_signal_handlers();

}
