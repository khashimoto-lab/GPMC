#pragma once
#include "include/gpmc/CNF.h"
#include "include/gpmc/Semiring.h"

namespace gpmc {

struct NormalizationResult {
    bool sat = true;
    int isolated_pvars = 0;
    int vars_before = 0;
    int vars_after = 0;
    int pvars_before = 0;
    int pvars_after = 0;
    int nsvars = 0;    // unused placeholder, always pvars_after
    int ndvars = 0;    // unused placeholder, always pvars_after
    int clauses_before = 0;
    int clauses_after = 0;
    int fixed_vars = 0;
    int dedup_removed = 0;
    double elapsed_sec = 0.0;

    SS multiplier;
};

class Normalizer {
public:
    // Rewrites cnf in place so it satisfies the five conditions the GPMC core
    // assumes, and nothing more:
    //   1. not UNSAT
    //   2. no unit clauses / fixed variables (BCP done)
    //   3. every variable occurs in some clause
    //   4. variable indices are packed 1..n with no gaps
    //   5. projected variables get lower indices than non-projected ones
    // The counting contribution of removed variables is returned via
    // multiplier (weighted) / isolated_pvars (unweighted).
    NormalizationResult normalize(CNF& cnf) const;
};

}
