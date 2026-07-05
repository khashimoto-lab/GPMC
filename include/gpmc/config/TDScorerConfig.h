#pragma once

#include <cstdint>

namespace gpmc {

struct TDScorerConfig {
    // Gates: skip the TD entirely unless the formula is worth decomposing.
    int     min_vars     = 20;       // too few vars: TD pointless
    int     var_limit    = 150000;   // too many vars: TD too costly
    int     min_dvars    = 10;       // need more projected vars than this
    double  dense_limit  = 0.10;     // primal-graph edge density ceiling
    double  ratio_limit  = 30.0;     // primal-graph edges/vars ceiling

    // Width guard: when the decomposition is too wide to trust, give up and
    // return no TD (scores stay zero) instead of a misleading gradient.
    double  tw_var_limit = 0.30;     // reject when width/ndvars >= this
    bool    width_guard  = false;

    // FlowCutter stop conditions. With both left at 0 FlowCutter runs a single
    // pass; otherwise it loops until whichever set limit fires.
    double  time_limit    = 0.0;     // CPU-time cap in seconds (0 = no time cap)
    int     iters         = 0;       // max FlowCutter passes (0 = no pass cap)

    // Score scaling (sum selectors only; see TDScorer::compute exp_coef).
    double  coef          = 100.0;

    // Centroid mass: weigh by all graph vars instead of projected vars only.
    bool    centroid_all_vars = false;
};

}
