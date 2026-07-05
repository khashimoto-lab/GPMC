#include "counter/ComponentAnalyzer.h"

#include <algorithm>
#include <cassert>

namespace gpmc {

void ComponentAnalyzer::init(int nvars, int nsvars, int ndvars,
                              const Glucose::vec<Glucose::CRef>& clauses,
                              const Glucose::ClauseAllocator& ca)
{
    nsvars_ = nsvars;
    ndvars_ = ndvars;

    std::vector<std::vector<Var>> bin_adj(nvars);
    std::vector<std::vector<int>> occ(nvars);

    for (int i = 0; i < clauses.size(); i++) {
        const Glucose::Clause& c = ca[clauses[i]];
        assert(c.size() >= 2);
        if (c.size() == 2) {
            bin_adj[Glucose::var(c[0])].push_back(Glucose::var(c[1]));
            bin_adj[Glucose::var(c[1])].push_back(Glucose::var(c[0]));
        } else {   // size > 2 (BCP is done and satisfied clauses removed, so no unit/binary here)
            clauses_.push(ca_.alloc(c));
            int cid = clauses_.size() - 1;
            for (int j = 0; j < c.size(); j++)
                occ[Glucose::var(c[j])].push_back(cid);
        }
    }

    // CSR occurrence list: per var, [adjacent vars] VAR_UNDEF [long-clause ids] CL_UNDEF
    occ_start_.resize(nvars);
    occ_pool_.clear();
    for (Var v = 0; v < nvars; v++) {
        occ_start_[v] = static_cast<int>(occ_pool_.size());

        auto& bl = bin_adj[v];
        std::sort(bl.begin(), bl.end());
        bl.erase(std::unique(bl.begin(), bl.end()), bl.end());
        for (Var u : bl) occ_pool_.push_back(u);
        occ_pool_.push_back(VAR_UNDEF);

        for (int cid : occ[v]) occ_pool_.push_back(cid);
        occ_pool_.push_back(CL_UNDEF);
    }

    var_freq_.assign(nvars, 0);
    var_state_.assign(nvars, SplitState::INACTIVE);
    cl_state_.assign(clauses_.size(), SplitState::INACTIVE);
    bfs_queue_.reserve(nvars);

    PackedComponent::initBits(nvars, static_cast<int>(clauses_.size()));
}

void ComponentAnalyzer::setupContext(const Component&                    super_comp,
                                     const Glucose::vec<Glucose::lbool>& assigns)
{
    for (int i = 0; super_comp[i] != VAR_UNDEF; i++) {
        Var v = super_comp[i];
        if (assigns[v] == l_Undef) {
            var_state_[v] = SplitState::ACTIVE;
            var_freq_[v] = 0;
        } else {
            var_state_[v] = SplitState::INACTIVE;
        }
    }
    int cls_start = super_comp.nVars() + 1;
    for (int i = cls_start; super_comp[i] != CL_UNDEF; i++)
        cl_state_[super_comp[i]] = SplitState::ACTIVE;
}

Component* ComponentAnalyzer::exploreCompOf(Var seed, const Component& super_comp,
                                             const Glucose::vec<Glucose::lbool>& assigns)
{
    assert(varIsActive(seed));
    bfs_queue_.clear();
    bfs_queue_.push_back(seed);
    var_state_[seed] = SplitState::IN_COMP;

    int ncls_in_comp   = 0;

    for (int qi = 0; qi < static_cast<int>(bfs_queue_.size()); qi++) {
        Var v = bfs_queue_[qi];

        int p = occ_start_[v];

        for (; occ_pool_[p] != VAR_UNDEF; p++) {
            Var u = occ_pool_[p];
            if (varIsActive(u)) {
                var_state_[u] = SplitState::IN_COMP;
                bfs_queue_.push_back(u);
                var_freq_[v]++;
                var_freq_[u]++;
            }
        }
        p++;

        for (; occ_pool_[p] != CL_UNDEF; p++) {
            int cid = occ_pool_[p];
            if (!clIsActive(cid)) continue;

            const Glucose::Clause& cl = ca_[clauses_[cid]];
            int satisfied_at = -1;
            int queue_before = static_cast<int>(bfs_queue_.size());

            for (int j = 0; j < cl.size(); j++) {
                Glucose::lbool val = assigns[Glucose::var(cl[j])] ^ Glucose::sign(cl[j]);
                if (val == l_True) { satisfied_at = j; break; }
                if (val == l_Undef) {
                    Var u = Glucose::var(cl[j]);
                    var_freq_[u]++;
                    if (varIsActive(u)) {
                        var_state_[u] = SplitState::IN_COMP;
                        bfs_queue_.push_back(u);
                    }
                }
            }

            if (satisfied_at >= 0) {   // clause is satisfied: undo freq bumps and queue pushes
                for (int j = 0; j < satisfied_at; j++)
                    if (assigns[Glucose::var(cl[j])] == l_Undef)
                        var_freq_[Glucose::var(cl[j])]--;
                for (int k = queue_before; k < static_cast<int>(bfs_queue_.size()); k++)
                    var_state_[bfs_queue_[k]] = SplitState::ACTIVE;
                bfs_queue_.resize(queue_before);
                cl_state_[cid] = SplitState::INACTIVE;
            } else {
                cl_state_[cid] = SplitState::IN_COMP;
                ncls_in_comp++;
            }
        }
    }

    if (bfs_queue_.size() == 1) {
        assert(ncls_in_comp == 0);
        var_state_[seed] = SplitState::CLAIMED;
        return nullptr;
    }

    auto* comp = new Component();
    comp->setNVars(static_cast<int>(bfs_queue_.size()));
    comp->setNCls(ncls_in_comp);
    comp->reserve(static_cast<int>(bfs_queue_.size()), ncls_in_comp);

    int nsvars_in_comp = 0;
    int ndvars_in_comp = 0;
    bool past_dvars = false;
    for (int i = 0; super_comp[i] != VAR_UNDEF; i++) {
        Var v = super_comp[i];
        if (var_state_[v] != SplitState::IN_COMP) continue;
        var_state_[v] = SplitState::CLAIMED;
        comp->pushVar(v);
        if (!past_dvars) {
            if (isSVar(v))       nsvars_in_comp++;
            else if (isDVar(v))  ndvars_in_comp++;
            else                 past_dvars = true;
        }
    }
    ndvars_in_comp += nsvars_in_comp;
    comp->setNSVarsInComp(nsvars_in_comp);
    comp->setNDVarsInComp(ndvars_in_comp);

#ifndef NDEBUG

    {
        int nv = comp->nVars();
        for (int i = 1; i < nv; i++)
            assert((*comp)[i-1] < (*comp)[i] && "component vars not strictly ascending");
        for (int i = 0; i < ndvars_in_comp; i++)
            assert((*comp)[i] < ndvars_ && "non-D-set var inside [0,nDVarsInComp())");
        for (int i = ndvars_in_comp; i < nv; i++)
            assert((*comp)[i] >= ndvars_ && "D-set var outside [0,nDVarsInComp())");
    }
#endif

    comp->closeVars();

    int cls_start = super_comp.nVars() + 1;
    for (int i = cls_start; super_comp[i] != CL_UNDEF; i++) {
        int cid = super_comp[i];
        if (cl_state_[cid] == SplitState::IN_COMP) {
            cl_state_[cid] = SplitState::CLAIMED;
            comp->pushClause(cid);
        }
    }
    comp->closeClauses();

    return comp;
}

}
