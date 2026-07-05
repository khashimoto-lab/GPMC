#pragma once

namespace gpmc {

// Which branching heuristic the counter uses. This is the public, user-facing
// choice (set via Counter::selectMode); the concrete selectors it maps to live
// in counter/ and are not part of the public surface.
enum class VarSelectMode {
    VSADS        = 0,
    VSADS_Lex    = 1,
    VSADS_TD     = 2,
    VSADS_TD_Lex = 3,
    VSIDS        = 4,
    DLCS         = 5,
};

}
