#pragma once

namespace gpmc {

struct PreprocessorConfig {
    // Round and time budget.
    int    var_limit     = 1000000;  // above this, drop to a light pass (1 round, no DVE)
    int    rounds        = 3;
    int    max_rounds    = 20;
    double pp_time_limit = 120.0;  // CPU-time cap in sec; 0 = no limit

    // Internal SAT solver (solverB) control.
    int solve_conflict_limit = 100000;

    // Definability-based variable elimination (DVE).
    bool   pp_dve           = true;
    int    dve_min_pvars    = 10;   // skip DVE unless at least this many proj vars
    int    dve_low_freq_max = 4;    // also admit vars with min(fpos,fneg) <= this; 0 = off
    double dve_time_limit   = 120.0;  // CPU-time cap in sec

    // Misc.
    bool pp_verbose = false;
};

}
