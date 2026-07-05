#pragma once
#include <vector>

#include "core/SolverTypes.h"

#include "counter/ComponentAnalyzer.h"
#include "counter/ComponentCache.h"
#include "counter/Decision.h"
#include "include/gpmc/Types.h"

namespace gpmc {

class ComponentManager {
public:
    // setup
    void init(int nvars, int nsvars, int ndvars,
              const Glucose::vec<Glucose::CRef>& clauses,
              const Glucose::ClauseAllocator&    ca,
              size_t cache_mb,
              int cache_init_pow2);

    // forward the interruption flag down to the cache's eviction sweep
    void set_stop_flag(volatile std::sig_atomic_t* flag) { cache_.set_stop_flag(flag); }

    // component stack
    int splitComponent(const Glucose::vec<Glucose::lbool>& assigns,
                       const std::vector<SS>* lit_weight = nullptr);
    Component& topComponent() { return *comp_stack_.back(); }
    void       popComponent();

    // decision stack
    Decision& topDecision()  { return decisions_.back(); }
    Decision& prevDecision() { return *(decisions_.end() - 2); }
    void pushDecision();
    void popDecision() { decisions_.pop_back(); }

    // D-set membership stamp (current decision)
    void setDVarInComp()           { topDecision().setDVarInComp(*comp_stack_.back()); }
    bool isDVarInComp(Var v) const { return decisions_.back().isDVarInComp(v); }

    // model-count aggregation
    void increaseTopModels(const Semiring& val);
    void propagateTopResult(const SS& val);

    // cache writes
    void storeModelCount(CacheEntryID id, SS count);
    void storeZeroCount(CacheEntryID id);
    void removeCachePollutions();

    // fixed-level fast path
    bool checkfixedDL() {
        if (fixed_level_ + 1 == (int)decisions_.size()) {
            const Decision& d = decisions_.back();
            if (!d.isFirstBranch() && d.numUnprocessedComp() == 1) {
                fixed_level_++;
                return true;
            }
        }
        return false;
    }

    // statistics
    int                     frequency(Var v) const { return analyzer_.frequency(v); }
    const std::vector<int>& freqRef()         const { return analyzer_.freqRef(); }
    void printStats() const {
        std::printf("c o   %-22s %lu\n", "split_calls",        split_calls_);
        std::printf("c o   %-22s %lu\n", "components_created",  components_created_);
        std::printf("c o   %-22s %lu\n", "split_multi",         split_multi_);
        std::printf("c o   %-22s %lu\n", "split_single",        split_single_);
        cache_.printStats();
    }

private:
    // owned modules
    ComponentAnalyzer analyzer_;
    ComponentCache    cache_;

    // search stacks
    std::vector<Decision>   decisions_;
    std::vector<Component*> comp_stack_;

    // scalar state
    int ndvars_      = 0;
    int fixed_level_ = 0;

    // statistics
    uint64_t split_calls_        = 0;
    uint64_t components_created_ = 0;
    uint64_t split_multi_        = 0;
    uint64_t split_single_       = 0;
};

}
