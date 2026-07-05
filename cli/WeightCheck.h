#pragma once

#include <gpmc/CNF.h>

namespace gpmc::cli {

// Preprocessor::preprocess() asserts (debug-only) that every projection
// variable carries both polarity weights in weighted mode; supplying a
// complete weighting is the caller's responsibility (see CNF::completeMccWeights()
// for the MCC one-sided-weight convention, opted into via --mode wmc/pwmc).
// A release build has no assert to catch a violation, so main() checks here
// first and exits with a message instead of segfaulting inside preprocess()/count().
inline bool check_weights_complete(const gpmc::CNF& cnf) {
    if (!cnf.isWeighted()) return true;
    for (int v = 0; v < cnf.numVars(); v++) {
        if (!cnf.isProj(v)) continue;
        if (!cnf.hasWeight(gpmc::mkLit(v, false)) || !cnf.hasWeight(gpmc::mkLit(v, true)))
            return false;
    }
    return true;
}

}
