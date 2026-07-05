#pragma once
#include <vector>

#include "core/SolverTypes.h"

#include "counter/Component.h"
#include "include/gpmc/Types.h"

namespace gpmc {

class ComponentAnalyzer {
public:
    void init(int nvars, int nsvars, int ndvars,
              const Glucose::vec<Glucose::CRef>& clauses,
              const Glucose::ClauseAllocator&    ca);

    void setupContext(const Component&                    super_comp,
                      const Glucose::vec<Glucose::lbool>& assigns);

    Component* exploreCompOf(Var seed, const Component& super_comp,
                             const Glucose::vec<Glucose::lbool>& assigns);

    int  frequency(Var v)     const { return var_freq_[v]; }
    // Var numbering is [S-set][rest of D-set][non-D-set], so nsvars_ <= ndvars_
    // and both checks reduce to a range test.
    bool isSVar(Var v)        const { return v < nsvars_; }
    bool isDVar(Var v)        const { return v < ndvars_; }
    int  numLongClauses()     const { return clauses_.size(); }
    bool varIsActive(Var v)   const { return var_state_[v] == SplitState::ACTIVE; }

    const std::vector<int>& freqRef() const { return var_freq_; }

private:

    // ownership during one split pass: INACTIVE (out) -> ACTIVE (in super-comp, free)
    //                               -> IN_COMP (being collected) -> CLAIMED (assigned to a comp)
    enum class SplitState : uint8_t { INACTIVE, ACTIVE, IN_COMP, CLAIMED };

    std::vector<int> occ_pool_;   // CSR data: per var, adjacent vars then long-clause ids
    std::vector<int> occ_start_;  // occ_pool_ offset where var v's block begins

    Glucose::ClauseAllocator ca_;
    Glucose::vec<Glucose::CRef> clauses_;

    std::vector<int>        var_freq_;
    std::vector<SplitState> var_state_;
    std::vector<SplitState> cl_state_;

    std::vector<Var> bfs_queue_;

    int nsvars_ = 0;
    int ndvars_ = 0;

    bool clIsActive(int cid) const { return cl_state_[cid] == SplitState::ACTIVE; }
};

}
