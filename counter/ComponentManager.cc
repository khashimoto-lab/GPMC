#include "counter/ComponentManager.h"

#include <cassert>

#include "include/gpmc/semirings/Integer.h"

namespace gpmc {

void ComponentManager::init(int nvars, int nsvars, int ndvars,
                             const Glucose::vec<Glucose::CRef>& clauses,
                             const Glucose::ClauseAllocator&    ca,
                             size_t cache_mb,
                             int cache_init_pow2)
{
    ndvars_    = ndvars;
    analyzer_.init(nvars, nsvars, ndvars, clauses, ca);
    cache_.init(cache_mb, cache_init_pow2);

    comp_stack_.clear();
    decisions_.clear();

    auto* root = new Component();
    root->setNVars(nvars);
    root->setNSVarsInComp(nsvars);
    root->setNDVarsInComp(ndvars);

    for (Var v = 0; v < nvars; v++) root->pushVar(v);
    root->closeVars();

    int ncls = analyzer_.numLongClauses();
    root->setNCls(ncls);
    for (int i = 0; i < ncls; i++) root->pushClause(i);
    root->closeClauses();

    comp_stack_.push_back(root);

    CacheEntryID root_id = cache_.newRootEntry();
    root->setId(root_id);

    decisions_.emplace_back(static_cast<int>(comp_stack_.size()), ndvars_);
    decisions_.back().changeBranch();

    fixed_level_     = 0;
}

int ComponentManager::splitComponent(const Glucose::vec<Glucose::lbool>& assigns,
                                      const std::vector<SS>* lit_weight)
{
    split_calls_++;

    Component& super = *comp_stack_.back();
    analyzer_.setupContext(super, assigns);

    int new_count = 0;

    for (int i = 0; super[i] != VAR_UNDEF; i++) {
        Var v = super[i];
        if (!analyzer_.varIsActive(v)) continue;

        Component* comp = analyzer_.exploreCompOf(v, super, assigns);
        if (!comp) {

            if (analyzer_.isDVar(v)) {

                if (lit_weight) {
                    Glucose::Lit pos = Glucose::mkLit(v, false);
                    Glucose::Lit neg = Glucose::mkLit(v, true);
                    SS sum = (*lit_weight)[Glucose::toInt(pos)]->add(
                                *(*lit_weight)[Glucose::toInt(neg)]);
                    topDecision().increaseModels(*sum);
                } else {
                    topDecision().increaseModels(Integer(2));
                }
            }
            continue;
        }

        PackedComponent pc(*comp);
        CacheEntryID id = cache_.lookup(pc);

        if (id != INVALID_ENTRY) {
            const CachedBucket& e = cache_.entry(id);
            if (e.isZero()) {

                topDecision().setCompTop(static_cast<int>(comp_stack_.size()));
                topDecision().markBranchUnsat();
                delete comp;
                return -1;
            }

            topDecision().increaseModels(e.modelCount());
            delete comp;
        } else {

            id = cache_.newEntry(std::move(pc));
            comp->setId(id);
            cache_.setDeletePermitted(id, false);
            cache_.addDescendant(super.id(), id);
            comp_stack_.push_back(comp);
            new_count++;
        }
    }

    topDecision().setCompTop(static_cast<int>(comp_stack_.size()));

    components_created_ += new_count;
    if (new_count >= 2)      split_multi_++;
    else if (new_count == 1) split_single_++;

    return new_count;
}

void ComponentManager::popComponent() {
    assert(!comp_stack_.empty());
    cache_.setDeletePermitted(comp_stack_.back()->id(), true);
    delete comp_stack_.back();
    comp_stack_.pop_back();
    decisions_.back().nextComp();
}

void ComponentManager::pushDecision() {
    decisions_.emplace_back(static_cast<int>(comp_stack_.size()), ndvars_);
}

void ComponentManager::increaseTopModels(const Semiring& val) {
    topDecision().increaseModels(val);
}

void ComponentManager::propagateTopResult(const SS& val) {
    if (val)
        prevDecision().increaseModels(*val);
    else
        prevDecision().markBranchUnsat();
}

void ComponentManager::storeModelCount(CacheEntryID id, SS count) {
    cache_.store(id, std::move(count));
}

void ComponentManager::storeZeroCount(CacheEntryID id) {
    cache_.storeZero(id);
}

void ComponentManager::removeCachePollutions() {
    if (!comp_stack_.empty())
        cache_.cleanDescendants(comp_stack_.back()->id(), topDecision().numComps());
}

}
