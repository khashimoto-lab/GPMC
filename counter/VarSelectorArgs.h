#pragma once
#include <vector>

#include "extern/glucose/mtl/Vec.h"

namespace gpmc {

// The dynamic search state a selector latches onto in VarSelector::bind().
// These arrays live in Counter and keep changing during the count; the
// selector only holds references to them, it does not own them.
struct VarSelectorArgs {
    const std::vector<int>&      frequency;
    const Glucose::vec<double>&  activity;
    const double&                var_inc;
};

}
