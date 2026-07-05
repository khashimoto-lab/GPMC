#pragma once
#include <optional>

#include "include/gpmc/CNF.h"

#include "include/gpmc/diagnostics/SetupReport.h"

namespace gpmc {

class Component;
struct VarSelectorArgs;

// Strategy for picking the next decision variable in a component.
class VarSelector {
public:
    virtual ~VarSelector() = default;

    // Short name for diagnostic output (e.g. "vsads_td_lex").
    virtual const char* name() const = 0;

    // Score of a candidate variable.
    virtual double computeScore(Var v) const { return 0.0; }

    // Choose a variable from the component.
    // Precondition: the component has >=1 selectable variable.
    virtual Var select(const Component& comp) const;

    // Called once after the formula is loaded. Precompute state that depends
    // only on the formula, e.g. a tree-decomposition score.
    virtual void prepareStatic(const CNF& cnf, int ndvars) {}

    // Called once before counting starts. Latch onto the search's dynamic
    // state (clause frequency, VSIDS activity) so select()/computeScore() see it.
    virtual void bind(const VarSelectorArgs& dyn) {}

    // Diagnostic summary of the tree-decomposition pass run by prepareStatic(),
    // for selectors that ran one. nullopt for selectors that don't use a TD.
    virtual std::optional<SetupReport::TD> tdReport() const { return std::nullopt; }
};

}
