#pragma once

#include <cstdint>
#include <vector>

#include "include/gpmc/CNF.h"
#include "include/gpmc/config/TDScorerConfig.h"

namespace gpmc {

struct TDScorerResult {
    bool computed = false;
    const char* skip_reason = nullptr;

    int    graph_edges       = 0;
    int    width             = -1;
    int    bags              = 0;
    double coef_used         = 0.0;
    double elapsed_sec       = 0.0;

    std::vector<double> score;
};

class TDScorer {
public:
    explicit TDScorer(TDScorerConfig cfg = {}) : config_(cfg) {}

    // With exp_coef the scores are scaled by a width-driven exponential
    // coefficient; otherwise they are the raw [0,1] centroid distance.
    TDScorerResult compute(const CNF& cnf, int ndvars, bool exp_coef) const;

private:
    TDScorerConfig config_;
};

}
