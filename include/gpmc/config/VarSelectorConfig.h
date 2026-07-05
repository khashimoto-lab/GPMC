#pragma once

#include <gpmc/config/TDScorerConfig.h>
#include <gpmc/config/VarSelectMode.h>

namespace gpmc {

// Static, user-set tuning values, shared across whichever selectors use them
// (currently the weighted-sum ones; some modes ignore parts of this).
// Contrast with VarSelectorArgs: that is the live search state a selector
// reads via bind(); this is decided up front and never changes during a run.
struct SelectorParams {
    double w_freq = 1.0;
    double w_act  = 1.0;
    double w_td   = 1.0;
};

struct VarSelectorConfig {
    VarSelectMode   mode = VarSelectMode::VSADS_TD_Lex;
    SelectorParams  params;
    TDScorerConfig  td;
};

}
