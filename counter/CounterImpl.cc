#include "counter/CounterImpl.h"

#include <cassert>
#include <cstdio>

#include "counter/ComponentManager.h"
#include "counter/Normalizer.h"
#include "counter/VarSelectorArgs.h"
#include "counter/VarSelectorFactory.h"
#include "include/gpmc/Preprocessor.h"
#include "include/gpmc/semirings/Integer.h"

namespace gpmc {

CounterImpl::CounterImpl()
    : cmpmgr_(std::make_unique<ComponentManager>())
{}

CounterImpl::~CounterImpl() = default;

void CounterImpl::set_stop_flag(volatile std::sig_atomic_t* flag) {
    stop_ = flag;
    cmpmgr_->set_stop_flag(flag);
}

void CounterImpl::setup(CNF& cnf, bool normalize) {
    setup(cnf, VarSelectorFactory::create(selector_config), normalize);
}

void CounterImpl::setup(CNF& cnf, const PreprocessingResult& prep_result) {
    setup(cnf, VarSelectorFactory::create(selector_config), prep_result);
}

void CounterImpl::setup(CNF& cnf, std::unique_ptr<VarSelector> selector, bool normalize) {
    assert(!setup_done_ && "Counter::setup() called twice on the same Counter; construct a new Counter instead");
    setup_done_ = true;

    if (!config.projected)
        cnf.setAllProjected();

    assert(config.semiring_factory && "CounterConfig::semiring_factory must be set");
    prototype_ = config.semiring_factory();

    // Decide weighted vs. unweighted from the input CNF, before normalize may
    // fold all weighted vars away into multiplier_ and leave weights_ empty.
    is_weighted_ = cnf.isWeighted();

    int norm_nsvars = cnf.numProjVars();
    int norm_ndvars = cnf.numProjVars();
    if (normalize) {
        Normalizer nr;
        auto r = nr.normalize(cnf);
        if (!r.sat) {
            setup_report_.normalize = SetupReport::Normalize{.sat = false};
            trivial_ = true; ok = false; return;
        }
        npvars_isolated_ = r.isolated_pvars;
        multiplier_      = std::move(r.multiplier);
        norm_nsvars      = r.nsvars;
        norm_ndvars      = r.ndvars;

        SetupReport::Normalize nrep;
        nrep.vars_after     = r.vars_after;
        nrep.pvars_after    = r.pvars_after;
        nrep.clauses_after  = r.clauses_after;
        nrep.fixed_vars     = r.fixed_vars;
        nrep.isolated_pvars = r.isolated_pvars;
        nrep.dedup_removed  = r.dedup_removed;
        nrep.elapsed_sec    = r.elapsed_sec;
        setup_report_.normalize = nrep;
    } else if (cnf.multiplier()) {
        // No normalization, but the input already carries a folded-in multiplier
        // (e.g. a preprocessed CNF read back). Seed multiplier_ with it.
        multiplier_ = cnf.multiplier()->dup();
    } else if (cnf.isolatedVars() > 0) {
        // Unweighted counterpart: a read-back CNF whose free vars were folded
        // into a "MUST SHIFT BY" count. Seed npvars_isolated_ with it.
        npvars_isolated_ = cnf.isolatedVars();
    }

    is_projected_ = config.projected;
    nsvars_       = norm_nsvars;
    ndvars_       = norm_ndvars;

    reserveVars(cnf.numVars());
    loadClauses(cnf);

    initSemiring(cnf);
    trivial_ = (cnf.numVars() == 0);
    if (!trivial_) {
        selector_ = std::move(selector);
        prepareSelector(cnf);
    }
}

void CounterImpl::setup(CNF& cnf, std::unique_ptr<VarSelector> selector,
                     const PreprocessingResult& prep_result) {
    assert(!setup_done_ && "Counter::setup() called twice on the same Counter; construct a new Counter instead");
    setup_done_ = true;

    assert(config.semiring_factory && "CounterConfig::semiring_factory must be set");
    prototype_ = config.semiring_factory();

    if (!prep_result.sat) {
        setup_report_.normalize = SetupReport::Normalize{.sat = false};
        trivial_ = true; ok = false; return;
    }

    if (!config.projected)
        cnf.setAllProjected();

    multiplier_ = prep_result.multiplier
                      ? prep_result.multiplier->dup() : nullptr;
    npvars_isolated_ = prep_result.isolated_pvars;

    // Weighted iff the preprocessed CNF still carries weights or preprocessing
    // folded some into a weight factor; decide before initSemiring sees cnf.
    is_weighted_ = cnf.isWeighted() || prep_result.multiplier != nullptr;

    is_projected_ = config.projected;
    nsvars_       = prep_result.nsvars;
    ndvars_       = prep_result.ndvars;

    reserveVars(cnf.numVars());
    loadClauses(cnf);

    initSemiring(cnf);
    trivial_ = (cnf.numVars() == 0);
    if (!trivial_) {
        selector_ = std::move(selector);
        prepareSelector(cnf);
    }
}

void CounterImpl::prepareSelector(const CNF& cnf) {
    // Phase 1: let the selector precompute formula-only state (e.g. a TD
    // score). Dynamic state is wired later in count() via selector_->bind().
    selector_->prepareStatic(cnf, ndvars_);

    setup_report_.selector_name = selector_->name();
    setup_report_.td            = selector_->tdReport();
}

void CounterImpl::reserveVars(int n) {
    watches   .init(Glucose::mkLit(n - 1, true));
    watchesBin.init(Glucose::mkLit(n - 1, true));
    assigns   .growTo(n, l_Undef);
    vardata   .growTo(n, mkVarData(Glucose::CRef_Undef, 0));
    activity  .growTo(n, 0);
    seen      .growTo(n, 0);
    permDiff  .growTo(n, 0);
    polarity  .growTo(n, true);
    decision  .growTo(n);
    trail     .capacity(n);
    for (int i = 0; i < n; i++)
        setDecisionVar(i, true);
}

// Copy the CNF clauses into the solver's clause database. This is the one
// place where CNF-side literals cross into backend-solver literals.
void CounterImpl::loadClauses(const CNF& cnf) {
    Glucose::vec<Glucose::Lit> gcl;
    for (const auto& c : cnf.clauses()) {
        gcl.clear();
        for (Lit l : c) gcl.push(Glucose::toLit(toInt(l)));
        Glucose::CRef cr = ca.alloc(gcl, false);
        clauses.push(cr);
        attachClause(cr);
    }
}

void CounterImpl::initSemiring(CNF& cnf) {
    // is_weighted_ is decided by the caller (setup) from the input CNF before
    // normalize/preprocess can fold weighted vars away and empty weights_.
    if (is_weighted_) {
        lit_weight_.resize(2 * ndvars_);
        for (int v = 0; v < ndvars_; v++) {
            Lit pos = mkLit(v), neg = ~pos;
            lit_weight_[toInt(pos)] = cnf.weight(pos)->dup();
            lit_weight_[toInt(neg)] = cnf.weight(neg)->dup();
        }
        if (!multiplier_) multiplier_ = prototype_->one();
    }
}

SS CounterImpl::count() {
    assert(!counted_ && "Counter::count() called twice on the same Counter; construct a new Counter instead");
    counted_ = true;

    auto t_start = Clock::now();

    if (trivial_) {

        if (!ok) {
            count_time_sec_ = Duration(Clock::now() - t_start).count();
            return prototype_->zero();
        }

        SS result = applyGlobalFactors(prototype_->one());
        count_time_sec_ = Duration(Clock::now() - t_start).count();
        return result;
    }

    cmpmgr_->init(nVars(), nsvars_, ndvars_, clauses, ca, config.cache_mb,
                  config.cache_init_pow2);

    // Phase 2: the selector was built and prepared in setup(); now that the
    // dynamic search state is live, bind it to the frequency/activity arrays.
    VarSelectorArgs args{
        .frequency = cmpmgr_->freqRef(),
        .activity  = activity,
        .var_inc   = var_inc,
    };
    selector_->bind(args);

    count_main();
    cancelUntil(0);

    count_time_sec_ = Duration(Clock::now() - t_start).count();

    if (interrupted_)
        return nullptr;

    Decision& root = cmpmgr_->topDecision();
    if (!root.hasModel())
        return prototype_->zero();

    return applyGlobalFactors(root.totalModels());
}

// Apply the two global corrections the search itself does not carry into the
// per-component count: the isolated-projection-variable freedom (unweighted,
// a shift by 2^npvars_isolated_) and the folded-in weight factor multiplier_
// (weighted). Exactly one branch fires, since completeMccWeights/normalize
// route free vars into npvars_isolated_ only when unweighted and into multiplier_
// only when weighted.
SS CounterImpl::applyGlobalFactors(SS count) const {
    if (!is_weighted_ && npvars_isolated_ > 0) {
        // Unweighted counting always runs in the Integer semiring; shift_left
        // is Integer-specific. (Weighted free vars go through multiplier_ below.)
        assert(dynamic_cast<Integer*>(count.get())
               && "unweighted count expects the Integer semiring");
        static_cast<Integer*>(count.get())->shift_left(npvars_isolated_);
    }
    if (is_weighted_)
        count = count->mul(*multiplier_);
    return count;
}

bool CounterImpl::simplify() {
    if (!ok || propagate() != Glucose::CRef_Undef) { ok = false; return false; }
    if (nAssigns() == simpDB_assigns || simpDB_props > 0) return true;
    removeSatisfied(learnts);
    if (remove_satisfied) removeSatisfied(clauses);
    simpDB_assigns = nAssigns();
    simpDB_props   = clauses_literals + learnts_literals;
    nrmvsat_++;
    return true;
}

void CounterImpl::analyzeMC(Glucose::CRef confl, Glucose::vec<Glucose::Lit>& out_learnt,
                        int& out_btlevel, unsigned int& lbd,
                        unsigned int& szWithoutSelectors)
{

    using Glucose::Lit;
    using Glucose::Clause;
    using Glucose::CRef_Undef;
    using Glucose::lit_Undef;

    auto lbdOfLits = [&](const Glucose::vec<Lit>& lits, int end) -> unsigned {
        unsigned nblevels = 0;
        MYFLAG++;
        for (int k = 0; k < lits.size(); k++) {
            (void)end;
            int l = level(Glucose::var(lits[k]));
            if (permDiff[l] != MYFLAG) { permDiff[l] = MYFLAG; nblevels++; }
        }
        return nblevels;
    };
    auto lbdOfClause = [&](const Clause& cl) -> unsigned {
        unsigned nblevels = 0;
        MYFLAG++;
        for (int k = 0; k < cl.size(); k++) {
            int l = level(Glucose::var(cl[k]));
            if (permDiff[l] != MYFLAG) { permDiff[l] = MYFLAG; nblevels++; }
        }
        return nblevels;
    };

    int pathC = 0;
    Lit p     = lit_Undef;

    out_learnt.push();
    int index = trail.size() - 1;

    do {
        assert(confl != CRef_Undef);
        Clause& c = ca[confl];

        if (p != lit_Undef && c.size() == 2 && value(c[0]) == l_False) {
            assert(value(c[1]) == l_True);
            Lit tmp = c[0];
            c[0] = c[1], c[1] = tmp;
        }

        if (c.learnt())
            claBumpActivity(c);

#ifdef DYNAMICNBLEVEL

        if (c.learnt() && c.lbd() > 2) {
            unsigned int nblevels = lbdOfClause(c);
            if (nblevels + 1 < c.lbd()) {
                if (c.lbd() <= lbLBDFrozenClause)
                    c.setCanBeDel(false);
                c.setLBD(nblevels);
            }
        }
#endif

        for (int j = (p == lit_Undef) ? 0 : 1; j < c.size(); j++) {
            Lit q = c[j];
            if (!seen[Glucose::var(q)] && level(Glucose::var(q)) > 0) {
                varBumpActivity(Glucose::var(q));
                seen[Glucose::var(q)] = 1;
                if (level(Glucose::var(q)) >= decisionLevel()) {
                    pathC++;
#ifdef UPDATEVARACTIVITY

                    if (reason(Glucose::var(q)) != CRef_Undef
                            && ca[reason(Glucose::var(q))].learnt())
                        lastDecisionLevel.push(q);
#endif
                } else
                    out_learnt.push(q);
            }
        }

        do {
            while (!seen[Glucose::var(trail[index--])]);
            p     = trail[index + 1];
            confl = reason(Glucose::var(p));
            seen[Glucose::var(p)] = 0;
            pathC--;
            if (confl == CRef_Undef && pathC > 0) {
                out_learnt.push(~p);
                assert(level(Glucose::var(p)) <= decisionLevel());
            }
        } while (confl == CRef_Undef && pathC > 0);

    } while (pathC > 0);
    out_learnt[0] = ~p;

    int i, j;
    out_learnt.copyTo(analyze_toclear);
    if (ccmin_mode == 2) {
        uint32_t abstract_level = 0;
        for (i = 1; i < out_learnt.size(); i++)
            abstract_level |= abstractLevel(Glucose::var(out_learnt[i]));
        for (i = j = 1; i < out_learnt.size(); i++)
            if (reason(Glucose::var(out_learnt[i])) == CRef_Undef
                    || !litRedundant(out_learnt[i], abstract_level))
                out_learnt[j++] = out_learnt[i];
    } else if (ccmin_mode == 1) {
        for (i = j = 1; i < out_learnt.size(); i++) {
            Var x = Glucose::var(out_learnt[i]);
            if (reason(x) == CRef_Undef)
                out_learnt[j++] = out_learnt[i];
            else {
                Clause& cc = ca[reason(Glucose::var(out_learnt[i]))];
                for (int k = ((cc.size() == 2) ? 0 : 1); k < cc.size(); k++)
                    if (!seen[Glucose::var(cc[k])] && level(Glucose::var(cc[k])) > 0) {
                        out_learnt[j++] = out_learnt[i];
                        break;
                    }
            }
        }
    } else
        i = j = out_learnt.size();

    max_literals += out_learnt.size();
    out_learnt.shrink(i - j);
    tot_literals += out_learnt.size();

    if (out_learnt.size() <= lbSizeMinimizingClause)
        minimisationWithBinaryResolution(out_learnt);

    if (out_learnt.size() == 1)
        out_btlevel = 0;
    else {
        int max_i = 1;
        for (int k = 2; k < out_learnt.size(); k++)
            if (level(Glucose::var(out_learnt[k])) > level(Glucose::var(out_learnt[max_i])))
                max_i = k;
        Lit q             = out_learnt[max_i];
        out_learnt[max_i] = out_learnt[1];
        out_learnt[1]     = q;
        out_btlevel       = level(Glucose::var(q));
    }

    szWithoutSelectors = out_learnt.size();

    lbd = lbdOfLits(out_learnt, out_learnt.size());

#ifdef UPDATEVARACTIVITY

    if (lastDecisionLevel.size() > 0) {
        for (int k = 0; k < lastDecisionLevel.size(); k++)
            if (ca[reason(Glucose::var(lastDecisionLevel[k]))].lbd() < lbd)
                varBumpActivity(Glucose::var(lastDecisionLevel[k]));
        lastDecisionLevel.clear();
    }
#endif

    for (int k = 0; k < analyze_toclear.size(); k++) seen[Glucose::var(analyze_toclear[k])] = 0;
}

// Main search: splits components, caches sub-results, and recurses into
// internalSAT()/countExists() for non-projection components when the mode is
// projected (PMC/PWMC).
void CounterImpl::count_main() {
    Glucose::vec<Glucose::Lit> learnt;
    int backtrack_level;
    unsigned int nblevels, szWoutSelectors;

    for (;;) {

        if (stop_ && *stop_) { interrupted_ = true; break; }

        int bcp_start = trail.size();
        for (int i = 0; i < unitcls_.size(); i++) {
            enqueue(unitcls_[i]);
            vardata[Glucose::var(unitcls_[i])].level = 0;
        }
        if (decisionLevel() == 0) unitcls_.clear();

        Glucose::CRef confl = propagate();

        if (is_weighted_) {
            for (int i = bcp_start; i < trail.size(); i++) {
                Glucose::Lit l = trail[i];
                Var v = Glucose::var(l);
                if (v >= ndvars_) continue;
                if (decisionLevel() == 0) {
                    multiplier_->mul_inplace(*lit_weight_[Glucose::toInt(l)]);
                } else if (cmpmgr_->isDVarInComp(v)) {
                    cmpmgr_->topDecision().mulBranchWeight(*lit_weight_[Glucose::toInt(l)]);
                }
            }
        }

        BtState s = BtState::GO_NEXT_COMP;

        if (confl != Glucose::CRef_Undef) {
            conflicts++;
            if (conflicts % 5000 == 0 && var_decay < 0.95)
                var_decay += 0.01;
            if (decisionLevel() == 0) break;

            learnt.clear();
            analyzeMC(confl, learnt, backtrack_level, nblevels, szWoutSelectors);

            varDecayActivity();
            claDecayActivity();

            if (learnt.size() == 1) {
                unitcls_.push(learnt[0]);
            } else {
                Glucose::CRef learnt_cr = ca.alloc(learnt, true);
                learnts.push(learnt_cr);
                ca[learnt_cr].setLBD(nblevels);
                ca[learnt_cr].setSizeWithoutSelectors(szWoutSelectors);
                attachClause(learnt_cr);
                claBumpActivity(ca[learnt_cr]);
            }

            cmpmgr_->topDecision().markBranchUnsat();
            s = backtrack();
        } else {

            int nsplit = cmpmgr_->splitComponent(assigns,
                             is_weighted_ ? &lit_weight_ : nullptr);

            if (nsplit < 0) {

                while (cmpmgr_->topDecision().hasUnprocessedComp())
                    cmpmgr_->popComponent();
                cmpmgr_->removeCachePollutions();
                s = backtrack();
            } else if (nsplit == 0) {

                cmpmgr_->increaseTopModels(*prototype_->one());
                s = backtrack();
            } else if (is_projected_) {

                bool unsat = false;
                while (cmpmgr_->topDecision().hasUnprocessedComp()
                       && !cmpmgr_->topComponent().hasSVar()) {
                    nsatchecks_++;
                    auto satCheck = config.use_exists_check ? countExists() : internalSAT();
                    if (satCheck == l_Undef) {

                        interrupted_ = true;
                        break;
                    }
                    if (satCheck == l_True) {
                        cmpmgr_->storeModelCount(
                            cmpmgr_->topComponent().id(), prototype_->one());
                        cmpmgr_->increaseTopModels(*prototype_->one());
                        cmpmgr_->popComponent();
                    } else {

                        cmpmgr_->topDecision().markBranchUnsat();
                        while (cmpmgr_->topDecision().hasUnprocessedComp())
                            cmpmgr_->popComponent();
                        cmpmgr_->removeCachePollutions();
                        unsat = true;
                        break;
                    }
                }
                if (interrupted_) break;
                if (unsat || !cmpmgr_->topDecision().hasUnprocessedComp())
                    s = backtrack();
            }
        }

        if (s == BtState::EXIT) break;
        if (s == BtState::RESOLVED) continue;

        // Fixed-level in-processing simplify (old-GPMC site): just before
        // deciding, so it is reached once per descend-to-next-decision step,
        // including the last sibling of a multi-way split.
        if (config.inproc_simplify && cmpmgr_->checkfixedDL() && !simplify()) {
            cmpmgr_->topDecision().markBranchUnsat();
            while (cmpmgr_->topDecision().hasUnprocessedComp())
                cmpmgr_->popComponent();
            cmpmgr_->removeCachePollutions();
            if (backtrack() == BtState::EXIT) break;
            continue;
        }

        if (conflicts >= (uint64_t)curRestart * nbclausesbeforereduce) {
            assert(learnts.size() > 0);
            curRestart = (conflicts / nbclausesbeforereduce) + 1;
            reduceDB();
            nbclausesbeforereduce += incReduceDB;
        }

        Var v = selector_->select(cmpmgr_->topComponent());
        assert(v != VAR_UNDEF);
        decisions++;

        newDecisionLevel();
        cmpmgr_->pushDecision();

        Glucose::Lit dlit = Glucose::mkLit(v, polarity[v]);
        if (is_weighted_) {
            cmpmgr_->topDecision().setBranchWeight(
                lit_weight_[Glucose::toInt(dlit)]->dup());
            cmpmgr_->setDVarInComp();
        }

        uncheckedEnqueue(dlit);
    }
}

// SAT/UNSAT check on the current (non-projection) component: a plain DPLL
// search with no component splitting or caching, restarting the Glucose
// restart/reduceDB machinery scoped to pre_dl. Selected when
// config.use_exists_check is false; countExists() is the alternative.
Glucose::lbool CounterImpl::internalSAT() {
    Component& comp = cmpmgr_->topComponent();
    int pre_dl = decisionLevel();

    Glucose::vec<Glucose::Var> vs;
    for (int i = 0; comp[i] != VAR_UNDEF; i++) {
        Glucose::Var v = comp[i];
        if (assigns[v] == l_Undef)
            vs.push(v);
    }
    order_heap.build(vs);

    solves++;
    trailQueue.fastclear();
    lbdQueue.fastclear();
    sumLBD            = 0;
    conflictsRestarts = 1;
    starts++;
    bool blocked = false;

    Glucose::vec<Glucose::Lit> learnt;
    Glucose::vec<Glucose::Lit> selectors;
    int btlevel;
    unsigned nblevels, szWoutSel;

    for (;;) {

        if (stop_ && *stop_) { cancelUntil(pre_dl); return l_Undef; }

        Glucose::CRef confl = propagate();

        if (confl != Glucose::CRef_Undef) {
            conflicts++;
            conflictsRestarts++;
            if (conflicts % 5000 == 0 && var_decay < 0.95)
                var_decay += 0.01;

            if (decisionLevel() <= pre_dl) {

                if (config.learn_unsat_comp) {
                    learnt.clear();
                    analyzeMC(confl, learnt, btlevel, nblevels, szWoutSel);
                    varDecayActivity(); claDecayActivity();
                    cancelUntil(pre_dl);
                    if (learnt.size() == 1) {
                        unitcls_.push(learnt[0]);
                    } else {
                        Glucose::CRef cr = ca.alloc(learnt, true);
                        learnts.push(cr);
                        ca[cr].setLBD(nblevels);
                        ca[cr].setSizeWithoutSelectors(szWoutSel);
                        attachClause(cr);
                        claBumpActivity(ca[cr]);
                    }
                } else {
                    cancelUntil(pre_dl);
                }
                return l_False;
            }

            trailQueue.push(trail.size());
            if (conflictsRestarts > LOWER_BOUND_FOR_BLOCKING_RESTART
                    && lbdQueue.isvalid()
                    && trail.size() > R * trailQueue.getavg()) {
                lbdQueue.fastclear();
                nbstopsrestarts++;
                if (!blocked) {
                    lastblockatrestart = starts;
                    nbstopsrestartssame++;
                    blocked = true;
                }
            }

            learnt.clear(); selectors.clear();
            analyze(confl, learnt, selectors, btlevel, nblevels, szWoutSel);
            lbdQueue.push(nblevels);
            sumLBD += nblevels;

            cancelUntil(std::max(btlevel, pre_dl));

            if (learnt.size() == 1) {
                unitcls_.push(learnt[0]);
                uncheckedEnqueue(learnt[0]);
                vardata[Glucose::var(learnt[0])].level = 0;
                nbUn++;
            } else {
                Glucose::CRef cr = ca.alloc(learnt, true);
                learnts.push(cr);
                ca[cr].setLBD(nblevels);
                ca[cr].setSizeWithoutSelectors(szWoutSel);
                if (nblevels <= 2) nbDL2++;
                if (ca[cr].size() == 2) nbBin++;
                attachClause(cr);
                claBumpActivity(ca[cr]);
                uncheckedEnqueue(learnt[0], cr);
            }
            varDecayActivity();
            claDecayActivity();
        } else {

            if (lbdQueue.isvalid()
                    && (lbdQueue.getavg() * K) > (sumLBD / conflictsRestarts)) {
                lbdQueue.fastclear();
                cancelUntil(pre_dl);
                blocked = false;
                starts++;
                continue;
            }

            if (conflicts >= (uint64_t)curRestart * nbclausesbeforereduce) {
                assert(learnts.size() > 0);
                curRestart = (conflicts / nbclausesbeforereduce) + 1;
                reduceDB();
                nbclausesbeforereduce += incReduceDB;
            }

            decisions++;
            Glucose::Lit next = pickBranchLit();
            if (next == Glucose::lit_Undef) {

                cancelUntil(pre_dl);
                return l_True;
            }
            newDecisionLevel();
            uncheckedEnqueue(next);
        }
    }
}

// SAT/UNSAT check on the current (non-projection) component, alternative to
// internalSAT(): splits into sub-components and reuses the component cache
// during the check, at the cost of maintaining a decision/component stack.
// Selected when config.use_exists_check is true.
Glucose::lbool CounterImpl::countExists() {
    int pre_dl = decisionLevel();
    exists_pre_dl_ = pre_dl;
    exists_found_  = false;

    Glucose::vec<Glucose::Lit> learnt;
    int backtrack_level;
    unsigned int nblevels, szWoutSelectors;

    for (;;) {

        if (stop_ && *stop_) { cancelUntil(pre_dl); return l_Undef; }

        for (int i = 0; i < unitcls_.size(); i++) {
            enqueue(unitcls_[i]);
            vardata[Glucose::var(unitcls_[i])].level = 0;
        }
        if (decisionLevel() == 0) unitcls_.clear();

        Glucose::CRef confl = propagate();
        BtState s = BtState::GO_NEXT_COMP;

        if (confl != Glucose::CRef_Undef) {
            conflicts++;
            if (conflicts % 5000 == 0 && var_decay < 0.95)
                var_decay += 0.01;
            if (decisionLevel() == pre_dl) {

                if (config.learn_unsat_comp) {
                    learnt.clear();
                    analyzeMC(confl, learnt, backtrack_level, nblevels, szWoutSelectors);
                    varDecayActivity(); claDecayActivity();
                    cancelUntil(pre_dl);
                    if (learnt.size() == 1) {
                        unitcls_.push(learnt[0]);
                    } else {
                        Glucose::CRef cr = ca.alloc(learnt, true);
                        learnts.push(cr);
                        ca[cr].setLBD(nblevels);
                        ca[cr].setSizeWithoutSelectors(szWoutSelectors);
                        attachClause(cr);
                        claBumpActivity(ca[cr]);
                    }
                } else {
                    cancelUntil(pre_dl);
                }
                return l_False;
            }

            learnt.clear();
            analyzeMC(confl, learnt, backtrack_level, nblevels, szWoutSelectors);
            varDecayActivity(); claDecayActivity();

            if (learnt.size() == 1) {
                unitcls_.push(learnt[0]);
            } else {
                Glucose::CRef cr = ca.alloc(learnt, true);
                learnts.push(cr);
                ca[cr].setLBD(nblevels);
                ca[cr].setSizeWithoutSelectors(szWoutSelectors);
                attachClause(cr);
                claBumpActivity(ca[cr]);
            }
            cmpmgr_->topDecision().markBranchUnsat();
            s = backtrackExists();
        } else {

            int nsplit = cmpmgr_->splitComponent(assigns,
                             is_weighted_ ? &lit_weight_ : nullptr);

            if (nsplit < 0) {

                while (cmpmgr_->topDecision().hasUnprocessedComp())
                    cmpmgr_->popComponent();
                cmpmgr_->removeCachePollutions();
                s = backtrackExists();
            } else if (nsplit == 0) {

                cmpmgr_->increaseTopModels(*prototype_->one());
                s = backtrackExists();
            } else {

            }
        }

        if (s == BtState::EXIT) break;
        if (s == BtState::RESOLVED) continue;

        Var v = selectExists(cmpmgr_->topComponent());
        assert(v != VAR_UNDEF);
        decisions++;
        newDecisionLevel();
        cmpmgr_->pushDecision();
        uncheckedEnqueue(Glucose::mkLit(v, polarity[v]));
    }

    return exists_found_ ? l_True : l_False;
}

// Same VSADS formula as VSADSSelector::computeScore, with the same (untouched)
// weights, so this experimental arm is not handicapped relative to the main
// search. Local copy because the selector's select() iterates only the D-set
// prefix of a component, and these components carry no D-set vars.
Var CounterImpl::selectExists(const Component& comp) const {
    const auto&  freq   = cmpmgr_->freqRef();
    const double w_freq = selector_config.params.w_freq;
    const double w_act  = selector_config.params.w_act;
    Var    best       = VAR_UNDEF;
    double best_score = -1.0;
    for (int i = 0; i < comp.nVars(); i++) {
        Var v = comp[i];
        if (assigns[v] != l_Undef) continue;
        double s = w_freq * freq[v] + w_act * activity[v] / var_inc;
        if (s > best_score) { best_score = s; best = v; }
    }
    return best;
}

CounterImpl::BtState CounterImpl::backtrackExists() {
    for (;;) {
        if (decisionLevel() == exists_pre_dl_) return BtState::EXIT;

        Decision& cur = cmpmgr_->topDecision();

        if (cur.isFirstBranch() && !(cur.hasModel() && !cur.totalModels()->is_zero())) {
            Glucose::Lit dlit = trail[trail_lim.last()];
            cancelCurDL();
            uncheckedEnqueue(~dlit);
            cur.changeBranch();
            return BtState::RESOLVED;
        }

        SS val = cur.totalModels();
        if (val && !val->is_zero()) {
            cmpmgr_->storeModelCount(cmpmgr_->topComponent().id(),
                                     prototype_->one());
        } else if (config.cache_unsat) {
            cmpmgr_->storeZeroCount(cmpmgr_->topComponent().id());
        }

        cmpmgr_->propagateTopResult(val);
        cancelCurDL();
        trail_lim.pop();
        cmpmgr_->popDecision();
        cmpmgr_->popComponent();

        if (decisionLevel() == exists_pre_dl_) {
            exists_found_ = (val && !val->is_zero());
            return BtState::EXIT;
        }
        if (cmpmgr_->topDecision().isUnsat()) {
            while (cmpmgr_->topDecision().hasUnprocessedComp())
                cmpmgr_->popComponent();
            cmpmgr_->removeCachePollutions();
        } else if (cmpmgr_->topDecision().hasUnprocessedComp()) {
            return BtState::GO_NEXT_COMP;
        }
    }
}

CounterImpl::BtState CounterImpl::backtrack() {
    for (;;) {
        if (decisionLevel() == 0) return BtState::EXIT;

        Decision& cur = cmpmgr_->topDecision();
        if (is_weighted_) cur.mulLitsWeights();

        if (cur.isFirstBranch()) {

            Glucose::Lit dlit = trail[trail_lim.last()];
            cancelCurDL();
            uncheckedEnqueue(~dlit);
            cur.changeBranch();
            if (is_weighted_)
                cur.setBranchWeight(lit_weight_[Glucose::toInt(~dlit)]->dup());
            return BtState::RESOLVED;
        } else {

            SS val = cur.totalModels();

            if (val && !val->is_zero()) {
                cmpmgr_->storeModelCount(cmpmgr_->topComponent().id(), val->dup());
            } else if (config.cache_unsat) {
                cmpmgr_->storeZeroCount(cmpmgr_->topComponent().id());
            }

            cmpmgr_->propagateTopResult(val);

            cancelCurDL();
            trail_lim.pop();
            cmpmgr_->popDecision();
            cmpmgr_->popComponent();

            if (cmpmgr_->topDecision().isUnsat()) {
                while (cmpmgr_->topDecision().hasUnprocessedComp())
                    cmpmgr_->popComponent();
                cmpmgr_->removeCachePollutions();
            } else if (cmpmgr_->topDecision().hasUnprocessedComp()) {
                return BtState::GO_NEXT_COMP;
            }
        }
    }
}

void CounterImpl::cancelCurDL() {
    int lim = trail_lim.last();
    for (int i = trail.size() - 1; i >= lim; i--) {
        Glucose::Var x = Glucose::var(trail[i]);
        assigns[x] = l_Undef;
        polarity[x] = Glucose::sign(trail[i]);
    }
    trail.shrink(trail.size() - lim);
    qhead = lim;
}

void CounterImpl::printStats() const {

    if (trivial_) {
        std::printf("c o   %s\n", "skip (solved during simplification)");
        return;
    }
    std::printf("c o   %-22s %lu\n",  "conflicts",     conflicts);
    std::printf("c o   %-22s %lu\n",  "decisions",     decisions);
    std::printf("c o   %-22s %lu\n",  "propagations",  propagations);
    std::printf("c o   %-22s %d\n",   "sat_removals",  nrmvsat_);
    std::printf("c o   %-22s %lu\n",  "sat_checks",    nsatchecks_);
    std::printf("c o   %-22s %lu\n",  "reducedb_calls",  nbReduceDB);
    std::printf("c o   %-22s %lu\n",  "learnts_removed", nbRemovedClauses);
    std::printf("c o   %-22s %d\n",   "learnts_final",   nLearnts());
    cmpmgr_->printStats();
    std::printf("c o   %-22s %.3f\n", "time (sec)",    count_time_sec_);
    std::fflush(stdout);
}

}
