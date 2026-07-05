#include "cli/SignalHandling.h"

#include <cstring>
#include <unistd.h>

namespace gpmc::cli {

volatile std::sig_atomic_t g_stop = 0;
volatile std::sig_atomic_t g_stop_sig = 0;
volatile std::sig_atomic_t g_in_core = 0;

namespace {

// async-signal-safe helpers: write a string literal and a decimal number
// (no printf allowed in signal handlers)
void write_str(int fd, const char* s) { write(fd, s, strlen(s)); }

void write_uint(int fd, int v) {
    char tmp[24]; int len = 0;
    if (v == 0) { tmp[len++] = '0'; }
    else { while (v > 0) { tmp[len++] = '0' + (v % 10); v /= 10; } }
    char buf[24]; int pos = 0;
    for (int i = len - 1; i >= 0; i--) buf[pos++] = tmp[i];
    write(fd, buf, pos);
}

void write_sigstatus(int fd, int sig) {
    write_str(fd, "c o status                   interrupted by signal ");
    write_uint(fd, sig);
    write(fd, "\n", 1);
}

void handle_interrupt(int sig) {
    g_stop = 1;
    g_stop_sig = sig;
    if (!g_in_core) {
        // Before the counting core (parsing / preprocessing / TD construction).
        // Nothing here polls g_stop, so cooperative stop cannot work. Exit
        // immediately after noting the signal. Everything here is
        // async-signal-safe (write only, no printf / destructors).
        write_str(STDOUT_FILENO,
                  "\nc o *** INTERRUPTED before counting by signal ");
        write_uint(STDOUT_FILENO, sig);
        write_str(STDOUT_FILENO, " ***\n");
        write_sigstatus(STDOUT_FILENO, sig);
        _exit(128 + sig);
    }
    // Inside the core: only raise the flag (async-signal-safe). The counting
    // loop polls it and returns through the normal path, which prints the
    // statistics. Do NOT _exit() here, or the stats are lost.
}

void handle_fatal(int sig) {
    const char* msg =
        (sig == SIGABRT) ? "\nc o *** ABORTED (signal 6) ***\n"
                         : "\nc o *** SEGFAULT (signal 11) ***\n";
    write(STDOUT_FILENO, msg, strlen(msg));
    write_sigstatus(STDOUT_FILENO, sig);
    signal(sig, SIG_DFL);
    raise(sig);
}

} // namespace

void install_signal_handlers() {
    signal(SIGINT,  handle_interrupt);
    signal(SIGTERM, handle_interrupt);
    signal(SIGABRT, handle_fatal);
    signal(SIGSEGV, handle_fatal);
}

}
