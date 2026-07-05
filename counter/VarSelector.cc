#include "counter/VarSelector.h"

#include "counter/Component.h"

namespace gpmc {

Var VarSelector::select(const Component& comp) const {
    Var    best       = VAR_UNDEF;
    double best_score = -1.0;
    for (int i = 0; i < comp.nDVarsInComp(); i++) {
        double s = computeScore(comp[i]);
        if (s > best_score) {
            best_score = s;
            best       = comp[i];
        }
    }
    return best;
}

}
