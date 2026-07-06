#include "include/gpmc/Preprocessor.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <unordered_map>
#include <vector>

#include "cadical.hpp"
#include "tracer.hpp"

#include "include/gpmc/CNF.h"
#include "utils/Timing.h"

namespace gpmc {

// Collects fixed (root-level assigned) variables from CaDiCaL's listener callback.
struct FixedCollector : CaDiCaL::FixedAssignmentListener {
    std::vector<bool> is_fixed;  // var (0-based) is fixed
    std::vector<int>  lits;      // fixed literals, de-duplicated, sign kept

    void reset(int num_vars) {
        is_fixed.assign(num_vars, false);
        lits.clear();
    }
    void notify_fixed_assignment(int lit) override {
        int v = std::abs(lit) - 1;
        assert(v >= 0 && v < (int)is_fixed.size());
        if (is_fixed[v]) return;
        is_fixed[v] = true;
        lits.push_back(lit);
    }
};

struct EquivCollector : CaDiCaL::Tracer {
    std::unordered_map<int, int> parent;

    // Iterative (not recursive) to avoid stack overflow on long parent chains.
    int find(int l) {
        int r = l;
        for (auto it = parent.find(r); it != parent.end(); it = parent.find(r))
            r = it->second;
        while (true) {  // second pass: path compression
            auto it = parent.find(l);
            if (it == parent.end()) break;
            l = it->second;
            it->second = r;
        }
        return r;
    }

    void notify_equivalence(int lit1, int lit2) override {
        assert(lit1 != 0 && lit2 != 0);
        int r1 = find(lit1), r2 = find(lit2);
        if (std::abs(r1) == std::abs(r2)) return;
        parent[r1]  = r2;
        parent[-r1] = -r2;
    }

    void clear() { parent.clear(); }

    std::unordered_map<int, std::vector<int>> classes() {
        std::unordered_map<int, std::vector<int>> cls;
        for (const auto& [lit, par] : parent)
            if (lit > 0) cls[std::abs(find(lit))].push_back(lit);
        for (auto& [root, members] : cls)
            members.push_back(root);
        return cls;
    }
};

struct ClauseGatherer : CaDiCaL::ClauseIterator {
    std::vector<std::vector<int>>& clauses;
    std::vector<bool>*             appears;
    ClauseGatherer(std::vector<std::vector<int>>& c, std::vector<bool>* a)
        : clauses(c), appears(a) {}
    // Units are dropped: fixed variables are already tracked via fixed_col,
    // so keeping them here would double-count against foldFixedWeights.
    bool clause(const std::vector<int>& cl) override {
        if (cl.size() < 2) return true;
        clauses.push_back(cl);
        if (appears)
            for (int lit : cl) (*appears)[std::abs(lit)] = true;
        return true;
    }
};

struct ClauseForwarder : CaDiCaL::ClauseIterator {
    CaDiCaL::Solver& dst;
    std::vector<bool>* appears;
    explicit ClauseForwarder(CaDiCaL::Solver& d, std::vector<bool>* a = nullptr)
        : dst(d), appears(a) {}
    // Units dropped, same reason as ClauseGatherer.
    bool clause(const std::vector<int>& cl) override {
        if (cl.size() < 2) return true;
        for (int l : cl) {
            dst.add(l);
            if (appears) (*appears)[std::abs(l)] = true;
        }
        dst.add(0);
        return true;
    }
    bool clause(const std::vector<int>& cl, bool redundant) override {
        if (redundant && cl.size() >= 2) {
            dst.add_redundant_clause(cl, 2);
            if (appears)
                for (int l : cl) (*appears)[std::abs(l)] = true;
            return true;
        }
        return clause(cl);
    }
};

static Lit cadical_to_gpmc(int clit) {
    return mkLit(std::abs(clit) - 1, clit < 0);
}

// Shared CaDiCaL setup: silence output, and disable factoring since our
// fixed-size bookkeeping assumes the variable numbering never grows.
static void init_solver(CaDiCaL::Solver& s) {
    s.set("verbose", 0);
    s.set("report", 0);
    s.set("factor", 0);
}

struct Preprocessor::PrepState {
    FixedCollector fixed_col;
    EquivCollector equiv_col;
    std::vector<std::vector<int>> clauses;
    std::vector<bool> appears;
    std::vector<bool> substituted;
    std::vector<bool> eliminated;
    bool weighted = false;
    SS   multiplier;

    // w is never null: projection-variable weights are total (invariant 1 at
    // preprocess entry; foldEquivalences copies both polarities onto promoted
    // survivors).
    void multiplyFactor(const SS& w) {
        assert(w);
        multiplier->mul_inplace(*w);
    }
};

void Preprocessor::foldEquivalences(CNF& cnf, PrepState& st) const {
    for (auto& [root, members] : st.equiv_col.classes()) {
#ifndef NDEBUG
        for (int m : members)
            assert(m <= cnf.numVars());
#endif
        // Pick the survivor: prefer a member that already appears in a kept
        // clause or is fixed, else fall back to the class's root.
        int surv = root;
        for (int m : members)
            if (st.appears[m] || st.fixed_col.is_fixed[m - 1]) { surv = m; break; }

        bool any_proj = false;
        for (int m : members)
            if (cnf.isProj(m - 1)) { any_proj = true; break; }
        if (any_proj)
            cnf.setProjected(surv - 1);

        if (st.weighted) {
            auto fold = [&](Lit from, Lit to) {
                if (!cnf.hasWeight(from)) return;
                if (cnf.hasWeight(to))
                    cnf.setWeight(to, cnf.weight(to)->mul(*cnf.weight(from)));
                else
                    cnf.setWeight(to, cnf.weight(from)->dup());
            };
            int rs = st.equiv_col.find(surv);
            for (int m : members) {
                if (m == surv) continue;
                int rm = st.equiv_col.find(m);
                bool same = (rm > 0) == (rs > 0);
                fold(mkLit(m - 1, false), mkLit(surv - 1, !same));
                fold(mkLit(m - 1, true),  mkLit(surv - 1,  same));
            }
        }

        for (int m : members)
            if (m != surv)
                st.substituted[m - 1] = true;
    }

    st.equiv_col.clear();
}

void Preprocessor::foldFixedWeights(const CNF& cnf, PrepState& st) const {
    if (!st.weighted) return;
    for (int flit : st.fixed_col.lits) {
        int v = std::abs(flit) - 1;
        if (!cnf.isProj(v)) continue;
        st.multiplyFactor(cnf.weight(cadical_to_gpmc(flit)));
    }
}

void Preprocessor::foldIsolatedVars(const CNF& cnf, PrepState& st, PreprocessingResult& res) const {
    for (int v = 0; v < cnf.numVars(); v++) {
        if (!cnf.isProj(v))     continue;
        if (st.fixed_col.is_fixed[v]) continue;
        if (st.substituted[v])  continue;
        if (st.eliminated[v])   continue;
        if (st.appears[v + 1])  continue;

        res.isolated_pvars++;
        if (st.weighted) {
            SS sum = cnf.weight(mkLit(v, false))->add(*cnf.weight(mkLit(v, true)));
            st.multiplyFactor(sum);
        }
    }
}

// New 0-based numbering for surviving variables: projection vars first
// (0..new_npvars-1), then the rest. remap is old 1-based var -> new, or -1.
struct Renumber {
    std::vector<int> remap;
    int new_npvars = 0;
    int new_nvars  = 0;
};

static Renumber renumberLiveVars(const CNF& cnf, const std::vector<bool>& appears) {
    std::vector<int> proj_live, nonproj_live;
    for (int v1 = 1; v1 <= cnf.numVars(); v1++) {
        if (!appears[v1]) continue;
        (cnf.isProj(v1 - 1) ? proj_live : nonproj_live).push_back(v1);
    }

    Renumber rn;
    rn.remap.assign(cnf.numVars() + 1, -1);
    int idx = 0;
    for (int v1 : proj_live)    rn.remap[v1] = idx++;
    rn.new_npvars = idx;
    for (int v1 : nonproj_live) rn.remap[v1] = idx++;
    rn.new_nvars = idx;
    return rn;
}

static std::vector<std::vector<Lit>> remapClauses(const std::vector<std::vector<int>>& clauses,
                                                  const std::vector<int>& remap) {
    std::vector<std::vector<Lit>> out;
    out.reserve(clauses.size());
    for (const auto& cl : clauses) {
        std::vector<Lit> gc;
        gc.reserve(cl.size());
        for (int clit : cl) {
            int nv = remap[std::abs(clit)];
            assert(nv >= 0);
            gc.push_back(mkLit(nv, clit < 0));
        }
        out.push_back(std::move(gc));
    }
    return out;
}

static std::vector<SS> remapWeights(const CNF& cnf, const Renumber& rn) {
    std::vector<SS> out(2 * rn.new_npvars);
    for (int v1 = 1; v1 <= cnf.numVars(); v1++) {
        if (!cnf.isProj(v1 - 1)) continue;
        int v_new = rn.remap[v1];
        if (v_new < 0) continue;  // isolated/fixed/substituted: no slot, already in the multiplier
        for (bool neg : {false, true})
            out[toInt(mkLit(v_new, neg))] =
                cnf.weight(mkLit(v1 - 1, neg))->dup();
    }
    return out;
}

void Preprocessor::rebuildCNF(CNF& cnf, PrepState& st) const {
    Renumber rn = renumberLiveVars(cnf, st.appears);

    auto new_clauses = remapClauses(st.clauses, rn.remap);
    std::vector<SS> new_weights;
    if (st.weighted)
        new_weights = remapWeights(cnf, rn);

    cnf.num_vars_      = rn.new_nvars;
    cnf.num_proj_vars_ = rn.new_npvars;
    cnf.is_proj_.assign(rn.new_npvars, true);
    cnf.is_proj_.resize(rn.new_nvars, false);
    cnf.clauses_  = std::move(new_clauses);
    cnf.weights_  = std::move(new_weights);
}

std::vector<int> Preprocessor::detectDefinable(const std::vector<std::vector<int>>& clauses,
                                               const std::vector<bool>& base,
                                               const std::vector<int>& candidates,
                                               double time_limit_sec,
                                               int conflict_limit) const {
    std::vector<int> result;
    if (candidates.empty()) return result;

    const int n = (int)base.size();
    const int k = (int)candidates.size();

    auto orig = [](int v0)  { return v0 + 1; };
    auto dup  = [&](int v0) { return n + 1 + v0; };

    // cand_sel[v0] is the selector-variable index for candidate v0 (0 for
    // non-candidates; never read for those). A plain array keyed by variable,
    // not a hash map: the definability loop below probes it heavily.
    std::vector<int> cand_sel(n, 0);
    std::vector<bool> is_cand(n, false);
    for (int i = 0; i < k; i++) {
        cand_sel[candidates[i]] = 2 * n + 1 + i;
        is_cand[candidates[i]] = true;
    }

    auto duplicated = [&](int v0) {
        return is_cand[v0] || !base[v0];
    };

    CaDiCaL::Solver solver;
    init_solver(solver);

    solver.resize(2 * n + k);

    auto lit = [&](int clit, bool dup_ns) {
        int v0 = std::abs(clit) - 1;
        int enc = dup_ns ? dup(v0) : orig(v0);
        return clit < 0 ? -enc : enc;
    };

    for (const auto& cl : clauses) {
        bool any_dup = false;
        for (int clit : cl) if (duplicated(std::abs(clit) - 1)) { any_dup = true; break; }

        for (int clit : cl)
            solver.add(lit(clit, false));
        solver.add(0);

        if (any_dup) {
            for (int clit : cl)
                solver.add(lit(clit, duplicated(std::abs(clit) - 1)));
            solver.add(0);
        }
    }

    for (int c : candidates) {
        int sel = cand_sel[c];
        solver.add(-sel); solver.add(orig(c));  solver.add(-dup(c)); solver.add(0);
        solver.add(-sel); solver.add(-orig(c)); solver.add( dup(c)); solver.add(0);
        solver.freeze(orig(c));
        solver.freeze(dup(c));
        solver.freeze(sel);
    }

    const double cpu_t0 = cpu_sec();  // CPU time, so this budget is reproducible regardless of machine load
    std::vector<bool> defined(n, false);
    for (int v : candidates) {
        if (cpu_sec() - cpu_t0 > time_limit_sec) break;

        solver.assume(orig(v));
        solver.assume(-dup(v));
        for (int w : candidates) {
            if (w == v || defined[w]) continue;
            solver.assume(cand_sel[w]);
        }
        if (conflict_limit > 0) solver.limit("conflicts", conflict_limit);
        if (solver.solve() == 20) {
            defined[v] = true;
            result.push_back(v);
            solver.melt(orig(v));
            solver.melt(dup(v));
            solver.melt(cand_sel[v]);
        }
    }
    return result;
}

void Preprocessor::applyDVE(CNF& cnf, CaDiCaL::Solver& solver, PrepState& st,
                            double t0) const {
    const int active_before = solver.active();
    const int nv = cnf.numVars();

    auto log_phase3 = [&] {
        if (!config_.pp_verbose) return;
        std::printf("c o   [PP phase3 DVE]   vars %d  clauses %ld  learnts %ld  eliminated %d  (%.2fs)\n",
            solver.active(), (long)solver.irredundant(), (long)solver.redundant(),
            active_before - solver.active(), now_sec() - t0);
        std::fflush(stdout);
    };

    std::vector<std::vector<int>> clauses;
    std::vector<int> fpos(nv + 1, 0), fneg(nv + 1, 0);
    {
        ClauseGatherer g(clauses, nullptr);
        solver.traverse_clauses(g);
        for (const auto& cl : clauses)
            for (int clit : cl) (clit > 0 ? fpos : fneg)[std::abs(clit)]++;
    }

    std::vector<bool> base(nv, false);
    std::vector<int>  candidates;
    const bool weighted = st.weighted;
    for (int v = 0; v < nv; v++) {
        if (!cnf.isProj(v)) continue;
        base[v] = true;
        int fp = fpos[v + 1], fn = fneg[v + 1];

        if (fp == 0 && fn == 0) continue;
        bool cheap = (long)fp * fn <= (long)fp + fn;
        bool low_freq = config_.dve_low_freq_max > 0 &&
            std::min(fp, fn) <= config_.dve_low_freq_max;

        bool wt_ok = !weighted ||
            cnf.weight(mkLit(v, false))->equals(*cnf.weight(mkLit(v, true)));
        if ((cheap || low_freq) && wt_ok) candidates.push_back(v);
    }
    if (candidates.empty()) { log_phase3(); return; }

    std::vector<int> definable =
        detectDefinable(clauses, base, candidates, config_.dve_time_limit,
                         config_.solve_conflict_limit);
    if (definable.empty()) { log_phase3(); return; }

    std::vector<bool> elim(nv, false);
    for (int v : definable) { elim[v] = true; solver.melt(v + 1); }

    for (int v = 0; v < nv; v++)
        if (!elim[v] && (fpos[v + 1] || fneg[v + 1])) solver.freeze(v + 1);
    solver.simplify(config_.rounds);

    // Fold only variables CaDiCaL actually eliminated: a mutually-definable
    // cluster (e.g. XOR a^b^c=0) is rank-deficient, so some "definable"
    // candidates survive as free variables and must reach foldIsolatedVars
    // instead. wt_ok above guarantees w(v,false) == w(v,true) here.
    for (int v : definable)
        if (solver.eliminated(v + 1)) {
            st.eliminated[v] = true;
            if (weighted)
                st.multiplier->mul_inplace(*cnf.weight(mkLit(v, false)));
        }

    log_phase3();
}

bool Preprocessor::simplifyWithoutBVE(CNF& cnf, PrepState& st,
                                      std::unique_ptr<CaDiCaL::Solver> prev_solverB,
                                      CaDiCaL::Solver& solverB, double t0) const {
    CaDiCaL::Solver solverA;
    init_solver(solverA);
    solverA.set("elim", 0);
    solverA.set("fastelim", 0);
    solverA.connect_fixed_listener(&st.fixed_col);
    solverA.connect_proof_tracer(&st.equiv_col, false);

    if (prev_solverB) {
        ClauseForwarder fwd(solverA);
        prev_solverB->traverse_clauses_with_elite_learnts(fwd);
        prev_solverB->disconnect_fixed_listener();
        prev_solverB.reset();  // fully consumed, free before this round's solve
    } else {
        for (const auto& cl : cnf.clauses()) {
            for (Lit l : cl)
                solverA.add(sign(l) ? -(var(l) + 1) : (var(l) + 1));
            solverA.add(0);
        }
    }

    int status = solverA.simplify(config_.rounds);
    solverA.disconnect_fixed_listener();
    solverA.disconnect_proof_tracer(&st.equiv_col);
    if (status == 20)
        return false;
    if (config_.pp_verbose) {
        std::printf("c o   [PP phase1 simp]  vars %d  clauses %ld  learnts %ld  equiv_classes %zu  (%.2fs)\n",
            solverA.active(), (long)solverA.irredundant(), (long)solverA.redundant(),
            st.equiv_col.classes().size(), now_sec() - t0);
        std::fflush(stdout);
    }

    solverB.connect_fixed_listener(&st.fixed_col);
    st.appears.assign(cnf.numVars() + 1, false);
    ClauseForwarder fwd(solverB, &st.appears);
    solverA.traverse_clauses_with_elite_learnts(fwd);

    foldEquivalences(cnf, st);

    return true;
}

bool Preprocessor::solveAndSimplifyWithBVE(CNF& cnf, PrepState& st,
                                           CaDiCaL::Solver& solverB, double t0,
                                           int round) const {
    for (int v = 0; v < cnf.numVars(); v++)
        if (cnf.isProj(v) && st.appears[v + 1])
            solverB.freeze(v + 1);

    if (round > 0 && config_.solve_conflict_limit > 0)
        solverB.limit("conflicts", config_.solve_conflict_limit);
    if (solverB.solve() == 20) {
        solverB.disconnect_fixed_listener();
        return false;
    }
    if (config_.pp_verbose) {
        std::printf("c o   [PP phase2 solve] vars %d  clauses %ld  learnts %ld  (%.2fs)\n",
            solverB.active(), (long)solverB.irredundant(), (long)solverB.redundant(), now_sec() - t0);
        std::fflush(stdout);
    }

    // Explicit BVE; frozen projection vars above are excluded from elimination.
    {
        const int active_before_bve = solverB.active();
        solverB.simplify(config_.rounds);
        if (config_.pp_verbose) {
            std::printf("c o   [PP phase2 BVE]   vars %d  clauses %ld  learnts %ld  eliminated %d  (%.2fs)\n",
                solverB.active(), (long)solverB.irredundant(), (long)solverB.redundant(),
                active_before_bve - solverB.active(), now_sec() - t0);
            std::fflush(stdout);
        }
    }

    return true;
}

void Preprocessor::finalize(CNF& cnf, PrepState& st,
                            std::unique_ptr<CaDiCaL::Solver> solverB,
                            PreprocessingResult& res) const {
    st.clauses.clear();
    st.appears.assign(cnf.numVars() + 1, false);
    ClauseGatherer gatherer(st.clauses, &st.appears);
    solverB->traverse_clauses(gatherer);
    solverB->disconnect_fixed_listener();
    solverB.reset();

    res.fixed_vars = (int)st.fixed_col.lits.size();
    foldFixedWeights(cnf, st);
    foldIsolatedVars(cnf, st, res);
    rebuildCNF(cnf, st);

    res.multiplier    = std::move(st.multiplier);
    res.vars_after    = cnf.numVars();
    res.pvars_after   = cnf.numProjVars();
    res.nsvars        = cnf.numProjVars();
    res.ndvars        = cnf.numProjVars();
    res.clauses_after = cnf.numClauses();
}

PreprocessingResult Preprocessor::preprocess(CNF& cnf) const {
    PreprocessingResult res;
    res.vars_before    = cnf.numVars();
    res.pvars_before   = cnf.numProjVars();
    res.clauses_before = cnf.numClauses();

    const double t0 = now_sec();

    // No variables: trivially satisfiable, nothing to simplify. (A CNF with
    // variables but no clauses is NOT short-circuited -- it flows through the
    // loop below and gets folded by foldIsolatedVars.)
    if (cnf.numVars() == 0) {
        res.vars_after = res.pvars_after = res.nsvars = res.ndvars = 0;
        res.clauses_after = 0;
        res.elapsed_sec = now_sec() - t0;
        return res;
    }

    // Huge instances get a light pass instead (one round, no DVE) rather than
    // being skipped outright. At least one round always runs: the final pass
    // depends on the loop's clause gather / appears.
    const bool light_pass     = (cnf.numVars() > config_.var_limit);
    const int  eff_max_rounds = light_pass ? 1 : std::max(1, config_.max_rounds);

#ifndef NDEBUG
    if (cnf.isWeighted())
        for (int v = 0; v < cnf.numVars(); v++)
            if (cnf.isProj(v))
                assert(cnf.hasWeight(mkLit(v, false)) && cnf.hasWeight(mkLit(v, true)) &&
                       "weighted mode: projection variable missing a polarity weight (invariant 1)");
#endif

    PrepState st;
    st.weighted = cnf.isWeighted();
    st.substituted.assign(cnf.numVars(), false);
    st.eliminated.assign(cnf.numVars(), false);
    st.fixed_col.reset(cnf.numVars());

    // Continues the multiplier chain from an earlier pass (e.g. gpmc-pp's
    // "c MUST MULTIPLY BY"), if any.
    if (cnf.isWeighted())
        st.multiplier = cnf.multiplier_ ? cnf.multiplier_->dup()
                                           : cnf.weightOne();

    const double cpu_t0 = cpu_sec();  // CPU time for the time limit; t0 stays wall-clock for elapsed_sec
    long prev_clauses = -1;
    int  prev_active  = -1;
    std::unique_ptr<CaDiCaL::Solver> prev_solverB;
    for (int round = 0; round < eff_max_rounds; round++) {
        if (round > 0 && config_.pp_time_limit > 0.0
                && cpu_sec() - cpu_t0 > config_.pp_time_limit)
            break;

        auto solverB = std::make_unique<CaDiCaL::Solver>();
        init_solver(*solverB);
        solverB->set("decompose", 0);  // equivalence detection off in phase2/3
        solverB->set("sweep", 0);

        if (!simplifyWithoutBVE(cnf, st, std::move(prev_solverB), *solverB, t0)
                || !solveAndSimplifyWithBVE(cnf, st, *solverB, t0, round)) {
            res.sat = false;
            res.elapsed_sec = now_sec() - t0;
            return res;
        }

        if (!light_pass && config_.pp_dve && cnf.numProjVars() >= config_.dve_min_pvars)
            applyDVE(cnf, *solverB, st, t0);

        long clause_count = (long)solverB->irredundant();
        int  active       = solverB->active();
        bool fixed_point = (clause_count == prev_clauses && active == prev_active);
        prev_clauses = clause_count;
        prev_active  = active;

        prev_solverB = std::move(solverB);
        res.pp_rounds++;
        if (active == 0 || fixed_point) break;
    }

    finalize(cnf, st, std::move(prev_solverB), res);
    res.elapsed_sec = now_sec() - t0;

    return res;
}

}
