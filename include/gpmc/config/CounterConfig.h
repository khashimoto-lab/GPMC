#pragma once

#include <cstddef>
#include <functional>

#include <gpmc/Semiring.h>

namespace gpmc {

struct CounterConfig {

    // Component cache.
    size_t        cache_mb        = 4000;
    int           cache_init_pow2 = 20;
    bool          cache_unsat     = false;

    // PMC/PWMC: how non-projection components are handled.
    bool use_exists_check  = false;
    bool learn_unsat_comp  = false;

    // Counting mode: MC vs PMC. Combined with semiring_factory below, this
    // also selects WMC/PWMC.
    bool projected = false;

    // Fixed-level in-processing simplification during the search.
    bool inproc_simplify = true;

    // Which algebra to count in (zero/one/dup come off a prototype built from
    // this). Per-literal weight parsing is the CNF's concern (readDimacs takes
    // a semiring prototype), not the counter's.
    std::function<SS()>  semiring_factory;

};

}
