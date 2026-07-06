#pragma once

#include <memory>
#include <vector>

#include <gpmc/CNF.h>
#include <gpmc/Semiring.h>
#include <gpmc/config/PreprocessorConfig.h>

namespace CaDiCaL { class Solver; }

namespace gpmc {

struct PreprocessingResult {
    bool sat            = true;

    SS   multiplier;
    int  vars_before    = 0;
    int  vars_after     = 0;
    int  pvars_before   = 0;
    int  pvars_after    = 0;
    int  nsvars         = 0;    // unused placeholder, always pvars_after
    int  ndvars         = 0;    // unused placeholder, always pvars_after
    int  fixed_vars     = 0;
    int  isolated_pvars = 0;
    int  clauses_before = 0;
    int  clauses_after  = 0;
    int  pp_rounds      = 0;
    double elapsed_sec  = 0.0;
};

class Preprocessor {
public:
    explicit Preprocessor(PreprocessorConfig cfg = {}) : config_(cfg) {}

    PreprocessingResult preprocess(CNF& cnf) const;

private:

    struct PrepState;

    // One round: phase 1 (no BVE) then phase 2 (solve + BVE). Phase 1 consumes
    // the previous round's solver: its clauses are forwarded into this round
    // and the solver is freed before the simplify runs.
    bool simplifyWithoutBVE(CNF& cnf, PrepState& st,
                            std::unique_ptr<CaDiCaL::Solver> prev_solverB,
                            CaDiCaL::Solver& solverB, double t0) const;

    bool solveAndSimplifyWithBVE(CNF& cnf, PrepState& st,
                                 CaDiCaL::Solver& solverB, double t0, int round) const;

    // foldEquivalences closes phase 1; applyDVE is phase 3 (definable
    // variable elimination), probing candidates via detectDefinable.
    void foldEquivalences(CNF& cnf, PrepState& st) const;
    void applyDVE(CNF& cnf, CaDiCaL::Solver& solver, PrepState& st, double t0) const;
    std::vector<int> detectDefinable(const std::vector<std::vector<int>>& clauses,
                                     const std::vector<bool>& base,
                                     const std::vector<int>& candidates,
                                     double time_limit_sec,
                                     int conflict_limit) const;

    // Final pass after the round loop: consumes the last round's solver,
    // gathers the surviving clauses, folds the remaining weight
    // contributions, and rebuilds the CNF -- via the helpers below, in order.
    void finalize(CNF& cnf, PrepState& st,
                  std::unique_ptr<CaDiCaL::Solver> solverB,
                  PreprocessingResult& res) const;
    void foldFixedWeights(const CNF& cnf, PrepState& st) const;
    void foldIsolatedVars(const CNF& cnf, PrepState& st, PreprocessingResult& res) const;
    void rebuildCNF(CNF& cnf, PrepState& st) const;

    PreprocessorConfig config_;
};

}
